#!/usr/bin/env python3
"""
main_launcher.py — Venus Robot Master Launcher
===============================================
Starts all subsystem processes in the correct order, with IPC cleanup
at startup and shutdown so stale socket files never cause EADDRINUSE.

Process start order (matters for ZMQ bind/connect sequence):
  1. sensors/main          — BINDS sensors.ipc  (must be first so subscribers don't miss data)
  2. WallE.py              — BINDS WallE_info.ipc, CONNECTS sensors.ipc
  3. algorithm/R2.py       — BINDS /tmp/R2_info.ipc, CONNECTS sensors.ipc
  4. motors/main           — CONNECTS WallE_info.ipc and /tmp/R2_info.ipc
  5. communication/main    — CONNECTS WallE_info.ipc

ZMQ connect-before-bind is handled gracefully by the library (reconnect loop),
but starting binders first avoids initial lost messages for non-conflated sockets.

Usage:
    python3 main_launcher.py           # full production run
    python3 main_launcher.py --no-r2   # run without R2 (WallE only)
    python3 main_launcher.py --no-comm # skip UART communication bridge

Press Ctrl-C to stop all processes cleanly.
"""

import subprocess
import sys
import time
import os
import signal

import os
import time

def fix_boot_timestamps(project_root):
    print("============================================================")
    print("[BOOT] Automatic Time-Sync & Timestamp Fix Active")
    print("============================================================")
    
    # 1. Update all files to match current boot time recursively
    print("[BOOT] Updating file timestamps to prevent clock skew...")
    current_time = time.time()
    
    for root, dirs, files in os.walk(project_root):
        for name in files:
            file_path = os.path.join(root, name)
            try:
                # This mimics 'touch' perfectly in Python
                os.utime(file_path, (current_time, current_time))
            except Exception as e:
                # Ignore hidden system files or root locks if they pop up
                pass
                
    print("[BOOT] Timestamps synchronized successfully.")
    print("============================================================\n")

# --- Existing Master Launcher Setup Below ---

# ─── IPC files to clean up ───────────────────────────────────────────────────
# Relative-path IPC files live in the working directory (proj_venus3 root).
IPC_FILES_REL = [
    "sensors.ipc",
    "WallE_info.ipc",
]
# /tmp IPC files
IPC_FILES_TMP = [
    "/tmp/R2_info.ipc",
]


def cleanup_ipc(label=""):
    """Remove all IPC socket files (ignores missing files)."""
    tag = f"[{label}] " if label else ""
    for f in IPC_FILES_REL + IPC_FILES_TMP:
        try:
            os.unlink(f)
            print(f"{tag}Removed stale IPC: {f}")
        except FileNotFoundError:
            pass


def build_all():
    """Compile C modules. Exits on failure."""
    modules = ["sensors", "motors", "communication"]
    for mod in modules:
        print(f"[BUILD] Compiling {mod}...")
        result = subprocess.run(["make", "-C", mod, "main"], capture_output=False)
        if result.returncode != 0:
            print(f"[ERROR] Failed to build {mod}. Aborting.")
            sys.exit(1)
    print("[BUILD] All C modules compiled.\n")

def kill_existing():
    """Kill any leftover processes from a previous run."""
    targets = ["sensors/main", "motors/main", "communication/main", "WallE.py"]
    for t in targets:
        subprocess.run(["pkill", "-f", t], capture_output=True)
    time.sleep(1)  # give them a moment to die
    print("[STARTUP] Killed any existing processes.\n")
    
def wait_for_hardware():
    """Wait until I2C bus is available."""
    print("[BOOT] Waiting for I2C hardware to be ready...")
    for _ in range(30):  # up to 30 seconds
        if os.path.exists("/dev/i2c-1"):  # adjust i2c-1 to whichever bus you use
            print("[BOOT] I2C ready.\n")
            return
        time.sleep(1)
    print("[BOOT] WARNING: I2C not found after 30s — continuing anyway.\n")

def main():
    # ── Parse args ────────────────────────────────────────────────────────────
    no_r2   = "--no-r2"   in sys.argv
    no_comm = "--no-comm" in sys.argv
    no_build = "--no-build" in sys.argv

    print("=" * 60)
    print("  Venus Robot — Master Launcher")
    print("=" * 60)
    
    wait_for_hardware()
    kill_existing()

    # ── Build step ────────────────────────────────────────────────────────────
    if not no_build:
        build_all()
    else:
        print("[BUILD] Skipped (--no-build)\n")

    # ── IPC cleanup at startup ────────────────────────────────────────────────
    print("[STARTUP] Cleaning up stale IPC socket files...")
    cleanup_ipc("STARTUP")

    # ── Launch processes ──────────────────────────────────────────────────────
    procs = []

    # 1. Sensor hub — BINDS sensors.ipc
    print("[LAUNCH 1/4] sensors/main")
    procs.append(subprocess.Popen(["sensors/main"],
                                  cwd=os.path.dirname(os.path.abspath(__file__))))

    time.sleep(3)   # give sensors time to bind before algorithms connect

    # 2. WallE algorithm — BINDS WallE_info.ipc, CONNECTS sensors.ipc
    print("[LAUNCH 2/4] WallE.py")
    procs.append(subprocess.Popen(["python3", "WallE.py"],
                                  cwd=os.path.dirname(os.path.abspath(__file__))))

    time.sleep(0.5)


    # 4. Motor driver — CONNECTS WallE_info.ipc (and /tmp/R2_info.ipc if R2 runs)
    print("[LAUNCH 3/4] motors/main")
    procs.append(subprocess.Popen(["motors/main"],
                                  cwd=os.path.dirname(os.path.abspath(__file__))))

    time.sleep(1.3)

    # 5. Communication bridge — CONNECTS WallE_info.ipc
    if not no_comm:
        print("[LAUNCH 4/4] communication/main")
        procs.append(subprocess.Popen(["./communication/main"],
                                      cwd=os.path.dirname(os.path.abspath(__file__))))
    else:
        print("[LAUNCH 5/4] communication/main skipped (--no-comm)")

    print("\n[OK] All modules running.  Press Ctrl-C to stop.\n")

    # ── Monitor & wait ────────────────────────────────────────────────────────
    try:
        while True:
            time.sleep(1)
            # Check for any process that died unexpectedly
            for i, p in enumerate(procs):
                if p.poll() is not None:
                    print(f"[WARNING] Process {i} (PID {p.pid}) exited with code {p.returncode}")
    except KeyboardInterrupt:
        print("\n[STOP] Ctrl-C received — shutting down...")

    # ── Shutdown ──────────────────────────────────────────────────────────────
    for p in procs:
        try:
            p.terminate()
        except ProcessLookupError:
            pass

    # Give processes 2 s to terminate gracefully, then kill
    time.sleep(2)
    for p in procs:
        try:
            p.kill()
        except ProcessLookupError:
            pass

    # ── IPC cleanup at shutdown ───────────────────────────────────────────────
    print("[SHUTDOWN] Cleaning up IPC socket files...")
    cleanup_ipc("SHUTDOWN")

    print("[SHUTDOWN] Done. Goodbye!")


if __name__ == "__main__":
    main()
