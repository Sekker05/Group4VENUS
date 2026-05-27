#include <libpynq.h>
#include <zmq.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define PIPE_IN  "ipc:///tmp/emb2com.ipc"
#define PIPE_OUT "ipc:///tmp/com2emb.ipc"

int main()
{
    printf("1\n"); fflush(stdout);

    void *ctx      = zmq_ctx_new();
    void *receiver = zmq_socket(ctx, ZMQ_SUB);
    void *sender   = zmq_socket(ctx, ZMQ_PUB);

    zmq_connect(receiver, PIPE_IN);
    zmq_setsockopt(receiver, ZMQ_SUBSCRIBE, "", 0);  // subscribe to all
    zmq_connect(sender, PIPE_OUT);
    
    switchbox_init();
    switchbox_set_pin(IO_AR0, SWB_UART0_RX);
    switchbox_set_pin(IO_AR1, SWB_UART0_TX);
    uart_init(UART0);
    uart_reset_fifos(UART0);


    char buf[2048];
    uint8_t len[4];
    int length;

    printf("2\n"); fflush(stdout);

    while (1)
    {   
        // RECEIVE FROM EMBEDDED
        int n = zmq_recv(receiver, buf, sizeof(buf)-1, ZMQ_DONTWAIT);

        if (n >= 0)
        {
            buf[n] = '\0';

            len[0] = (n >> 24) & 0xFF;
            len[1] = (n >> 16) & 0xFF;
            len[2] = (n >> 8 ) & 0xFF;
            len[3] = (n      ) & 0xFF;

            for (int i = 0; i < 4; i++)
                uart_send(UART0, len[i]);

            for (int i = 0; i < n; i++)
                uart_send(UART0, buf[i]);

            printf("Outgoing Message: %s\n", buf);
        }
        
        if (uart_has_data(UART0)) {
            for (int i = 0; i < 4; i++)
                len[i] = uart_recv(UART0);

            length =
                ((uint32_t)len[0] << 24) |
                ((uint32_t)len[1] << 16) |
                ((uint32_t)len[2] << 8 ) |
                ((uint32_t)len[3]);

            char *buffer = malloc(length + 1);

            for (int i = 0; i < length; i++)
                buffer[i] = uart_recv(UART0);

            buffer[length] = '\0';

            zmq_send(sender, buffer, length, 0);

            printf("Incoming Message: %s\n", buffer);

            free(buffer);
        }
    }


    fflush(NULL);
    uart_reset_fifos(UART0);
    uart_destroy(UART0);
    pynq_destroy();
    return EXIT_SUCCESS;
}