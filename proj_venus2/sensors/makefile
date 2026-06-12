# --- Compiler Configuration ---
CC       = gcc
CFLAGS   = -O2 -Wall -Wextra -std=gnu11
INCLUDES = -I/home/student/proj_venus/sensors/../../libpynq/library
LIBS     = -lzmq -lcjson -lm

# --- Targets ---
TARGET   = sensors
SRC      = sensors.c

# --- Build Rules ---
.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	@echo "[BUILD] Compiling Hardware Release Target ($(TARGET))..."
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) -o $(TARGET) $(LIBS)
	@echo "[SUCCESS] Core production binary generated: ./$(TARGET)"

clean:
	@echo "[CLEAN] Purging compiled binary footprints..."
	rm -f $(TARGET)
	@echo "[CLEAN] Workspace scrubbed."

run: $(TARGET)
	@echo "[RUN] Executing production Sensor Hub..."
	./$(TARGET)