# --- 1. Compiler and Flag Settings ---
CC      = gcc
CFLAGS  = -Wall -Wextra -O3 -g -I../../libpynq/library -I../../libpynq/platform
LDFLAGS = -lzmq -lcjson -lm

# --- 2. Paths to Library Dependencies ---
LIB_DIR = ../../libpynq/library
OBJ     = motor.o \
          $(LIB_DIR)/stepper.o \
          $(LIB_DIR)/log.o \
          $(LIB_DIR)/arm_shared_memory_system.o \
          $(LIB_DIR)/gpio.o \
          $(LIB_DIR)/switchbox.o \
          $(LIB_DIR)/version.o \
          $(LIB_DIR)/pinmap.o \
          $(LIB_DIR)/libpynq.o

# --- 3. Primary Build Target (Physical Hardware Mode) ---
motor: $(OBJ)
	@echo "[BUILD] Compiling Clanker Motor Production Target..."
	$(CC) $(CFLAGS) -o motor $(OBJ) $(LDFLAGS)
	@echo "[SUCCESS] Production binary ready: ./motor"

# --- 4. Test Bench Target (Isolates output to motor_test binary) ---
motor_test:
	@echo "[BUILD] Compiling Mock Motor Test Bench..."
	$(CC) $(CFLAGS) -DTEST_BENCH -o motor_test motor.c \
		$(LIB_DIR)/stepper.c \
		$(LIB_DIR)/log.c \
		$(LIB_DIR)/arm_shared_memory_system.c \
		$(LIB_DIR)/gpio.c \
		$(LIB_DIR)/switchbox.c \
		$(LIB_DIR)/version.c \
		$(LIB_DIR)/pinmap.c \
		$(LIB_DIR)/libpynq.c \
		$(LDFLAGS)
	@echo "[SUCCESS] Test bench binary ready: ./motor_test"

# --- 5. Rules to Compile Individual Object Files ---
motor.o: motor.c
	$(CC) $(CFLAGS) -c motor.c -o motor.o

$(LIB_DIR)/stepper.o: $(LIB_DIR)/stepper.c
	$(CC) $(CFLAGS) -c $(LIB_DIR)/stepper.c -o $(LIB_DIR)/stepper.o

$(LIB_DIR)/log.o: $(LIB_DIR)/log.c
	$(CC) $(CFLAGS) -c $(LIB_DIR)/log.c -o $(LIB_DIR)/log.o

$(LIB_DIR)/arm_shared_memory_system.o: $(LIB_DIR)/arm_shared_memory_system.c
	$(CC) $(CFLAGS) -c $(LIB_DIR)/arm_shared_memory_system.c -o $(LIB_DIR)/arm_shared_memory_system.o

$(LIB_DIR)/gpio.o: $(LIB_DIR)/gpio.c
	$(CC) $(CFLAGS) -c $(LIB_DIR)/gpio.c -o $(LIB_DIR)/gpio.o

$(LIB_DIR)/switchbox.o: $(LIB_DIR)/switchbox.c
	$(CC) $(CFLAGS) -c $(LIB_DIR)/switchbox.c -o $(LIB_DIR)/switchbox.o

$(LIB_DIR)/version.o: $(LIB_DIR)/version.c
	$(CC) $(CFLAGS) -c $(LIB_DIR)/version.c -o $(LIB_DIR)/version.o

$(LIB_DIR)/pinmap.o: $(LIB_DIR)/pinmap.c
	$(CC) $(CFLAGS) -c $(LIB_DIR)/pinmap.c -o $(LIB_DIR)/pinmap.o

$(LIB_DIR)/libpynq.o: $(LIB_DIR)/libpynq.c
	$(CC) $(CFLAGS) -c $(LIB_DIR)/libpynq.c -o $(LIB_DIR)/libpynq.o

# --- 6. Clean Utility ---
clean:
	@echo "[CLEAN] Purging compiled motor binaries and footprints..."
	rm -f motor motor_test motor.o $(LIB_DIR)/*.o
	@echo "[CLEAN] Motor workspace scrubbed."