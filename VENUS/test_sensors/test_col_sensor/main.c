#include <libpynq.h>
#include <stdio.h>
#include <stdint.h>

#define PIN_S0  IO_AR4
#define PIN_S1  IO_AR5
#define PIN_S2  IO_AR6
#define PIN_S3  IO_AR7
#define PIN_OUT IO_AR8

#define MEASURE_MS 100   // gating window per channel (ms)

// ---------- Tunables for the classifier (tweak these by eye) ---------------
// If BLACK gets misread, raise BLACK_TOTAL.
// If WHITE gets misread as a color, lower WHITE_PCT.
#define WHITE_PCT     28   // each channel must exceed this % to be WHITE
// ---------------------------------------------------------------------------

typedef enum { COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_CLEAR } color_t;

// Select the photodiode filter via S2/S3
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
        case COLOR_CLEAR:
            gpio_set_level(PIN_S2, GPIO_LEVEL_HIGH);
            gpio_set_level(PIN_S3, GPIO_LEVEL_LOW);
            break;
        case COLOR_GREEN:
            gpio_set_level(PIN_S2, GPIO_LEVEL_HIGH);
            gpio_set_level(PIN_S3, GPIO_LEVEL_HIGH);
            break;
    }
    sleep_msec(2);  // settle time after switching the filter
}

// Count rising edges on OUT for MEASURE_MS, return raw count.
// Units don't matter — we'll classify by ratios.
static uint32_t read_frequency(void) {
    uint32_t count = 0;
    int prev = gpio_get_level(PIN_OUT);

    for (uint32_t ms = 0; ms < MEASURE_MS; ms++) {
        for (int i = 0; i < 2000; i++) {
            int now = gpio_get_level(PIN_OUT);
            if (prev == 0 && now == 1) count++;
            prev = now;
        }
    }
    return count;
}

// Classify by ratios — no per-color calibration needed.
static const char *classify(uint32_t r, uint32_t g, uint32_t b) {
    uint32_t total = r + g + b;
    if (total < 750) return "BLACK";
    if (total > 5000 && total < 6500) return "WHITE";

    if (r > 400) return "RED";
    if (b > 330) return "BLUE";
    return "GREEN";
}

int main(void) {
    pynq_init();

    gpio_set_direction(PIN_S0,  GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_S1,  GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_S2,  GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_S3,  GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_OUT, GPIO_DIR_INPUT);

    // 20% frequency scaling: S0=H, S1=L
    gpio_set_level(PIN_S0, GPIO_LEVEL_HIGH);
    gpio_set_level(PIN_S1, GPIO_LEVEL_LOW);

    while (1) {
        select_color(COLOR_RED);   uint32_t r = read_frequency();
        select_color(COLOR_GREEN); uint32_t g = read_frequency();
        select_color(COLOR_BLUE);  uint32_t b = read_frequency();

        // uint32_t total = r + g + b;
        // int rp = (100 * r) / total;
        // int gp = (40 * g) / total;
        // int bp = (193 * b) / total;

        const char *guess = classify(r, g, b);
        printf("R=%6u  G=%6u  B=%6u  ->  %s\n", r, g, b, guess);
        // printf("R=%d  G=%d  B=%d  ->  %s\n", rp, gp, bp, guess);

        sleep_msec(200);
    }

    pynq_destroy();
    return 0;
}