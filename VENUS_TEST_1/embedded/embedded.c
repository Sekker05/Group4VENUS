#include <libpynq.h>
#include <zmq.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PIPE_IN_SEN  "ipc:///tmp/sen2emb.ipc"
#define PIPE_IN_ALG  "ipc:///tmp/alg2emb.ipc"
#define PIPE_OUT_ALG "ipc:///tmp/emb2alg.ipc"
#define PIPE_OUT_COM "ipc:///tmp/emb2com.ipc"

int main() {
    void *ctx = zmq_ctx_new();

    // --- 4 sockets, set up ONCE before the loop ---

    // Receives from sensors
    void *sub_sen = zmq_socket(ctx, ZMQ_SUB);
    zmq_connect(sub_sen, PIPE_IN_SEN);
    zmq_setsockopt(sub_sen, ZMQ_SUBSCRIBE, "", 0);

    // Sends processed data to algorithm
    void *pub_alg = zmq_socket(ctx, ZMQ_PUB);
    zmq_connect(pub_alg, PIPE_OUT_ALG);

    // Receives commands back from algorithm
    void *sub_alg = zmq_socket(ctx, ZMQ_SUB);
    zmq_connect(sub_alg, PIPE_IN_ALG);
    zmq_setsockopt(sub_alg, ZMQ_SUBSCRIBE, "", 0);

    // Sends to communications module
    void *pub_com = zmq_socket(ctx, ZMQ_PUB);
    zmq_connect(pub_com, PIPE_OUT_COM);

    // Small delay so sockets are ready before loop starts
    usleep(200000);

    // --- variables declared here so they're accessible throughout the loop ---
    char buf[1024];
    int  distance    = -1;
    int  temperature = -1;
    char color_front[32]  = "";
    char color_ground[32] = "";

    printf("[EMB] Ready\n"); fflush(stdout);

    while (1) {

        // ── 1. RECEIVE FROM SENSORS ──────────────────────────────────────
        int n = zmq_recv(sub_sen, buf, sizeof(buf) - 1, ZMQ_DONTWAIT);
        if (n >= 0) {
            buf[n] = '\0';
            cJSON *msg = cJSON_Parse(buf);
            if (!msg) {
                printf("[EMB] Bad JSON from sensors\n");
            } else {
                distance    = cJSON_GetObjectItem(msg, "distance")->valueint;
                temperature = cJSON_GetObjectItem(msg, "temperature")->valueint;
                strncpy(color_front,  cJSON_GetObjectItem(msg, "color_front")->valuestring,  sizeof(color_front)  - 1);
                strncpy(color_ground, cJSON_GetObjectItem(msg, "color_ground")->valuestring, sizeof(color_ground) - 1);

                printf("[EMB] From sensors — distance=%d  temp=%d  front=%s  ground=%s\n",
                       distance, temperature, color_front, color_ground);
                fflush(stdout);
                cJSON_Delete(msg);
            }
        }

        // ── 2. SEND TO ALGORITHM ─────────────────────────────────────────
        if (distance >= 0 && temperature >= 0) {
            cJSON *to_alg = cJSON_CreateObject();
            cJSON_AddNumberToObject(to_alg, "distance",     distance);
            cJSON_AddNumberToObject(to_alg, "temperature",  temperature);
            cJSON_AddBoolToObject  (to_alg, "ground_black", (strcmp(color_ground, "black") == 0));

            char *to_alg_str = cJSON_PrintUnformatted(to_alg);
            zmq_send(pub_alg, to_alg_str, strlen(to_alg_str), 0);
            printf("[EMB] To algorithm: %s\n", to_alg_str); fflush(stdout);

            free(to_alg_str);
            cJSON_Delete(to_alg);
        }

        // ── 3. RECEIVE FROM ALGORITHM ────────────────────────────────────
        int m = zmq_recv(sub_alg, buf, sizeof(buf) - 1, ZMQ_DONTWAIT);
        if (m >= 0) {
            buf[m] = '\0';
            cJSON *cmd = cJSON_Parse(buf);
            if (!cmd) {
                printf("[EMB] Bad JSON from algorithm\n");
            } else {
                int motor_left  = cJSON_GetObjectItem(cmd, "motor_left")->valueint;
                int motor_right = cJSON_GetObjectItem(cmd, "motor_right")->valueint;

                printf("[EMB] From algorithm — motor_left=%d  motor_right=%d\n",
                       motor_left, motor_right);
                fflush(stdout);

                // Drive motors here using libpynq...

                cJSON_Delete(cmd);

                // ── 4. RELAY TO COMMUNICATIONS ───────────────────────────
                cJSON *to_com = cJSON_CreateObject();
                cJSON_AddNumberToObject(to_com, "motor_left",   motor_left);
                cJSON_AddNumberToObject(to_com, "motor_right",  motor_right);
                cJSON_AddNumberToObject(to_com, "distance",     distance);
                cJSON_AddNumberToObject(to_com, "temperature",  temperature);

                char *to_com_str = cJSON_PrintUnformatted(to_com);
                zmq_send(pub_com, to_com_str, strlen(to_com_str), 0);
                printf("[EMB] To comms: %s\n", to_com_str); fflush(stdout);

                free(to_com_str);
                cJSON_Delete(to_com);
            }
        }

        usleep(100000); // 100ms
    }

    // Cleanup
    zmq_close(sub_sen);
    zmq_close(pub_alg);
    zmq_close(sub_alg);
    zmq_close(pub_com);
    zmq_ctx_destroy(ctx);
    return 0;
}