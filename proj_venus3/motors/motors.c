#define _DEFAULT_SOURCE 

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>

#include <libpynq.h>
#include <stepper.h>

#ifndef MOTORS_TEST_MODE
#include <zmq.h>
#include <cjson/cJSON.h>
#endif

/* ─── Tuning constants ────────────────────────────────────────────────────── */
#define STEPS_PER_TICK          50
#define STEPPER_PERIOD_SLOW     35000u
#define STEPPER_PERIOD_FAST     30000u
#define MAX_SPEED_SIM            0.4f
#define VEL_DEADBAND             0.02f
#define FRAME_US                10000

#ifndef MOTORS_TEST_MODE
#define ZMQ_ENDPOINT  "ipc://WallE_info.ipc"
#endif

/* ─── Scanning-twitch constants ──────────────────────────────────────────── */
#define TWITCH_SPEED    30000u
#define TWITCH_STEPS      300

/* ─── Signal handler ─────────────────────────────────────────────────────── */
static void handle_signal(int sig) {
    (void)sig;
    stepper_disable();
    stepper_destroy();
    exit(0);
}

/* ─── Helpers ─────────────────────────────────────────────────────────────── */
static void velocity_to_stepper(float norm_v, int16_t *out_steps, uint16_t *out_period) {
    float abs_v = fabsf(norm_v);
    if (abs_v < VEL_DEADBAND) {
        *out_steps  = 0;
        *out_period = STEPPER_PERIOD_SLOW;
        return;
    }
    if (abs_v > 1.0f) abs_v = 1.0f;
    uint16_t period = (uint16_t)(STEPPER_PERIOD_SLOW - abs_v * (STEPPER_PERIOD_SLOW - STEPPER_PERIOD_FAST));
    if (period < STEPPER_PERIOD_FAST) period = STEPPER_PERIOD_FAST;
    *out_period = period;
    *out_steps  = (int16_t)((norm_v > 0.0f ? 1 : -1) * STEPS_PER_TICK);
}

static void compute_wheel_velocities(float desired_dx, float desired_dy, float speed,
                                     float current_dx, float current_dy,
                                     float *v_left, float *v_right) {
    float mag = sqrtf(desired_dx * desired_dx + desired_dy * desired_dy);
    if (mag < 1e-6f) {
        *v_left = *v_right = 0.0f;
        return;
    }
    float ndx = desired_dx / mag;
    float ndy = desired_dy / mag;

    float cmag = sqrtf(current_dx * current_dx + current_dy * current_dy);
    float cdx = current_dx / (cmag < 1e-6f ? 1.0f : cmag);
    float cdy = current_dy / (cmag < 1e-6f ? 1.0f : cmag);

    float dot   = cdx * ndx + cdy * ndy;
    float cross = cdx * ndy - cdy * ndx;

    float base = speed / MAX_SPEED_SIM;
    if (base >  1.0f) base =  1.0f;
    if (base < -1.0f) base = -1.0f;

    if (dot < 0.98f) {
        float turn_intensity = 0.5f;
        if (cross > 0.0f) {
            *v_left  =  turn_intensity;
            *v_right = -turn_intensity;
        } else {
            *v_left  = -turn_intensity;
            *v_right =  turn_intensity;
        }
    } else {
        if (fabsf(base) < VEL_DEADBAND) {
            *v_left = 0.0f; *v_right = 0.0f;
        } else {
            *v_left = base; *v_right = base;
        }
    }
}

#ifndef MOTORS_TEST_MODE
static void execute_scanning_twitch(void) {
    struct timespec ts_1ms = {0, 1000000L};
    struct timespec ts_3s  = {3, 0L};
    stepper_set_speed(TWITCH_SPEED, TWITCH_SPEED);
    stepper_steps(0, TWITCH_STEPS);
    while (!stepper_steps_done()) nanosleep(&ts_1ms, NULL);
    nanosleep(&ts_3s, NULL);
    stepper_steps(0, -TWITCH_STEPS);
    while (!stepper_steps_done()) nanosleep(&ts_1ms, NULL);
}

