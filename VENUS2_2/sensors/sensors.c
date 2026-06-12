#include <libpynq.h>
#include <iic.h>
#include "vl53l0x.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <zmq.h>

// ─── Color sensor A (OUT = AR8) ───────────────────────────────────────────────
#define PIN_S0   IO_AR4
#define PIN_S1   IO_AR5
#define PIN_S2   IO_AR6
#define PIN_S3   IO_AR7
#define PIN_OUTA IO_AR8
#define PIN_OUTB IO_AR9

// ─── Temperature sensor ───────────────────────────────────────────────────────
#define R2   9.7 *1000
#define R0   8* 1000
#define T0   273.15 + 25
#define BETA 4050

#define MEASURE_MS 3

// ─── IPC endpoint — must match PIPE_IN_SEN in embedded.c ─────────────────────
#define PIPE_OUT_EMB "ipc:///tmp/sen2emb.ipc"
#define PIPE_IN_ALG "ipc:///tmp/alg2emb.ipc"

// ─── Color helpers ────────────────────────────────────────────────────────────

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

static const char *classify(uint32_t r, uint32_t g, uint32_t b) {
    if (g < 300)                            return "BLACK";
    if (g > 1500 && r > 1500 && b > 1500)  return "WHITE";
    if (r > 800)                            return "RED";
    if (b > 800)                            return "BLUE";
    return "GREEN";
}

static const char *classify_line(uint32_t r, uint32_t g, uint32_t b) {
    return (r > 500 && g > 500 && b > 500) ? "SAFE" : "LINE";
}

// ─── Temperature helper ───────────────────────────────────────────────────────
static double read_temperature(void) {
    double v_out = adc_read_channel(ADC0);
    double v_ref = adc_read_channel(ADC1);
    double r_t   = (v_ref - v_out) * R2 / v_out;
    double t_k   = (1 / T0) + 1 / (log(r_t / R0) / BETA);
    return t_k - 273.15;
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(void) {
    pynq_init();
    adc_init();

    void *zmq_ctx = zmq_ctx_new();
    void *pub     = zmq_socket(zmq_ctx, ZMQ_PUB);
    if (zmq_bind(pub, PIPE_OUT_EMB) != 0) {
        fprintf(stderr, "zmq_bind(%s) failed: %s\n",
                PIPE_OUT_EMB, zmq_strerror(zmq_errno()));
        return 1;
    }
    
    void *sub = zmq_socket(zmq_ctx, ZMQ_SUB);
    zmq_connect(sub, PIPE_IN_ALG);
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);
//    int conflate = 1;
//    zmq_setsockopt(sub, ZMQ_CONFLATE, &conflate, sizeof(conflate));
    
    sleep_msec(300);

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

        uint32_t slow_counter = 0;

    int cached_temp = 25;
    int cached_tape = 0;
    char cached_color_front[16] = "UNKNOWN";

    while (1) {

        // ── Distance (60 Hz) ─────────────────────────────────────
        uint32_t dist_mm = tof_ok ? tofReadDistance(&tof) : 0;

        // ── Slow sensors (2 Hz) ──────────────────────────────────
        if (++slow_counter >= 30) {      // 30 * 16ms ≈ 480ms
            slow_counter = 0;

            // Front color
            select_color(COLOR_RED);
            uint32_t rA = read_frequency(PIN_OUTA);

            select_color(COLOR_GREEN);
            uint32_t gA = read_frequency(PIN_OUTA);

            select_color(COLOR_BLUE);
            uint32_t bA = read_frequency(PIN_OUTA);

            strcpy(cached_color_front,
                   classify(rA, gA, bA));

            // Tape sensor
            select_color(COLOR_RED);
            uint32_t rB = read_frequency(PIN_OUTB);

            select_color(COLOR_GREEN);
            uint32_t gB = read_frequency(PIN_OUTB);

            select_color(COLOR_BLUE);
            uint32_t bB = read_frequency(PIN_OUTB);

            cached_tape =
                (strcmp(classify_line(rB, gB, bB), "LINE") == 0);

            cached_temp = (int)read_temperature();
        }

        // ── Publish latest state ────────────────────────────────
        char payload[256];

        int len = snprintf(
            payload,
            sizeof(payload),
            "{\"distance\":%u,"
            "\"temperature\":%d,"
            "\"color_front\":\"%s\","
            "\"tape\":%s}",
            dist_mm,
            cached_temp,
            cached_color_front,
            cached_tape ? "true" : "false"
        );

        zmq_send(pub, payload, len, ZMQ_DONTWAIT);

        sleep_msec(16);
    }

    zmq_close(pub);
    zmq_ctx_destroy(zmq_ctx);
    iic_destroy(IIC0);
    adc_destroy();
    pynq_destroy();
    return 0;
}
































