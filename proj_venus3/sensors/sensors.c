/*
 * sensors.c  — Venus Robot Sensor Hub
 * Merges:
 *   - VENUS2_2 sensor logic  (correct libpynq pins, VL53L0X IIC, dual TCS3200, NTC temp)
 *   - proj_venus2 IPC paths  (ipc://sensors.ipc to match WallE.py which we cannot change)
 *
 * IPC topology (WHO BINDS, WHO CONNECTS):
 *   PUB  sensors.ipc  — we BIND here; WallE.py connects to read sensor data
 *   SUB  WallE_info.ipc — we CONNECT here; WallE.py binds and publishes its state
 *
 * Runs at ~15 Hz (every 66 ms per loop).
 * Distance sensor polls every loop (~15 Hz).
 * Color / tape / temperature are slow (update every 30 loops ≈ 2 Hz) to avoid
 * blocking the distance read with the 6 ms gating window per channel.
 *
 * IPC files are cleaned up on startup and shutdown.
 *
 * Build: see sensors/Makefile (links against libpynq.a + libzmq + libcjson + libm)
 *
 * === Stand-alone test mode ===
 * Compile with -DSENSORS_TEST_MODE to print sensor values to stdout without
 * touching ZMQ so you can run it on its own:
 *   make test
 */

#include <libpynq.h>
#include <iic.h>
#include "vl53l0x.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#ifndef SENSORS_TEST_MODE
#include <zmq.h>
#endif

/* ─── Pin assignments (from proj_venus2 — physically verified) ────────────── */
/* TCS3200 colour sensor A — faces forward (cube classification)               */
#define PIN_S0    IO_AR4
#define PIN_S1    IO_AR5
#define PIN_S2    IO_AR6
#define PIN_S3    IO_AR7
#define PIN_OUTA  IO_AR8   /* OUT of front colour sensor */

/* TCS3200 colour sensor B — faces down (crater/tape detection)                */
#define PIN_OUTB  IO_AR9   /* OUT of bottom tape sensor, shares S0-S3 bus */

/* ─── NTC 10K thermistor (voltage divider on ADC channels) ───────────────── */
/* Circuit: VCC → R2 → ADC0 (V_out), ADC0 → NTC → GND; ADC1 = V_ref (VCC)  */
#define NTC_R2    9700.0   /* fixed resistor                 */
#define NTC_R0    1750.0   /* NTC resistance at T0    10k in the doc so shoul work    */
#define NTC_T0    (273.15 + 25.0)  /* reference temperature, K               */
#define NTC_BETA  4050.0           /* NTC Beta coefficient                    */

/* ─── VL53L0X ToF distance sensor ────────────────────────────────────────── */
/* Wired on IIC0 (AR_SCL / AR_SDA), default I²C address 0x29                 */

/* ─── TCS3200 gating window ──────────────────────────────────────────────── */
/* 3 ms per channel → 9 ms total for one R/G/B read.  Kept short so distance */
/* sensor stays responsive.                                                    */
#define MEASURE_MS 30

/* ─── Loop timing ─────────────────────────────────────────────────────────── */
#define LOOP_MS          66    /* ~15 Hz main loop period                      */
#define SLOW_EVERY       30    /* update slow sensors every 30 loops ≈ 2 Hz   */

/* ─── IPC endpoints (must match WallE.py — do NOT use /tmp/ prefix) ─────── */
/* WallE.py line:  pub.bind("ipc://sensors.ipc")  → we mirror with bind here */
/* WallE.py line:  sensors.connect("ipc://sensors.ipc") → we ARE that socket */
#ifndef SENSORS_TEST_MODE
#define IPC_PUB_SENSORS   "ipc://sensors.ipc"      /* we BIND  */
#define IPC_SUB_WALLE     "ipc://WallE_info.ipc"   /* we CONNECT */
#endif

/* ─── Helpers ─────────────────────────────────────────────────────────────── */

typedef enum { COLOR_RED, COLOR_GREEN, COLOR_BLUE } color_t;

static void select_color(color_t c)
{
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
    sleep_msec(2); /* filter settle time */
}

/*
 * Count rising edges on `out_pin` for MEASURE_MS milliseconds.
 * Busy-loops at ~2000 samples/ms — fast enough to catch TCS3200 pulses.
 */
static uint32_t read_frequency(int out_pin)
{
    uint32_t count = 0;
    int prev = gpio_get_level(out_pin);
    for (uint32_t ms = 0; ms < MEASURE_MS; ms++) {
        for (int i = 0; i < 2000; i++) {
            int now = gpio_get_level(out_pin);
            if (prev == 0 && now == 1) count++;
            prev = now;
            //usleep(1);
        }
    }
    return count;
}

/*
 * Classify the FRONT sensor (cube colour).
 * Thresholds calibrated for the 20 % scaling setting (S0=H, S1=L).
 * Adjust BLACK / WHITE thresholds if ambient light conditions differ.
 */
