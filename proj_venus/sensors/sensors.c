#include <libpynq.h>
#include <iic.h>
#include <switchbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <zmq.h>
#include <cjson/cJSON.h>
#include "vl53l0x.h"

#define PIN_S0   IO_AR4
#define PIN_S1   IO_AR5
#define PIN_S2   IO_AR6
#define PIN_S3   IO_AR7
#define PIN_OUTA IO_AR8
#define PIN_OUTB IO_AR9

#define MEASURE_MS 3
#define LOCKOUT_DURATION_MS 2000

typedef enum { MODE_NORMAL, MODE_LOCKOUT, MODE_SCAN } sensor_mode_t;
typedef enum { COLOR_RED, COLOR_GREEN, COLOR_BLUE } color_t;

static void select_color(color_t c) {
    switch (c) {
        case COLOR_RED:
            gpio_set_level(PIN_S2, GPIO_LEVEL_LOW);
            gpio_set_level(PIN_S3, GPIO_LEVEL_LOW);
            break;
        case COLOR_BLUE:
            gpio_set_level(PIN_S2, GPIO_LEVEL_LOW);
            gpio_set_level(PIN_S3, GPIO_LEVEL_HIGH);
            break;
        case COLOR_GREEN:
            gpio_set_level(PIN_S2, GPIO_LEVEL_HIGH);
            gpio_set_level(PIN_S3, GPIO_LEVEL_HIGH);
            break;
    }
    sleep_msec(2);
}

static uint32_t read_frequency(int out_pin) {
    uint32_t count = 0;
    int prev = gpio_get_level(out_pin);
    for (uint32_t ms = 0; ms < MEASURE_MS; ms++) {
        for (int i = 0; i < 2000; i++) {
            int now = gpio_get_level(out_pin);
            if (prev == 0 && now == 1) count++;
            prev = now;
        }
    }
    return count;
}

static const char *classify_cube(uint32_t r, uint32_t g, uint32_t b) {
    if (g < 300)                           return "BLACK";
    if (g > 1500 && r > 1500 && b > 1500) return "WHITE";
    if (r > 800)                           return "RED";
    if (b > 800)                           return "BLUE";
    return "GREEN";
}

static bool parse_scanning(const char *json, bool *scanning) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "scanning");
    bool ok = false;
    if (cJSON_IsBool(s)) { *scanning = cJSON_IsTrue(s); ok = true; }
    cJSON_Delete(root);
    return ok;
}

int main(void) {
    printf("=== WallE Sensor Hub: Initializing Production Hardware ===\n");

    pynq_init();
    adc_init();

    gpio_set_direction(PIN_S0,   GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_S1,   GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_S2,   GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_S3,   GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_OUTA, GPIO_DIR_INPUT);
    gpio_set_direction(PIN_OUTB, GPIO_DIR_INPUT);

    gpio_set_level(PIN_S0, GPIO_LEVEL_HIGH);
    gpio_set_level(PIN_S1, GPIO_LEVEL_LOW);

    switchbox_set_pin(IO_AR_SCL, SWB_IIC0_SCL);
    switchbox_set_pin(IO_AR_SDA, SWB_IIC0_SDA);
    iic_init(IIC0);

    vl53x tof;
    int tof_ok = (tofPing(IIC0, 0x29) == 0) &&
                 (tofInit(&tof, IIC0, 0x29, 0) == 0);
    if (!tof_ok) printf("[WARN] Distance sensor not found!\n");

    void *zmq_ctx = zmq_ctx_new();

    void *sub_brain = zmq_socket(zmq_ctx, ZMQ_SUB);
    zmq_connect(sub_brain, "ipc:///home/student/proj_venus/WallE_info.ipc");
    zmq_setsockopt(sub_brain, ZMQ_SUBSCRIBE, "", 0);

    void *pub_metrics = zmq_socket(zmq_ctx, ZMQ_PUB);
    zmq_bind(pub_metrics, "ipc:///home/student/proj_venus/sensors.ipc");

    sensor_mode_t current_mode = MODE_NORMAL;
    bool last_scanning = false;
    uint32_t lockout_start_ms = 0;
    uint32_t slow_counter = 0;
    int cached_tape = 0;
    char cached_color[16] = "UNKNOWN";

    printf("[SUCCESS] Hardware online. Entering processing loop...\n");

    while (1) {
        sleep_msec(16);

        // Read brain commands
        zmq_msg_t msg;
        zmq_msg_init(&msg);
        int bytes = zmq_msg_recv(&msg, sub_brain, ZMQ_DONTWAIT);
        if (bytes > 0) {
            char *json = malloc(bytes + 1);
            memcpy(json, zmq_msg_data(&msg), bytes);
            json[bytes] = '\0';
            bool wants_scanning = false;
            if (parse_scanning(json, &wants_scanning)) {
                if (wants_scanning != last_scanning) {
                    current_mode = MODE_LOCKOUT;
                    lockout_start_ms = 0;
                    last_scanning = wants_scanning;
                    printf("[STATE] Instruction shift. Entering Lockout.\n");
                }
            }
            free(json);
        }
        zmq_msg_close(&msg);

        // Slow sensors at ~2Hz
        if (++slow_counter >= 30) {
            slow_counter = 0;

            select_color(COLOR_RED);
            uint32_t rA = read_frequency(PIN_OUTA);
            select_color(COLOR_GREEN);
            uint32_t gA = read_frequency(PIN_OUTA);
            select_color(COLOR_BLUE);
            uint32_t bA = read_frequency(PIN_OUTA);
            strcpy(cached_color, classify_cube(rA, gA, bA));

            select_color(COLOR_RED);
            uint32_t rB = read_frequency(PIN_OUTB);
            select_color(COLOR_GREEN);
            uint32_t gB = read_frequency(PIN_OUTB);
            select_color(COLOR_BLUE);
            uint32_t bB = read_frequency(PIN_OUTB);
            cached_tape = (rB > 500 && gB > 500 && bB > 500) ? 0 : 1;
        }

        // Distance at 60Hz
        uint32_t dist_mm = tof_ok ? tofReadDistance(&tof) : 9999;
        uint32_t dist_cm = dist_mm / 10;

        // State machine
        switch (current_mode) {
            case MODE_LOCKOUT:
                lockout_start_ms += 16;
                if (lockout_start_ms >= LOCKOUT_DURATION_MS) {
                    current_mode = last_scanning ? MODE_SCAN : MODE_NORMAL;
                    printf("[STATE] Lockout expired.\n");
                }
                break;

            case MODE_NORMAL: {
                char payload[128];
                int len = snprintf(payload, sizeof(payload),
                    "{\"status\":\"NORMAL\",\"distance\":%u,\"tape\":%s}",
                    dist_cm, cached_tape ? "true" : "false");
                zmq_send(pub_metrics, payload, len, ZMQ_DONTWAIT);
                break;
            }

            case MODE_SCAN: {
                char payload[256];
                int len = snprintf(payload, sizeof(payload),
                    "{\"status\":\"SCAN\",\"color\":\"%s\",\"distance\":%u,\"tape\":%s}",
                    cached_color, dist_cm, cached_tape ? "true" : "false");
                zmq_send(pub_metrics, payload, len, ZMQ_DONTWAIT);
                current_mode = MODE_NORMAL;
                break;
            }
        }
    }

    zmq_close(sub_brain);
    zmq_close(pub_metrics);
    zmq_ctx_destroy(zmq_ctx);
    iic_destroy(IIC0);
    adc_destroy();
    pynq_destroy();
    return 0;
}