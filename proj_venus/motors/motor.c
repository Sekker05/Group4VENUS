// 1. Standard C Library Headers
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>  
#include <stdint.h>   
#include <math.h>
#include <string.h>
#include <unistd.h>

// 2. Libpynq Core Hardware Mode & Framework Dependencies
#include <libpynq.h>   
#include <stepper.h>   
#include <log.h>
#include <arm_shared_memory_system.h>


#include <zmq.h>
#include <cjson/cJSON.h> 

/* -----------------------------------------------------------------------
 * Compile-time tuning knobs
 * --------------------------------------------------------------------- */
#define STEPS_PER_TICK       32
#define WHEEL_BASE_STEPS     60.0f
#define STEPPER_PERIOD_SLOW  12000u   /* near-stop speed */
#define STEPPER_PERIOD_FAST   6200u   /* near-maximum speed */
#define MAX_SPEED_SIM        0.4f
#define ZMQ_ENDPOINT "ipc:///home/student/proj_venus/WallE_info.ipc"

/* -----------------------------------------------------------------------
 * Helper: map a normalised wheel velocity [-1, 1] to a stepper period and step count.
 * --------------------------------------------------------------------- */
static void velocity_to_stepper(float norm_v, int16_t *out_steps, uint16_t *out_period)
{
    float abs_v = fabsf(norm_v);

    if (abs_v < 0.02f) {
        *out_steps  = 0;
        *out_period = STEPPER_PERIOD_SLOW;
        return;
    }

    if (abs_v > 1.0f) abs_v = 1.0f;

    float t = abs_v;   
    uint16_t period = (uint16_t)(STEPPER_PERIOD_SLOW - t * (STEPPER_PERIOD_SLOW - STEPPER_PERIOD_FAST));

    if (period < STEPPER_PERIOD_FAST) period = STEPPER_PERIOD_FAST;

    *out_period = period;
    *out_steps  = (int16_t)((norm_v > 0.0f ? 1 : -1) * STEPS_PER_TICK);
}

/* -----------------------------------------------------------------------
 * Parse one WallE JSON message.
 * --------------------------------------------------------------------- */
static bool parse_walle_msg(const char *json_str, float *dx, float *dy, float *speed)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        return false;
    }

    bool ok = false;
    cJSON *dir   = cJSON_GetObjectItemCaseSensitive(root, "direction");
    cJSON *spd   = cJSON_GetObjectItemCaseSensitive(root, "speed");

    if (cJSON_IsArray(dir) && cJSON_GetArraySize(dir) == 2 && cJSON_IsNumber(spd)) {
        *dx    = (float)cJSON_GetArrayItem(dir, 0)->valuedouble;
        *dy    = (float)cJSON_GetArrayItem(dir, 1)->valuedouble;
        *speed = (float)spd->valuedouble;
        ok = true;
    }

    cJSON_Delete(root);
    return ok;
}

/* -----------------------------------------------------------------------
 * Core computation: direction vector + speed → left/right wheel velocities
 * --------------------------------------------------------------------- */
static void compute_wheel_velocities(
        float desired_dx, float desired_dy, float speed,
        float *current_dx, float *current_dy,
        float *v_left,  float *v_right)
{
    float mag = sqrtf(desired_dx*desired_dx + desired_dy*desired_dy);
    if (mag < 1e-6f) {
        *v_left = *v_right = 0.0f;
        return;
    }
    float ndx = desired_dx / mag;
    float ndy = desired_dy / mag;

    float cmag = sqrtf((*current_dx)*(*current_dx) + (*current_dy)*(*current_dy));
    if (cmag < 1e-6f) {
        *current_dx = ndx;
        *current_dy = ndy;
        *v_left = *v_right = speed / MAX_SPEED_SIM;
        return;
    }
    float cdx = *current_dx / cmag;
    float cdy = *current_dy / cmag;

    float raw_cross = cdx * ndy - cdy * ndx;   
    float cross     = -raw_cross;               

    float base = speed / MAX_SPEED_SIM;     
    if (base > 1.0f)  base = 1.0f;
    if (base < -1.0f) base = -1.0f;         

    float turn_gain = 1.0f;

    float left  = base - turn_gain * cross;
    float right = base + turn_gain * cross;

    float maxv = fmaxf(fabsf(left), fabsf(right));
    if (maxv > 1.0f) {
        left  /= maxv;
        right /= maxv;
        left  *= base;
        right *= base;
    }

    *v_left  = left;
    *v_right = right;

    float blend = 0.3f;    
    float new_dx = cdx + blend * (ndx - cdx);
    float new_dy = cdy + blend * (ndy - cdy);
    float nmag   = sqrtf(new_dx*new_dx + new_dy*new_dy);
    if (nmag > 1e-6f) {
        *current_dx = new_dx / nmag;
        *current_dy = new_dy / nmag;
    }
}

/* -----------------------------------------------------------------------
 * Main Application Execution
 * --------------------------------------------------------------------- */
int main(void)
{
    // Arm the hardware base platform layers
    pynq_init();
    stepper_init();
    stepper_enable();

    void *zmq_ctx  = zmq_ctx_new();
    void *sub_sock = zmq_socket(zmq_ctx, ZMQ_SUB);

    // Enforce conflation to maintain single-message processing constraints
    int conflate = 1;
    zmq_setsockopt(sub_sock, ZMQ_CONFLATE, &conflate, sizeof(conflate));
    zmq_setsockopt(sub_sock, ZMQ_SUBSCRIBE, "", 0);

    if (zmq_connect(sub_sock, ZMQ_ENDPOINT) != 0) {
        stepper_disable();
        stepper_destroy();
        pynq_destroy();
        return 1;
    }

    float cur_dx = 0.0f, cur_dy = -1.0f;  
    char buf[2048];        

    float cmd_dx = 0.0f, cmd_dy = -1.0f, cmd_speed = 0.0f;
    bool  have_cmd = false;

    while (true) {
        // Grab exactly ONE current packet per frame from Python
        int n = zmq_recv(sub_sock, buf, sizeof(buf) - 1, ZMQ_DONTWAIT);
        
        if (n >= 0) {
            buf[n] = '\0';
            float dx, dy, spd;
            if (parse_walle_msg(buf, &dx, &dy, &spd)) {
                cmd_dx    = dx;
                cmd_dy    = dy;
                cmd_speed = spd;
                have_cmd  = true;
            }
        } else if (zmq_errno() != EAGAIN) {
            break;
        }

        if (!have_cmd) {
            usleep(5000);   
            continue;
        }

        float v_left, v_right;
        compute_wheel_velocities(cmd_dx, cmd_dy, cmd_speed,
                                 &cur_dx, &cur_dy,
                                 &v_left, &v_right);

        int16_t  steps_left,  steps_right;
        uint16_t period_left, period_right;

        velocity_to_stepper(v_left,  &steps_left,  &period_left);
        velocity_to_stepper(v_right, &steps_right, &period_right);

        // Drive the physical stepper H-Bridge loops directly
        stepper_set_speed(period_left, period_right);
        stepper_steps(steps_left, steps_right);

        // Frame timing limit prevents redlining the CPU thread
        usleep(10000); 
    }

    // Clean up hardware resources cleanly upon loop exit
    stepper_disable();
    stepper_destroy();
    zmq_close(sub_sock);
    zmq_ctx_destroy(zmq_ctx);
    pynq_destroy();

    return 0;
}
