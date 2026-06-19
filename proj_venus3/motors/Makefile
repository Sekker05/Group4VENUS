# motors/Makefile
# Layout: ../libpynq/ is the sibling of ../proj_venus3/
#
# Usage:
#   make        → production binary  ./main
#   make test   → test binary        ./motors_test  (stdin commands, no ZMQ)
#   make clean

CC      := gcc
PYNQ    := ../../libpynq
CFLAGS  := -Wall -Wextra -O2 -g3 -std=c99 -D_POSIX_C_SOURCE=200809L \
           -I$(PYNQ)/library -I$(PYNQ)/platform -I.
LDFLAGS := -lm -lzmq -lcjson

LIBPYNQ_A := $(PYNQ)/build/lib/libpynq.a
SOURCES   := motors.c

.PHONY: all test clean run run_test

all: main

main: $(SOURCES) $(LIBPYNQ_A)
	@echo "[BUILD] motors/main (production)"
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LIBPYNQ_A) $(LDFLAGS)
	@echo "[OK] motors/main ready"
	sudo setcap cap_sys_nice,cap_sys_rawio+ep ./main

motors_test: $(SOURCES) $(LIBPYNQ_A)
	@echo "[BUILD] motors/motors_test (stdin test)"
	$(CC) $(CFLAGS) -DMOTORS_TEST_MODE -o $@ $(SOURCES) $(LIBPYNQ_A) -lm
	@echo "[OK] motors_test ready — run with: sudo ./motors_test"
	sudo setcap cap_sys_nice,cap_sys_rawio+ep ./motors_test

test: motors_test

run_test: motors_test
	@echo "[RUN] Running motors_test — type 'dx dy speed' then Enter"
	./motors_test

$(LIBPYNQ_A):
	@echo "[BUILD] Building libpynq first..."
	$(MAKE) -C $(PYNQ)

run: main
	./main

clean:
	rm -f main motors_test motors.o
	@echo "[CLEAN] motors cleaned"