static bool parse_walle_msg(const char *json_str, float *dx, float *dy, float *speed, bool *scanning) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return false;
    cJSON *dir  = cJSON_GetObjectItemCaseSensitive(root, "direction");
    cJSON *spd  = cJSON_GetObjectItemCaseSensitive(root, "speed");
    cJSON *scan = cJSON_GetObjectItemCaseSensitive(root, "scanning");
    bool ok = false;
    if (cJSON_IsArray(dir) && cJSON_GetArraySize(dir) == 2 && cJSON_IsNumber(spd)) {
        *dx = (float)cJSON_GetArrayItem(dir, 0)->valuedouble;
        *dy = (float)cJSON_GetArrayItem(dir, 1)->valuedouble;
        *speed = (float)spd->valuedouble;
        *scanning = cJSON_IsBool(scan) ? cJSON_IsTrue(scan) : false;
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}
#endif

/* ─── Main ────────────────────────────────────────────────────────────────── */
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    stepper_init();
#ifdef MOTORS_TEST_MODE
    pynq_init();
#endif
    stepper_enable();
    signal(SIGTERM, handle_signal);
    signal(SIGINT,  handle_signal);

#ifndef MOTORS_TEST_MODE
    void *zmq_ctx  = zmq_ctx_new();
    void *sub_sock = zmq_socket(zmq_ctx, ZMQ_SUB);
    zmq_setsockopt(sub_sock, ZMQ_SUBSCRIBE, "", 0);
    zmq_connect(sub_sock, ZMQ_ENDPOINT);

    float cur_dx = 0.0f, cur_dy = -1.0f; // Internal orientation state
    float cmd_dx = 0.0f, cmd_dy = -1.0f, cmd_speed = 0.0f;
    bool have_cmd = false, scanning = false, scan_last = false;
    char buf[2048];

    while (1) {
        // 1. Drain ZMQ queue to get the LATEST command
        int n;
        while ((n = zmq_recv(sub_sock, buf, sizeof(buf) - 1, ZMQ_DONTWAIT)) > 0) {
            buf[n] = '\0';
            float tx, ty, ts; bool sc;
            if (parse_walle_msg(buf, &tx, &ty, &ts, &sc)) {
                cmd_dx = tx; cmd_dy = ty; cmd_speed = ts;
                scanning = sc; have_cmd = true;
            }
        }

        if (!have_cmd) { usleep(FRAME_US); continue; }

        // 2. Handle Twitch
        if (scanning != scan_last) {
            scan_last = scanning;
            if (scanning) execute_scanning_twitch();
        }

        // 3. Calculate wheel speeds
        float vl, vr;
        compute_wheel_velocities(cmd_dx, cmd_dy, cmd_speed, cur_dx, cur_dy, &vl, &vr);

        int16_t sl, sr; uint16_t pl, pr;
        velocity_to_stepper(vl, &sl, &pl);
        velocity_to_stepper(vr, &sr, &pr);

        // 4. Execute and Update Kinematics
        if (stepper_steps_done()) {
            if (sl != 0 || sr != 0) {
                stepper_enable();
                stepper_set_speed(pl, pr);
                stepper_steps(sl, sr);

                // Update internal orientation based on movement
                float d_theta = 0.0012f * (float)(sl - sr);
                float cos_t = cosf(d_theta);
                float sin_t = sinf(d_theta);
                float next_dx = cur_dx * cos_t - cur_dy * sin_t;
                float next_dy = cur_dx * sin_t + cur_dy * cos_t;
                cur_dx = next_dx;
                cur_dy = next_dy;
            } else {
                stepper_disable();
            }
        }
        usleep(FRAME_US);
    }
#else
    /* Test Mode Logic (Omitted for brevity, keep your existing working test block here) */
#endif

    stepper_destroy();
    return 0;
}