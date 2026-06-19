# sensors/Makefile
# Builds against libpynq which lives ONE level above this project (sibling of proj_venus3).
# Directory layout expected:
#   ../libpynq/         ← the libpynq repo
#   ../proj_venus3/     ← this project
#
# Usage:
#   make            → build production binary  ./main
#   make test       → build test binary        ./sensors_test  (no ZMQ, prints to stdout)
#   make clean      → remove built files

CC      := gcc
PYNQ    := ../../libpynq
CFLAGS  := -Wall -Wextra -O2 -g3 -std=c99 -D_POSIX_C_SOURCE=200809L \
           -I$(PYNQ)/library -I$(PYNQ)/platform -I.
LDFLAGS := -lm -lzmq -lcjson

# All libpynq objects we need (same list as VENUS2_2/end.mk builds via libpynq.a)
LIBPYNQ_A := $(PYNQ)/build/lib/libpynq.a

SOURCES := sensors.c

.PHONY: all test clean run run_test

all: main

# Production binary (with ZMQ, full hardware)
main: $(SOURCES) $(LIBPYNQ_A)
	@echo "[BUILD] sensors/main (production)"
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LIBPYNQ_A) $(LDFLAGS)
	@echo "[OK] sensors/main ready"
	sudo setcap cap_sys_nice,cap_sys_rawio+ep ./main

# Test binary: no ZMQ, just prints sensor readings to stdout
sensors_test: $(SOURCES) $(LIBPYNQ_A)
	@echo "[BUILD] sensors/sensors_test (standalone test)"
	$(CC) $(CFLAGS) -DSENSORS_TEST_MODE -o $@ $(SOURCES) $(LIBPYNQ_A) -lm
	@echo "[OK] sensors_test ready — run with: sudo ./sensors_test"
	sudo setcap cap_sys_nice,cap_sys_rawio+ep ./sensors_test

test: sensors_test

# Shortcut to run the test binary
run_test: sensors_test
	@echo "[RUN] Running sensors_test — Ctrl-C to stop"
	./sensors_test

# Make sure libpynq.a is built first if it doesn't exist
$(LIBPYNQ_A):
	@echo "[BUILD] libpynq.a not found — building libpynq first..."
	$(MAKE) -C $(PYNQ)

run: main
	./main

clean:
	rm -f main sensors_test sensors.o
	@echo "[CLEAN] sensors cleaned"
