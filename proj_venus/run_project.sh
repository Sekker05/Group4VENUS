#!/bin/bash

# ==============================================================================
# Master Runner for Venus_project
# Coordinates WallE.py and the C motor controller
# ==============================================================================

set -e

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cleanup() {
    echo ""
    echo "[SYSTEM] Shutting down processes..."
    sudo pkill -f sensors 2>/dev/null || true
    sudo pkill -f motors  2>/dev/null || true
    sudo pkill -f WallE   2>/dev/null || true
    pkill -P $$ 2>/dev/null || true

    echo "[SYSTEM] Removing IPC socket files..."
    sudo fuser -k "$BASE_DIR/WallE_info.ipc" 2>/dev/null || true
    sudo fuser -k "$BASE_DIR/sensors.ipc"    2>/dev/null || true
    sudo fuser -k "$BASE_DIR/motors.ipc"     2>/dev/null || true
    rm -f "$BASE_DIR/WallE_info.ipc" \
          "$BASE_DIR/sensors.ipc"    \
          "$BASE_DIR/motors.ipc"
    echo "[SYSTEM] Goodbye!"
}
trap cleanup EXIT INT TERM

# Kill and clean up anything left from a previous run
echo "[SYSTEM] Checking for leftover processes and sockets..."
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

# Check arguments
if [ -z "$1" ]; then
    echo "Usage: ./run_project.sh [run | test | python-only]"
    echo "  run         : Compiles and runs the hardware motor controller + WallE.py"
    echo "  test        : Compiles and runs the software test bench + WallE.py"
    echo "  python-only : Runs WallE.py by itself (no C code)"
    exit 1
fi

MODE=$1

if [ "$MODE" == "test" ]; then
    echo "[SYSTEM] Building TEST BENCH mode..."
    cd "$BASE_DIR/motors" && make clean && make motor_test
    echo "[SYSTEM] Starting motor_test (C)..."
    sudo ./motor_test &
    cd "$BASE_DIR"

elif [ "$MODE" == "run" ]; then
    echo "[SYSTEM] Building HARDWARE mode..."
    cd "$BASE_DIR/motors" && make clean && make motor
    echo "[SYSTEM] Starting motor (C)..."
    sudo ./motor &
    cd "$BASE_DIR"

elif [ "$MODE" == "python-only" ]; then
    echo "[SYSTEM] Running Python only..."
else
    echo "[ERROR] Unknown mode: $MODE"
    exit 1
fi

sleep 0.5

echo "[SYSTEM] Starting WallE.py (Python)..."
python3 "$BASE_DIR/WallE.py" &

echo "[SYSTEM] Both processes are running. Press Ctrl+C to stop."
wait
