# communication/Makefile
CC      := gcc
PYNQ    := ../../libpynq
CFLAGS  := -Wall -Wextra -O2 -g3 -std=c99 -D_POSIX_C_SOURCE=200809L \
           -I$(PYNQ)/library -I$(PYNQ)/platform -I.
LDFLAGS := -lm -lzmq -lcjson

LIBPYNQ_A := $(PYNQ)/build/lib/libpynq.a
SOURCES   := communication.c

.PHONY: all test clean run run_test

all: main

main: $(SOURCES) $(LIBPYNQ_A)
	@echo "[BUILD] communication/main"
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LIBPYNQ_A) $(LDFLAGS)
	@echo "[OK] communication/main ready"
	sudo setcap cap_sys_nice,cap_sys_rawio+ep ./main

comm_test: $(SOURCES)
	@echo "[BUILD] communication/comm_test (no hardware)"
	$(CC) $(CFLAGS) -DCOMM_TEST_MODE -o $@ $(SOURCES) -lm
	@echo "[OK] comm_test ready — run with: ./comm_test"

test: comm_test

run_test: comm_test
	./comm_test

$(LIBPYNQ_A):
	$(MAKE) -C $(PYNQ)

run: main
	./main

clean:
	rm -f main comm_test communication.o
	@echo "[CLEAN] communication cleaned"
