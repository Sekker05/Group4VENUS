/*
 * communication.c  — Venus Robot UART ↔ ZMQ Bridge
 *
 * Receives JSON telemetry from WallE.py over ZMQ and forwards it over UART
 * to an external host (e.g. the base station running the visualizer).
 * Also receives JSON from UART and injects it back into the ZMQ bus.
 *
 * IPC topology:
 *   SUB  ipc://WallE_info.ipc   — connect (WallE.py binds)
 *   PUB  ipc://com2walle.ipc    — bind (future upstream injection)
 *
 * Wire protocol over UART:
 *   [4-byte big-endian length][N bytes JSON payload]
 *
 * Run independently for testing:
 *   compile with -DCOMM_TEST_MODE → prints UART bytes to stdout rather than
 *   actually touching UART hardware so you can test on a dev machine.
 *   make test
 */

#include <libpynq.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef COMM_TEST_MODE
#include <zmq.h>
#include <cjson/cJSON.h>
#endif

#ifndef COMM_TEST_MODE
#define IPC_SUB_WALLE   "ipc://WallE_info.ipc"
#define IPC_SUB_SEN     "ipc://sensors.ipc"
#define IPC_PUB_REPLY   "ipc://com2walle.ipc"
#endif

#define UART_RX_PIN  IO_AR0
#define UART_TX_PIN  IO_AR1

static void send_length_prefixed(uint32_t n, const char *buf)
{
#ifndef COMM_TEST_MODE
    uint8_t *len_bytes = (uint8_t *)&n;
    for (int i = 0; i < 4; i++) uart_send(UART0, len_bytes[i]);
    for (uint32_t i = 0; i < n; i++) uart_send(UART0, (uint8_t)buf[i]);
#else
    printf("[COMM TEST] UART TX [%u bytes]: %.*s\n", n, (int)n, buf);
    fflush(stdout);
#endif
}

int main(void)
{
    printf("[COM] Communication bridge starting...\n"); fflush(stdout);

#ifndef COMM_TEST_MODE
    unlink("com2walle.ipc");

    void *ctx      = zmq_ctx_new();
    void *receiver = zmq_socket(ctx, ZMQ_SUB);
    void *receiver_sen = zmq_socket(ctx, ZMQ_SUB);
    void *sender   = zmq_socket(ctx, ZMQ_PUB);

    zmq_bind(sender, IPC_PUB_REPLY);

    int conflate = 1;
    zmq_setsockopt(receiver, ZMQ_CONFLATE,  &conflate, sizeof(conflate));
    zmq_setsockopt(receiver, ZMQ_SUBSCRIBE, "", 0);
    zmq_connect(receiver, IPC_SUB_WALLE);
    
    
    zmq_setsockopt(receiver_sen, ZMQ_CONFLATE,  &conflate, sizeof(conflate));
    zmq_setsockopt(receiver_sen, ZMQ_SUBSCRIBE, "", 0);
    zmq_connect(receiver_sen, IPC_SUB_SEN);
    
    

    switchbox_init();
    switchbox_set_pin(UART_RX_PIN, SWB_UART0_RX);
    switchbox_set_pin(UART_TX_PIN, SWB_UART0_TX);
    uart_init(UART0);
    uart_reset_fifos(UART0);

    printf("[COM] UART and ZMQ ready. Bridging...\n"); fflush(stdout);

    char buf[2048];
    char buf_sen[2048];

    while (1) {
        int n = zmq_recv(receiver, buf, sizeof(buf) - 1, ZMQ_DONTWAIT);
        if (n >= 0) {
            buf[n] = '\0';
            send_length_prefixed((uint32_t)n, buf);
            printf("[COM] UART TX: %s\n", buf); fflush(stdout);
        }
        
        sleep_msec(300);
        
        int m = zmq_recv(receiver_sen, buf_sen, sizeof(buf_sen) - 1, ZMQ_DONTWAIT);
        if (m >= 0) {
            buf_sen[m] = '\0';
            send_length_prefixed((uint32_t)m, buf_sen);
            printf("[COM] UART TX: %s\n", buf_sen); fflush(stdout);
        }


        if (uart_has_data(UART0)) {
            uint8_t len_bytes[4];
            for (int i = 0; i < 4; i++) len_bytes[i] = uart_recv(UART0);

            uint32_t length = *((uint32_t *)len_bytes);

            if (length > 0 && length < sizeof(buf) - 1) {
                char *rbuf = malloc(length + 1);
                for (uint32_t i = 0; i < length; i++)
                    rbuf[i] = (char)uart_recv(UART0);
                rbuf[length] = '\0';
                zmq_send(sender, rbuf, length, 0);
                printf("[COM] UART RX: %s\n", rbuf); fflush(stdout);
                free(rbuf);
            } else {
                printf("[COM] Dropped UART RX — invalid length %u\n", length);
                fflush(stdout);
            }
        }

        sleep_msec(1000);
    }

    uart_reset_fifos(UART0);
    uart_destroy(UART0);
    zmq_close(receiver);
    zmq_close(sender);
    zmq_ctx_destroy(ctx);
    unlink("com2walle.ipc");
    
    pynq_destroy();

#else
    printf("[COMM TEST] Running in test mode — no hardware, no ZMQ.\n");
    printf("[COMM TEST] Simulating 5 outgoing messages...\n");
    for (int i = 0; i < 5; i++) {
        char msg[128];
        int len = snprintf(msg, sizeof(msg),
            "{\"distance\":%d,\"tape\":false,\"temperature\":25,\"color_front\":\"GREEN\"}", i);
        send_length_prefixed((uint32_t)len, msg);
        usleep(500000);
    }
    printf("[COMM TEST] Done.\n");
#endif
    return 0;
}