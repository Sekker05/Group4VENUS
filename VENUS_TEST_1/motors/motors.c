#include <arm_shared_memory_system.h>
#include <libpynq.h>
#include <zmq.h>
#include <cjson/cJSON.h>
#include <platform.h>
#include <stdint.h>
#include <stepper.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>     // Used for nanosleep()
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define PIPE_IN  "ipc:///tmp/emb2mot.ipc"

// --- Custom 25-Degree Function (Updated to High-Torque 15000 Speed) ---
void execute_25_degree_twitch(void) {
  struct timespec ts_1ms;
  ts_1ms.tv_sec = 0;
  ts_1ms.tv_nsec = 1000000;

  struct timespec ts_3s;
  ts_3s.tv_sec = 3;
  ts_3s.tv_nsec = 0;

  // Enforcing the low-error high speed
  stepper_set_speed(15000, 15000);

  // 1. Swing Left Wheel forward 25 degrees (594 steps)
  printf(">> Executing precise 25-degree forward swing (594 steps)...\n");
  stepper_steps(0, 594); 

  while (!stepper_steps_done()) {
    nanosleep(&ts_1ms, NULL);
  }

  // 2. Pause 3 seconds
  printf(">> Pausing for 3 seconds...\n");
  nanosleep(&ts_3s, NULL);

  // 3. Reverse Left Wheel back to place
  printf(">> Executing precise 25-degree backward swing...\n");
  stepper_steps(0, -594);

  while (!stepper_steps_done()) {
    nanosleep(&ts_1ms, NULL);
  }

  printf(">> Twitch sequence complete.\n\n");
}