static const char *classify_cube(uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t total = r + g + b;
    if (total < 250)          return "BLACK";
    if (total > 1500)         return "WHITE";
    if (r > g*2 && r > b*2)  return "RED";
    if (b > r*2 && b > g)    return "BLUE";
    if (g > r && g > b)      return "GREEN";
    return "GREEN"; //gotta take our chanses
}

/*
 * Classify the BOTTOM sensor (tape / crater detection).
 * Returns "LINE" when the sensor sees dark tape (low counts).
 * Returns "SAFE" on bright floor.
 */
static const char *classify_tape(uint32_t r, uint32_t g, uint32_t b)
{
    /* If all channels are weak → dark tape detected */
    return (r > 500 && g > 500 && b > 500) ? "SAFE" : "LINE";
}

/*
 * Read NTC thermistor temperature in degrees Celsius.
 * Uses a voltage-divider: V_out (ADC0) across NTC; V_ref (ADC1) = supply.
 *   R_ntc = (V_ref - V_out) * R2 / V_out
 *   T_k   = 1 / (1/T0 + ln(R_ntc/R0)/BETA)
 */
static double read_temperature(void)
{
    double v_out = adc_read_channel(ADC1);
    double v_ref = adc_read_channel(ADC0);
    if (v_out < 1e-6) return -99.0; /* protect against div-by-zero */
    double r_ntc = (v_ref - v_out) * NTC_R2 / v_out;
    double inv_t = (1.0 / NTC_T0) + log(r_ntc / NTC_R0) / NTC_BETA;
    return (1.0 / inv_t) - 273.15;
}

/* ─── Main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("[SEN] Sensors starting up...\n"); fflush(stdout);

    /* --- Clean up any leftover IPC socket files from a previous crash ---
     * ZMQ IPC sockets leave a file on disk; if it is not cleaned the next
     * zmq_bind() on the same path fails with EADDRINUSE.
     * We do this BEFORE pynq_init so it happens even if hardware init fails. */
#ifndef SENSORS_TEST_MODE
    unlink("sensors.ipc");
    printf("[SEN] Cleared stale IPC: sensors.ipc\n"); fflush(stdout);
#endif

    /* --- Hardware init --- */
    pynq_init();
    adc_init();

    /* TCS3200 control lines */
    gpio_set_direction(PIN_S0,   GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_S1,   GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_S2,   GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_S3,   GPIO_DIR_OUTPUT);
    gpio_set_direction(PIN_OUTA, GPIO_DIR_INPUT);
    gpio_set_direction(PIN_OUTB, GPIO_DIR_INPUT);

    /* 20 % frequency scaling (S0=H, S1=L) — good balance of speed & signal */
    gpio_set_level(PIN_S0, GPIO_LEVEL_HIGH);
    gpio_set_level(PIN_S1, GPIO_LEVEL_LOW);

    /* VL53L0X over IIC0 */
    switchbox_set_pin(IO_AR_SCL, SWB_IIC0_SCL);
    switchbox_set_pin(IO_AR_SDA, SWB_IIC0_SDA);
    iic_init(IIC0);

    vl53x tof;
    int tof_ok = (tofPing(IIC0, 0x29) == 0) &&
                 (tofInit(&tof, IIC0, 0x29, 0) == 0);
    if (tof_ok) {
        printf("[SEN] VL53L0X OK\n");
    } else {
        printf("[SEN] WARNING: VL53L0X not found — distance will report 0\n");
    }
    fflush(stdout);

#ifndef SENSORS_TEST_MODE
    /* --- ZMQ setup ---
     * We BIND the publisher so WallE.py can connect to us.
     * We CONNECT the subscriber to WallE.py's bound socket for scanning flag. */
    void *zmq_ctx = zmq_ctx_new();

    void *pub = zmq_socket(zmq_ctx, ZMQ_PUB);
    if (zmq_bind(pub, IPC_PUB_SENSORS) != 0) {
        fprintf(stderr, "[SEN] zmq_bind(%s) failed: %s\n",
                IPC_PUB_SENSORS, zmq_strerror(zmq_errno()));
        adc_destroy();
        iic_destroy(IIC0);
        pynq_destroy();
        return 1;
    }
    printf("[SEN] Bound publisher to %s\n", IPC_PUB_SENSORS); fflush(stdout);

    void *sub = zmq_socket(zmq_ctx, ZMQ_SUB);
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);
    /* CONFLATE: only keep the newest WallE state packet, don't queue up stale ones */
    int conflate = 1;
    zmq_setsockopt(sub, ZMQ_CONFLATE, &conflate, sizeof(conflate));
    zmq_connect(sub, IPC_SUB_WALLE);
    printf("[SEN] Connected subscriber to %s\n", IPC_SUB_WALLE); fflush(stdout);

    /* Brief delay so ZMQ's slow-joiner problem doesn't drop first messages */
    sleep_msec(300);
