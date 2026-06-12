import zmq
import json
import time
import subprocess
import sys
import os
import signal

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
IPC_BRAIN  = f"ipc://{BASE_DIR}/WallE_info.ipc"
IPC_SENSOR = f"ipc://{BASE_DIR}/sensors.ipc"

def kill_leftover_processes():
    print("[CLEANUP] Killing any leftover sensors/WallE processes...")
    subprocess.run("sudo pkill -f sensors  2>/dev/null || true", shell=True)
    subprocess.run("sudo pkill -f WallE    2>/dev/null || true", shell=True)
    time.sleep(0.5)

def cleanup_sockets():
    print("[CLEANUP] Removing stale IPC socket files...")
    ipc_brain_path  = f"{BASE_DIR}/WallE_info.ipc"
    ipc_sensor_path = f"{BASE_DIR}/sensors.ipc"
    subprocess.run(f"sudo fuser -k {ipc_brain_path}  2>/dev/null || true", shell=True)
    subprocess.run(f"sudo fuser -k {ipc_sensor_path} 2>/dev/null || true", shell=True)
    subprocess.run(f"rm -f {ipc_brain_path} {ipc_sensor_path}", shell=True)
    time.sleep(0.3)

def run_test_bench():
    print("==========================================================")
    print("   WallE / Clanker Core System Integration Test Bench     ")
    print("==========================================================")

    # 1. Kill leftover processes and clean up stale socket files
    kill_leftover_processes()
    cleanup_sockets()

    # 2. Setup standard IPC sockets to mimic the software stack
    ctx = zmq.Context()

    # We act as the Brain's output channel to drive state updates into C
    mock_brain_pub = ctx.socket(zmq.PUB)
    mock_brain_pub.bind(IPC_BRAIN)

    # We act as the Brain's input channel to read data produced by C
    sensor_sub = ctx.socket(zmq.SUB)
    sensor_sub.connect(IPC_SENSOR)
    sensor_sub.set(zmq.SUBSCRIBE, b"")

    # 3. Spin up the production C sensors binary as a background process
    print("\n[LAUNCH] Starting production C sensors binary under test...")
    c_process = subprocess.Popen(["sudo", f"{BASE_DIR}/sensors/sensors"],
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)

    # Allow the C program to boot and bind sockets, then warm up the SUB connection
    time.sleep(2.0)

    # Ensure the background C process didn't instantly crash
    if c_process.poll() is not None:
        print("[FAIL] C sensors binary failed to start. Did you compile it?")
        cleanup_sockets()
        sys.exit(1)

    # Warm up ZMQ slow-joiner: send a dummy message so the SUB is fully connected
    print("[INIT] Warming up ZMQ subscriber connection...")
    mock_brain_pub.send_string(json.dumps({"scanning": False}))
    time.sleep(0.5)

    # 4. Matrix of Test Inputs to evaluate
    test_cases = [
        {"input_scanning": False, "expected_status": "NORMAL", "desc": "Baseline tracking"},
        {"input_scanning": True,  "expected_status": "SCAN",   "desc": "Trigger color sensor matching"},
        {"input_scanning": False, "expected_status": "NORMAL", "desc": "Return to baseline tracking"}
    ]

    passed_tests = 0

    try:
        for i, case in enumerate(test_cases, 1):
            print(f"\n--- Test Case {i}: {case['desc']} ---")

            command_payload = {"scanning": case["input_scanning"]}
            print(f"[INPUT] Sending Command to C: {command_payload}")
            mock_brain_pub.send_string(json.dumps(command_payload))

            if case["input_scanning"] == True:
                print("[WAIT] Allowing 2.1 seconds for C lockout state mitigation...")
                time.sleep(2.1)
            else:
                time.sleep(0.5)

            received_payload = None
            matched_target = False

            time.sleep(0.1)

            while True:
                try:
                    msg = sensor_sub.recv_string(flags=zmq.NOBLOCK)
                    packet = json.loads(msg)
                    received_payload = packet

                    if packet.get("status") == case["expected_status"]:
                        matched_target = True
                except zmq.Again:
                    break

            if received_payload:
                print(f"[EVAL] Last processed packet from C: {received_payload}")

                if matched_target:
                    print(f"✅ [PASS] Target status found: Got {case['expected_status']}")
                    passed_tests += 1
                else:
                    print(f"❌ [FAIL] Status mismatch! Expected {case['expected_status']}, but it wasn't captured.")
            else:
                print("❌ [FAIL] No telemetry frames captured from C subsystem.")

            time.sleep(0.5)

    finally:
        # 5. Clean up and shut down everything safely
        print("\n==========================================================")
        print("[CLEANUP] Terminating background processes...")
        subprocess.run(["sudo", "kill", str(c_process.pid)], stderr=subprocess.DEVNULL)
        c_process.terminate()
        c_process.wait()
        ctx.destroy()
        cleanup_sockets()

    # Final Summary Report
    print(f"\n[RESULT] Test Bench Complete: {passed_tests}/{len(test_cases)} Tests Passed.")
    if passed_tests == len(test_cases):
        print("🎉 SYSTEM VALIDATION SUCCESSFUL!")
    else:
        print("⚠️  INTEGRATION FAILURES DETECTED.")

if __name__ == "__main__":
    run_test_bench()