int main(void) {  
  pynq_init();  

  void *ctx      = zmq_ctx_new();
  void *receiver = zmq_socket(ctx, ZMQ_SUB);

  zmq_connect(receiver, PIPE_IN);
  zmq_setsockopt(receiver, ZMQ_SUBSCRIBE, "", 0);  // subscribe to all

  char buf[1024];
  double dir_x = 0;
  double dir_y = 0;
  int speed = 0;
  bool scanning = false;
  
  // Initialize GPIO and Steppers
  gpio_set_direction(IO_AR2, GPIO_DIR_INPUT);
  gpio_set_direction(IO_AR3, GPIO_DIR_INPUT); 
  stepper_init();  
  stepper_enable();  
  
  // Default start configuration (Enforced 15000 ticks for minimal error)
  stepper_set_speed(15000, 15000);  
  stepper_steps(0, 0);  

  printf("\n=== Robot Terminal Control Initialized ===\n");
  printf("Hardware Calibration: Turn Multiplier = 1255 Steps | Speed Baseline = 15000 Ticks\n");
  printf("Instructions: Enter values separated by spaces and press Enter.\n");
  printf("Format: [dir_x] [dir_y] [speed_ticks]\n");
  printf("Shortcut: Type '9 9 0' to trigger the 25-degree left wheel twitch.\n");
  printf("==========================================\n\n");

  // Setup nanosleep delay structures
  struct timespec ts_1ms;
  ts_1ms.tv_sec = 0;
  ts_1ms.tv_nsec = 1000000; // 1 millisecond

  struct timespec ts_settle;
  ts_settle.tv_sec = 0;
  ts_settle.tv_nsec = 250000000; // 250ms settling delay to tame high-speed momentum

  while (1) {    
    // --- 1. RECEIVE FROM EMBEDDED (ALGORITHM) ---
    int n = zmq_recv(receiver, buf, sizeof(buf) - 1, ZMQ_DONTWAIT);
    if (n >= 0) {
      buf[n] = '\0';
      cJSON *from_emb = cJSON_Parse(buf);
      if (!from_emb) {
            printf("[MOT] Bad JSON\n");
      } else {
        dir_x = cJSON_GetObjectItem(from_emb, "dir_x")->valuedouble;
        dir_y = cJSON_GetObjectItem(from_emb, "dir_y")->valuedouble;
        speed = cJSON_GetObjectItem(from_emb, "speed")->valueint;
        scanning = cJSON_IsTrue(cJSON_GetObjectItem(from_emb, "scanning"));

        fflush(stdout);
        cJSON_Delete(from_emb);
      }
    }

    printf("[MOT] From embedded — dir_x=%f  dir_y=%f  speed=%d  scanning=%d\n", dir_x, dir_y, speed, scanning);

    // TODO: Directional vector stuff, scanning boolean implementation, 

    // Test values
    // double dir_x = 0.0;
    // double dir_y = 0.0;
    int16_t base_speed = (int16_t) speed;

    printf("Enter Command (X Y Speed): ");
    fflush(stdout); 

    if (scanf("%lf %lf %hd", &dir_x, &dir_y, &base_speed) == 3) {
      
      // --- INTERCEPT TERMINAL SHORTCUT FIRST ---
      if (dir_x == 9.0 && dir_y == 9.0) {
        execute_25_degree_twitch();
        
        dir_x = 0;
        dir_y = 0;
        base_speed = 15000; // Force high-speed baseline
      }

      int16_t l_steps = 0;
      int16_t r_steps = 0;
      uint16_t l_speed = abs(base_speed);
      uint16_t r_speed = abs(base_speed);

      // Force fallback to high-speed 15000 ticks if input is unassigned/low
      if (l_speed < 15000 && (dir_x != 0 || dir_y != 0)) {
        printf(">> Speed optimized to 15000 ticks to avoid system errors.\n");
        l_speed = 15000; 
        r_speed = 15000;
      }

      // --- Calibrated Movement Logic (1255 with Settling Delay) ---
      
      // A. Pure In-Place Pivot Spin (X is 0, Y acts as the spin command)
      if (fabs(dir_x) < 0.01 && fabs(dir_y) > 0.01) {
        l_steps = (int16_t)(-dir_y * 1255);
        r_steps = (int16_t)(dir_y * 1255);
        printf(">> Spinning in place! Target Steps: L=%d, R=%d\n", l_steps, r_steps);
      }
      // B. Sequential 180-degree Turn AND Drive Forward
      else if (dir_x < -0.1 && fabs(dir_y) < 0.2) {
        printf(">> Step 1: Executing high-speed 180-degree pivot turn (1255 steps)...\n");
        
        stepper_set_speed(l_speed, r_speed);
        stepper_steps(1255, -1255);
        
        // Wait block for turn to finish entirely
        while (!stepper_steps_done()) {
          nanosleep(&ts_1ms, NULL); 
        }
        
        // --- HIGH SPEED MOMENTUM SETTLING DELAY ---
        printf(">> Pausing for 250ms to completely absorb spin inertia...\n");
        nanosleep(&ts_settle, NULL);
        
        // Drive forward cleanly
        printf(">> Step 2: Driving forward cleanly in the new direction...\n");
        double forward_drive = fabs(dir_x); 
        l_steps = (int16_t)((forward_drive - dir_y) * 1600);
        r_steps = (int16_t)((forward_drive + dir_y) * 1600);
      } 
      // C. Regular Driving & Banking Logic
      else {
        l_steps = (int16_t)((dir_x - dir_y) * 1600);
        r_steps = (int16_t)((dir_x + dir_y) * 1600);
      }

      // --- DIRECTION FIX ---
      if (base_speed < 0 && !(dir_x < -0.1 && fabs(dir_y) < 0.2)) {
        l_steps = -l_steps;
        r_steps = -r_steps;
        printf(">> Reverse detected! Inverting steps.\n");
      }

      if (dir_x == 0 && dir_y == 0 && base_speed == 0) {
        printf(">> Stopping robot.\n");
      }

      // Apply finalized settings to hardware
      stepper_set_speed(l_speed, r_speed);        
      stepper_steps(l_steps, r_steps);        
      
      while (!stepper_steps_done()) {
        nanosleep(&ts_1ms, NULL); 
      }
      printf(">> Movement Complete.\n\n");
      
    } else {
      printf("Invalid input format. Please use numbers: X Y Speed\n");
      while (getchar() != '\n'); 
    }
  }  

  stepper_destroy();  
  pynq_destroy();  
  return EXIT_SUCCESS;
}