#include <arm_shared_memory_system.h>
#include <libpynq.h>
#include <zmq.h>
#include <platform.h>
#include <stdint.h>
#include <stepper.h>
#include <stdlib.h>
#include <cjson/cJSON.h>
#include <math.h>
#include <time.h>     // Used for nanosleep()
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PIPE_IN_SEN  "ipc:///tmp/sen2emb.ipc"
#define PIPE_IN_ALG  "ipc:///tmp/alg2emb.ipc"
#define PIPE_OUT_ALG "ipc:///tmp/emb2alg.ipc"
#define PIPE_OUT_COM "ipc:///tmp/emb2com.ipc"

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

int main() {
    // Initialize GPIO and Steppers
    gpio_set_direction(IO_AR2, GPIO_DIR_INPUT);  
    gpio_set_direction(IO_AR3, GPIO_DIR_INPUT);  
    stepper_init();  
    stepper_enable();  

    void *ctx = zmq_ctx_new();

    // --- 4 sockets ---

    // Receive from sensors
    void *sub_sen = zmq_socket(ctx, ZMQ_SUB);
    zmq_connect(sub_sen, PIPE_IN_SEN);
    zmq_setsockopt(sub_sen, ZMQ_SUBSCRIBE, "", 0);

    // Send to algorithm
    void *pub_alg = zmq_socket(ctx, ZMQ_PUB);
    zmq_connect(pub_alg, PIPE_OUT_ALG);

    // Receive from algorithm
    void *sub_alg = zmq_socket(ctx, ZMQ_SUB);
    zmq_connect(sub_alg, PIPE_IN_ALG);
    zmq_setsockopt(sub_alg, ZMQ_SUBSCRIBE, "", 0);

    // Sends to communication
    void *pub_com = zmq_socket(ctx, ZMQ_PUB);
    zmq_connect(pub_com, PIPE_OUT_COM);

    // Small delay so sockets are ready before loop starts
    sleep_msec(200);

    char buf[1024];
    int  distance    = -1;
    int  temperature = -1;
    char color_front[32]  = "";
    char color_ground[32] = "";

    printf("[EMB] Ready\n"); fflush(stdout);

    while (1) {

        // --- 1. RECEIVE FROM SENSORS ---
        int n = zmq_recv(sub_sen, buf, sizeof(buf) - 1, ZMQ_DONTWAIT);
        if (n >= 0) {
            buf[n] = '\0';
            cJSON *from_sen = cJSON_Parse(buf);
            if (!from_sen) {
                printf("[EMB] Bad JSON from sensors\n");
            } else {
                distance    = cJSON_GetObjectItem(from_sen, "distance")->valueint;
                temperature = cJSON_GetObjectItem(from_sen, "temperature")->valueint;
                strncpy(color_front,  cJSON_GetObjectItem(from_sen, "color_front")->valuestring,  sizeof(color_front)  - 1);
                strncpy(color_ground, cJSON_GetObjectItem(from_sen, "color_ground")->valuestring, sizeof(color_ground) - 1);

                printf("[EMB] From sensors — distance=%d  temp=%d  front=%s  ground=%s\n",
                       distance, temperature, color_front, color_ground);
                fflush(stdout);
                cJSON_Delete(from_sen);
            }
        }

        // --- 2. SEND TO ALGORITHM ---
        if (distance >= 0 && temperature >= 0) {
            cJSON *to_alg = cJSON_CreateObject();
            cJSON_AddNumberToObject(to_alg, "distance",     distance);
            cJSON_AddBoolToObject  (to_alg, "ground_black", (strcmp(color_ground, "black") == 0));

            char *to_alg_str = cJSON_PrintUnformatted(to_alg);
            zmq_send(pub_alg, to_alg_str, strlen(to_alg_str), 0);
            printf("[EMB] To algorithm: %s\n", to_alg_str); fflush(stdout);

            free(to_alg_str);
            cJSON_Delete(to_alg);
        }

        // --- 3. RECEIVE FROM ALGORITHM ---
        int m = zmq_recv(sub_alg, buf, sizeof(buf) - 1, ZMQ_DONTWAIT);
        if (m >= 0) {
            buf[m] = '\0';
            cJSON *from_alg = cJSON_Parse(buf);
            if (!from_alg) {
                printf("[EMB] Bad JSON from algorithm\n");
            } else {
                // TESTING
                // speed (positive/negative/0), directional vector
                // int motor_left  = cJSON_GetObjectItem(from_alg, "motor_left")->valueint;
                // int motor_right = cJSON_GetObjectItem(from_alg, "motor_right")->valueint;

                printf("[EMB] From algorithm — motor_left=%d  motor_right=%d\n",
                       motor_left, motor_right);
                fflush(stdout);

                // Drive motors here
                // TODO

                double dir_x = 0.0;
                double dir_y = 0.0;
                int16_t base_speed = 0;

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

                // END TODO

                cJSON_Delete(from_alg);

                // --- 4. RELAY TO COMMUNICATIONS ---
                // positions, block locations, temperature, block colors
                cJSON *to_com = cJSON_CreateObject();
                // TESTING
                cJSON_AddNumberToObject(to_com, "temperature",  temperature);

                char *to_com_str = cJSON_PrintUnformatted(to_com);
                zmq_send(pub_com, to_com_str, strlen(to_com_str), 0);
                printf("[EMB] To comms: %s\n", to_com_str); fflush(stdout);

                free(to_com_str);
                cJSON_Delete(to_com);
            }
        }

        sleep_msec(10);
    }

    // Cleanup
    zmq_close(sub_sen);
    zmq_close(pub_alg);
    zmq_close(sub_alg);
    zmq_close(pub_com);
    zmq_ctx_destroy(ctx);
    return 0;
}
