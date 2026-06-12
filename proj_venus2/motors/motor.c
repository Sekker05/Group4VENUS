#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include <libpynq.h>
#include <stepper.h>
#include <arm_shared_memory_system.h>

#include <zmq.h>
#include <cjson/cJSON.h>

#define ZMQ_ENDPOINT     "ipc:///home/student/proj_venus/WallE_info.ipc"
#define STEPPER_SPEED    15000u   /* stepper period ticks — lower = faster */
#define STEPS_PER_FRAME  32       /* steps issued per loop iteration */
#define FRAME_US         10000    /* loop frame time in microseconds */

/* 1ms nanosleep helper */
static const struct timespec ts_1ms = { 0, 1000000 };

/* -----------------------------------------------------------------------
 * Parse WallE JSON: expects {"direction":[dx,dy], "speed":s, "turn_angle":t}
 * turn_angle is in radians; speed is normalised [-0.4, 0.4]
 * --------------------------------------------------------------------- */
static bool parse_walle_msg(const char *json_str,
                             float *speed, float *turn_angle)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return false;

    cJSON *spd   = cJSON_GetObjectItemCaseSensitive(root, "speed");
    cJSON *angle = cJSON_GetObjectItemCaseSensitive(root, "turn_angle");

    bool ok = false;
    if (cJSON_IsNumber(spd) && cJSON_IsNumber(angle)) {
        *speed      = (float)spd->valuedouble;
        *turn_angle = (float)angle->valuedouble;
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

int main(void)
{
    printf("[MOTOR] Initializing hardware...\n");
    fflush(stdout);

    pynq_init();

    gpio_set_direction(IO_AR2, GPIO_DIR_INPUT);
    gpio_set_direction(IO_AR3, GPIO_DIR_INPUT);

    stepper_init();
    stepper_enable();
    stepper_set_speed(STEPPER_SPEED, STEPPER_SPEED);
    stepper_steps(0, 0);

    printf("[MOTOR] Stepper ready. Connecting to ZMQ...\n");
    fflush(stdout);

    void *zmq_ctx  = zmq_ctx_new();
    void *sub_sock = zmq_socket(zmq_ctx, ZMQ_SUB);

    int conflate = 1;
    zmq_setsockopt(sub_sock, ZMQ_CONFLATE,  &conflate, sizeof(conflate));
    zmq_setsockopt(sub_sock, ZMQ_SUBSCRIBE, "",         0);

    if (zmq_connect(sub_sock, ZMQ_ENDPOINT) != 0) {
        fprintf(stderr, "[MOTOR] ZMQ connect failed: %s\n",
                zmq_strerror(zmq_errno()));
        stepper_disable();
        stepper_destroy();
        pynq_destroy();
        return 1;
    }

    printf("[MOTOR] Connected to %s. Running.\n", ZMQ_ENDPOINT);
    fflush(stdout);

    char  buf[2048];
    float cmd_speed = 0.0f;
    float cmd_angle = 0.0f;
    bool  have_cmd  = false;

    while (true) {
        int n = zmq_recv(sub_sock, buf, sizeof(buf) - 1, ZMQ_DONTWAIT);

        if (n >= 0) {
            buf[n] = '\0';
            float spd, ang;
            if (parse_walle_msg(buf, &spd, &ang)) {
                cmd_speed = spd;
                cmd_angle = ang;
                have_cmd  = true;
                printf("[MOTOR] cmd speed=%.3f angle=%.3f\n", spd, ang);
                fflush(stdout);
            }
        } else if (zmq_errno() != EAGAIN) {
            break;
        }

        if (!have_cmd) {
            nanosleep(&ts_1ms, NULL);
            continue;
        }

        /* Map speed + angle to left/right steps */
        float abs_speed = fabsf(cmd_speed);
        int16_t l_steps = 0;
        int16_t r_steps = 0;

        if (abs_speed > 0.01f) {
            float norm = cmd_speed / 0.4f;   /* normalise to [-1, 1] */
            if (norm >  1.0f) norm =  1.0f;
            if (norm < -1.0f) norm = -1.0f;

            float angle_norm = cmd_angle;     /* radians, small values */

            l_steps = (int16_t)((norm - angle_norm) * STEPS_PER_FRAME);
            r_steps = (int16_t)((norm + angle_norm) * STEPS_PER_FRAME);
        }

        stepper_set_speed(STEPPER_SPEED, STEPPER_SPEED);
        stepper_steps(l_steps, r_steps);

        /* Wait for steps to finish before next frame */
        while (!stepper_steps_done()) {
            nanosleep(&ts_1ms, NULL);
        }
    }

    stepper_disable();
    stepper_destroy();
    zmq_close(sub_sock);
    zmq_ctx_destroy(zmq_ctx);
    pynq_destroy();
    return 0;
}
