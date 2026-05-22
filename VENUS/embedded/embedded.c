#include <libpynq.h>
#include <zmq.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PIPE_IN_SEN "ipc:///tmp/sen2emb.ipc"
#define PIPE_IN_ALG "ipc:///tmp/alg2emb.ipc"
#define PIPE_OUT_ALG "ipc:///tmp/emb2alg.ipc"

int main() {
    void *ctx      = zmq_ctx_new();
    void *receiver = zmq_socket(ctx, ZMQ_SUB);
    void *sender   = zmq_socket(ctx, ZMQ_PUB);

    // zmq_connect(receiver, PIPE_IN);
    // zmq_setsockopt(receiver, ZMQ_SUBSCRIBE, "", 0);  // subscribe to all
    // zmq_connect(sender, PIPE_OUT);

    char buf[1024];
    printf("<MODULE_EMB> Ready\n"); fflush(stdout);

    while (1) {
        // RECEIVE SENSORS
        zmq_connect(receiver, PIPE_IN_SEN);
        zmq_setsockopt(receiver, ZMQ_SUBSCRIBE, "", 0);  // subscribe to all
        int n = zmq_recv(receiver, buf, sizeof(buf) - 1, ZMQ_DONTWAIT);
        if (n >= 0) {
            buf[n] = '\0';

            // Parse JSON
            cJSON *msg = cJSON_Parse(buf);
            if (!msg) {
                printf("[EMB] Bad JSON\n");
                continue;
            }

            int distance = cJSON_GetObjectItem(msg, "distance")->valueint;
            int temperature = cJSON_GetObjectItem(msg, "temperature")->valueint;
            char *color_front = cJSON_GetObjectItem(msg, "color_front")->valuestring;
            char *color_ground = cJSON_GetObjectItem(msg, "color_ground")->valuestring;

            printf("[EMB] distance=%d  temperature=%d  color_front=%s  color_ground=%s\n", distance, temperature, color_front, color_ground);

            // int   counter = cJSON_GetObjectItem(msg, "counter")->valueint;
            // int   speed   = cJSON_GetObjectItem(msg, "speed")->valueint;
            // float turn    = (float)cJSON_GetObjectItem(msg, "turn")->valuedouble;
            // printf("[EMB] counter=%d  speed=%d  turn=%.2f\n",
            //        counter, speed, turn);

            fflush(stdout);
            cJSON_Delete(msg);

            // Build and send JSON reply
            cJSON *reply = cJSON_CreateObject();
            // cJSON_AddStringToObject(reply, "status", "ok");
            // cJSON_AddNumberToObject(reply, "echo_counter", counter);
            cJSON_AddNumberToObject(reply, "distance", distance);
            cJSON_AddBoolToObject  (reply, "ground_black", (strcmp(color_ground, "black")));
            
            char *reply_str = cJSON_PrintUnformatted(reply);

            zmq_connect(sender, PIPE_OUT_ALG);

            zmq_send(sender, reply_str, strlen(reply_str), 0);
            printf("[EMB] Sent: %s\n", reply_str); fflush(stdout);

            free(reply_str);
            cJSON_Delete(reply);
        } else {
            // printf("[EMB] Nothing received\n"); fflush(stdout);
            sleep(0.5);  // Wait before trying again
        }

        // RECEIVE ALGORITHM
        zmq_connect(receiver, PIPE_IN_ALG);
        zmq_setsockopt(receiver, ZMQ_SUBSCRIBE, "", 0);  // subscribe to all
        int n = zmq_recv(receiver, buf, sizeof(buf) - 1, ZMQ_DONTWAIT);
        if (n >= 0) {
            buf[n] = '\0';

            // Parse JSON
            cJSON *msg = cJSON_Parse(buf);
            if (!msg) {
                printf("[EMB] Bad JSON\n");
                continue;
            }

            int distance = cJSON_GetObjectItem(msg, "distance")->valueint;
            int temperature = cJSON_GetObjectItem(msg, "temperature")->valueint;
            char *color_front = cJSON_GetObjectItem(msg, "color_front")->valuestring;
            char *color_ground = cJSON_GetObjectItem(msg, "color_ground")->valuestring;

            printf("[EMB] distance=%d  temperature=%d  color_front=%s  color_ground=%s\n", distance, temperature, color_front, color_ground);

            // int   counter = cJSON_GetObjectItem(msg, "counter")->valueint;
            // int   speed   = cJSON_GetObjectItem(msg, "speed")->valueint;
            // float turn    = (float)cJSON_GetObjectItem(msg, "turn")->valuedouble;
            // printf("[EMB] counter=%d  speed=%d  turn=%.2f\n",
            //        counter, speed, turn);

            fflush(stdout);
            cJSON_Delete(msg);

            // Build and send JSON reply
            cJSON *reply = cJSON_CreateObject();
            // cJSON_AddStringToObject(reply, "status", "ok");
            // cJSON_AddNumberToObject(reply, "echo_counter", counter);
            cJSON_AddNumberToObject(reply, "distance", distance);
            cJSON_AddBoolToObject  (reply, "ground_black", (strcmp(color_ground, "black")));
            
            char *reply_str = cJSON_PrintUnformatted(reply);

            // zmq_connect(sender, PIPE_OUT_ALG);

            // zmq_send(sender, reply_str, strlen(reply_str), 0);
            // printf("[EMB] Sent: %s\n", reply_str); fflush(stdout);

            free(reply_str);
            cJSON_Delete(reply);
        } else {
            // printf("[EMB] Nothing received\n"); fflush(stdout);
            sleep(0.5);  // Wait before trying again
        }
    }

    zmq_close(receiver);
    zmq_close(sender);
    zmq_ctx_destroy(ctx);
    return 0;
}
