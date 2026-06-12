#!/bin/bash

# Exit immediately if any compilation or execution step fails

echo "=================================================="
echo "    WallE Robot Core Stack: Production Launch     "
echo "=================================================="

# 1. Pull the absolute path of the proj_venus directory
BASE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# 2. Kill and clean up anything left from a previous run
echo -e "\n[CLEANUP] Checking for leftover processes and sockets..."
sudo pkill -f sensors 2>/dev/null || true
sudo pkill -f motors  2>/dev/null || true
sudo pkill -f WallE   2>/dev/null || true
sudo fuser -k "$BASE_DIR/WallE_info.ipc" 2>/dev/null || true
sudo fuser -k "$BASE_DIR/sensors.ipc"    2>/dev/null || true
sudo fuser -k "$BASE_DIR/motors.ipc"     2>/dev/null || true
rm -f "$BASE_DIR/WallE_info.ipc" \
      "$BASE_DIR/sensors.ipc"    \
      "$BASE_DIR/motors.ipc"
sleep 0.5

# 3. Compile the C subsystems cleanly using their own Makefiles
echo -e "\n[BUILD] Checking and compiling C subsystems..."
cd "$BASE_DIR/sensors" && make
cd "$BASE_DIR/motors"  && make

# 4. Request sudo privileges upfront so background tasks don't hang
echo -e "\n[AUTH] Requesting root privileges to access PYNQ hardware pins..."
sudo -v

# Keep-alive: update existing sudo timestamp until script finishes
while true; do sudo -n true; sleep 60; kill -0 "$$" || exit; done 2>/dev/null &

# 5. Cleanup function for safe shutdown
function cleanup() {
    echo -e "\n\n=================================================="
    echo "    Shutting down WallE Core Stack Safely...     "
    echo "=================================================="

    if [ -n "$SENSOR_PID" ]; then
        sudo kill $SENSOR_PID 2>/dev/null || true
    fi
    if [ -n "$MOTOR_PID" ]; then
        sudo kill $MOTOR_PID 2>/dev/null || true
    fi
    sudo pkill -f WallE 2>/dev/null || true

    echo "[CLEANUP] Removing IPC socket files..."
    sudo fuser -k "$BASE_DIR/WallE_info.ipc" 2>/dev/null || true
    sudo fuser -k "$BASE_DIR/sensors.ipc"    2>/dev/null || true
    sudo fuser -k "$BASE_DIR/motors.ipc"     2>/dev/null || true
    rm -f "$BASE_DIR/WallE_info.ipc" \
          "$BASE_DIR/sensors.ipc"    \
          "$BASE_DIR/motors.ipc"

    echo "[CLEANUP] All processes terminated. Goodbye!"
    exit 0
}
trap cleanup INT TERM EXIT

# 6. Launch the Sensor Hub in the background
echo "[LAUNCH 1/3] Starting C Sensor Hub..."
cd "$BASE_DIR/sensors"
sudo env LD_LIBRARY_PATH=/home/student/libpynq/build/lib ./sensors &
SENSOR_PID=$!

# 7. Launch the Motor Engine in the background
echo "[LAUNCH 2/3] Starting C Motor Engine..."
cd "$BASE_DIR/motors"
sudo env LD_LIBRARY_PATH=/home/student/libpynq/build/lib ./motor &
MOTOR_PID=$!

# Give the hardware background services a moment to open sockets
sleep 1

# 8. Launch the main Python Brain in the foreground
echo "[LAUNCH 3/3] Executing WallE Python Brain..."
cd "$BASE_DIR"
python3 WallE.py