#endif /* !SENSORS_TEST_MODE */

    /* --- Cached slow-sensor values (updated at ~2 Hz) --- */
    uint32_t slow_counter   = 0;
    int      cached_temp    = 25;
    int      cached_tape    = 0;
    char     cached_color_front[16] = "UNKNOWN";

    printf("[SEN] Entering main loop at ~15 Hz\n"); fflush(stdout);

    while (1) {
        /* ── 1. Distance sensor  (~15 Hz) ───────────────────────────────── */
        uint32_t dist_mm = tof_ok ? tofReadDistance(&tof) : 0;
        // Raw pin test - if very cooked 
//        int raw_transitions = 0;
//        int raw_prev = gpio_get_level(PIN_OUTA);
//        for (int i = 0; i < 100000; i++) {
//            int raw_now = gpio_get_level(PIN_OUTA);
//            if (raw_prev != raw_now) raw_transitions++;
//            raw_prev = raw_now;
       // }
       // printf("[RAW] PIN_OUTA transitions: %d\n", raw_transitions);
        /* ── 2. Slow sensors  (~2 Hz every 30 loops) ─────────────────────
         * Front colour sensor → cube colour classification
         * Bottom colour sensor → tape / crater detection
         * ADC temperature read                                              */
        if (++slow_counter >= SLOW_EVERY/2) { //make it more responsive to 0.5 hz
            slow_counter = 0;

            /* Front colour */
            select_color(COLOR_RED);
            uint32_t rA = read_frequency(PIN_OUTA);
            select_color(COLOR_GREEN);
            uint32_t gA = read_frequency(PIN_OUTA);
            select_color(COLOR_BLUE);
            uint32_t bA = read_frequency(PIN_OUTA);
            strncpy(cached_color_front, classify_cube(rA, gA, bA),
                    sizeof(cached_color_front) - 1);

            /* Bottom tape sensor (shares S2/S3 pins) */
            select_color(COLOR_RED);
            uint32_t rB = read_frequency(PIN_OUTB);
            select_color(COLOR_GREEN);
            uint32_t gB = read_frequency(PIN_OUTB);
            select_color(COLOR_BLUE);
            uint32_t bB = read_frequency(PIN_OUTB);
            cached_tape = (strcmp(classify_tape(rB, gB, bB), "LINE") == 0) ? 1 : 0;

            /* Temperature */
            cached_temp = (int)read_temperature();
            printf("[CAL] front R=%u G=%u B=%u\n", rA, gA, bA);
            printf("[CAL] tape  R=%u G=%u B=%u  temp=%.2f\n", rB, gB, bB, read_temperature());
            //fflush(stdout);


#ifdef SENSORS_TEST_MODE
            /* In test mode: print a rich human-readable table */
            printf("[SEN TEST] dist=%u mm | front R=%u G=%u B=%u → %s"
                   " | tape R=%u G=%u B=%u → %s"
                   " | temp=%d°C\n",
                   dist_mm,
                   rA, gA, bA, cached_color_front,
                   rB, gB, bB, cached_tape ? "TAPE" : "SAFE",
                   cached_temp);
            fflush(stdout);
#endif
        }

        /* ── 3. Publish JSON payload over ZMQ ────────────────────────────
         * Format matches what WallE.py expects:
         *   {"distance": <mm/10 = cm>, "tape": <bool>, "temperature": <°C>,
         *    "color_front": "<str>"}
         * NOTE: WallE.py uses 'distance' directly as cm (its algorithm divides
         * by 10 for long_dist), so we convert mm→cm here.                  */
#ifndef SENSORS_TEST_MODE
        char payload[256];
        int len = snprintf(payload, sizeof(payload),
            "{\"distance\":%u,"
            "\"temperature\":%d,"
            "\"color_front\":\"%s\","
            "\"tape\":%s}",
            dist_mm / 10,          /* mm → cm so WallE.py obstacle logic works */
            cached_temp,
            cached_color_front,
            cached_tape ? "true" : "false");

        zmq_send(pub, payload, len, ZMQ_DONTWAIT);

        /* Also check for scanning flag from WallE (non-blocking, discard) */
        char wbuf[512];
        int wn = zmq_recv(sub, wbuf, sizeof(wbuf) - 1, ZMQ_DONTWAIT);
        (void)wn; /* We read it so the buffer doesn't back up; not needed here */
#else
        /* In test mode just print distance every loop too */
        printf("[SEN TEST] dist=%u mm (%u cm)\n", dist_mm, dist_mm / 10);
        fflush(stdout);
#endif

        sleep_msec(LOOP_MS);
    }

    /* --- Cleanup (reached only if loop is broken externally) --- */
#ifndef SENSORS_TEST_MODE
    zmq_close(pub);
    zmq_close(sub);
    zmq_ctx_destroy(zmq_ctx);
    unlink("sensors.ipc");
    printf("[SEN] IPC cleaned up\n");
#endif
    iic_destroy(IIC0);
    adc_destroy();
    pynq_destroy();
    return 0;
}
