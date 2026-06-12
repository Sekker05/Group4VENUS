#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <zmq.h>
#include <cjson/cJSON.h>

/* --- Hardware Calibration Configuration --- */
#define MODE_NORMAL  0
#define MODE_LOCKOUT 1
#define MODE_SCAN    2

#define LOCKOUT_DURATION_SEC 2.0f

/* --- Low-Level Linux Sysfs GPIO Implementations --- */
static int gpio_export(int pin)
{
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) { perror("GPIO Export Error"); return -1; }
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%d", pin);
    
    ssize_t w = write(fd, buf, n);
    (void)w; /* Explicitly silence warn_unused_result */
    
    close(fd);
    usleep(50000); /* Allow hardware driver settle time */
    return 0;
}

static int gpio_direction(int pin, const char *dir)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) { perror("GPIO Direction Error"); return -1; }
    
    ssize_t w = write(fd, dir, strlen(dir));
    (void)w;
    
    close(fd);
    return 0;
}

static int gpio_read(int pin)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char c = '0';
    
    ssize_t r = read(fd, &c, 1);
    (void)r;
    
    close(fd);
    return (c == '1') ? 1 : 0;
}

/* --- Core Telemetry Handling Functions --- */
static bool parse_walle_info(const char *json, bool *scanning)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "scanning");
    bool parse_success = false;
    if (cJSON_IsBool(s)) {
        *scanning = cJSON_IsTrue(s);
        parse_success = true;
    }
    cJSON_Delete(root);
    return parse_success;
}

static const char* classify_cube(float r, float g, float b)
{
    if (r > 20000.0f && g > 20000.0f && b > 20000.0f) return "white";
    if (g > r && g > b && g > 25000.0f)                return "green";
    if (b > r && b > g && b > 20000.0f)                return "blue";
    return "black";
}

/* --- Main Production Routine --- */
int main(void)
{
    printf("=== WallE Sensor Hub: Initializing Production Hardware ===\n");

    /* 1. Setup Physical Hardware Pins */
    gpio_export(13); // Example Pin for Tape Tracker
    gpio_direction(13, "in");

    /* 2. Setup ZeroMQ Infrastructure */
    void *zmq_ctx = zmq_ctx_new();
    
    // Subscriber socket to listen to the Python Master Brain instructions
    void *sub_brain = zmq_socket(zmq_ctx, ZMQ_SUB);
    zmq_connect(sub_brain, "ipc://WallE_info.ipc");
    zmq_setsockopt(sub_brain, ZMQ_SUBSCRIBE, "", 0);

    // Publisher socket to broadcast sensor metrics back out to the robot stack
    void *pub_metrics = zmq_socket(zmq_ctx, ZMQ_PUB);
    zmq_bind(pub_metrics, "ipc://sensors.ipc");

    /* 3. Setup State Machine tracking variables */
    int current_mode = MODE_NORMAL;
    bool last_scanning_instruction = false;
    struct timespec lockout_start = {0, 0};

    printf("[SUCCESS] Hardware and communication structures online. Entering processing loop...\n");

    while (1) {
        // High frequency loop execution matching standard telemetry rates (approx 12.5Hz)
        usleep(80000); 

        /* --- Phase A: Process Incoming Brain Commands via ZMQ (Non-blocking) --- */
        zmq_msg_t msg;
        zmq_msg_init(&msg);
        int bytes_rx = zmq_msg_recv(&msg, sub_brain, ZMQ_DONTWAIT);
        
        if (bytes_rx > 0) {
            char *json_string = malloc(bytes_rx + 1);
            memcpy(json_string, zmq_msg_data(&msg), bytes_rx);
            json_string[bytes_rx] = '\0';

            bool brain_wants_scanning = false;
            if (parse_walle_info(json_string, &brain_wants_scanning)) {
                // If the scanning attribute state has suddenly changed, toggle our state machine
                if (brain_wants_scanning != last_scanning_instruction) {
                    current_mode = MODE_LOCKOUT;
                    clock_gettime(CLOCK_MONOTONIC, &lockout_start);
                    last_scanning_instruction = brain_wants_scanning;
                    printf("[STATE] Instruction shift caught. Entering Sensor Lockout Mode.\n");
                }
            }
            free(json_string);
        }
        zmq_msg_close(&msg);

        /* --- Phase B: Execute Core System State Machine --- */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        // Read physical sensor variables
        int tape_raw = gpio_read(13);
        bool tape_detected = (tape_raw == 1);

        switch (current_mode) {
            case MODE_LOCKOUT: {
                float elapsed = (now.tv_sec - lockout_start.tv_sec) + 
                                (now.tv_nsec - lockout_start.tv_nsec) / 1000000000.0f;
                
                if (elapsed >= LOCKOUT_DURATION_SEC) {
                    current_mode = last_scanning_instruction ? MODE_SCAN : MODE_NORMAL;
                    printf("[STATE] Lockout expired. Progressing state machine.\n");
                }
                break;
            }

            case MODE_NORMAL: {
                // Broadcast basic telemetry mapping back to the robot core
                char telemetry_payload[128];
                snprintf(telemetry_payload, sizeof(telemetry_payload),
                         "{\"status\":\"NORMAL\",\"tape\":%s}", 
                         tape_detected ? "true" : "false");
                
                zmq_send(pub_metrics, telemetry_payload, strlen(telemetry_payload), 0);
                break;
            }

            case MODE_SCAN: {
                // Read physical analog color frequencies (Dummy mock hardware endpoints shown here)
                float r = 3000.0f;
                float g = 40000.0f;
                float b = 4000.0f;
                
                const char *detected_color = classify_cube(r, g, b);
                
                char telemetry_payload[256];
                snprintf(telemetry_payload, sizeof(telemetry_payload),
                         "{\"status\":\"SCAN\",\"color\":\"%s\",\"temp\":25.5}", 
                         detected_color);
                
                zmq_send(pub_metrics, telemetry_payload, strlen(telemetry_payload), 0);
                
                // Drop back to normal operation immediately following single-shot evaluation
                current_mode = MODE_NORMAL; 
                break;
            }
        }
    }

    // Unreachable loop termination cleanup signatures
    zmq_close(sub_brain);
    zmq_close(pub_metrics);
    zmq_ctx_destroy(zmq_ctx);
    return 0;
}