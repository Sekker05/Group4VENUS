#include <libpynq.h>
#include <zmq.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PIPE_IN_SEN  "ipc:///tmp/sen2emb.ipc"
#define PIPE_IN_ALG  "ipc:///tmp/alg2emb.ipc"
#define PIPE_OUT_ALG "ipc:///tmp/emb2alg.ipc"
#define PIPE_OUT_MOT "ipc:///tmp/emb2mot.ipc"
#define PIPE_OUT_COM "ipc:///tmp/emb2com.ipc"

// TODO: 2 data pipes for R2 and WallE

int main() {
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

    // Send to motors
    void *pub_mot = zmq_socket(ctx, ZMQ_PUB);
    zmq_connect(pub_mot, PIPE_OUT_MOT);

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

                // TODO: Send necessary stuff to motors here
                // Test
                double dir_x = 0;
                double dir_y = 9;
                int speed = 0;
                bool scanning = false;

                cJSON *to_mot = cJSON_CreateObject();
                cJSON_AddNumberToObject(to_mot, "dir_x", dir_x);
                cJSON_AddNumberToObject(to_mot, "dir_y", dir_y);
                cJSON_AddNumberToObject(to_mot, "speed", speed);
                cJSON_AddBoolToObject(to_mot, "scanning", scanning);

                char *to_mot_str = cJSON_PrintUnformatted(to_mot);
                zmq_send(pub_mot, to_mot_str, strlen(to_mot_str), 0);
                printf("[EMB] To motors: %s\n", to_mot_str); fflush(stdout);

                free(to_mot_str);
                cJSON_Delete(to_mot);

                // END TODO

                cJSON_Delete(from_alg);

                // --- 4. RELAY TO COMMUNICATIONS ---
                // positions, block locations, temperature, block colors
                cJSON *to_com = cJSON_CreateObject();
                // TESTING
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