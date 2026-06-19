#!/usr/bin/env python3
"""
test_connection.py — IPC / ZMQ Connection Test
================================================
Tests the ZMQ IPC plumbing WITHOUT requiring any C binaries or hardware.

Run this from the proj_venus3 root:
    python3 test/test_connection.py

What it does:
  1. Cleans up all IPC socket files.
  2. Creates mock versions of every IPC socket pair and verifies messages
     flow correctly in both directions.
  3. Simulates the full data path:
       sensors.ipc  →  WallE.py / R2.py (sensor data)
       WallE_info.ipc  →  motors / comm  (drive commands)
       /tmp/R2_info.ipc  →  motors (R2 drive commands)
  4. Prints PASS / FAIL for each check.

No hardware is needed.  Uses zmq.Poller for non-blocking waits with timeouts.
"""

import zmq
import json
import time
import os
import sys

# ─── IPC socket paths ────────────────────────────────────────────────────────
IPC_SENSORS       = "ipc://sensors.ipc"
IPC_WALLE_INFO    = "ipc://WallE_info.ipc"
IPC_R2_INFO       = "ipc:///tmp/R2_info.ipc"

IPC_FILES_REL     = ["sensors.ipc", "WallE_info.ipc"]
IPC_FILES_TMP     = ["/tmp/R2_info.ipc"]

# ─── Test state ──────────────────────────────────────────────────────────────
passed = 0
failed = 0


def check(label, condition, detail=""):
    global passed, failed
    if condition:
        print(f"  ✅ PASS: {label}")
        passed += 1
    else:
        print(f"  ❌ FAIL: {label}" + (f"  ({detail})" if detail else ""))
        failed += 1


def cleanup_ipc():
    """Remove all IPC socket files so bind() doesn't get EADDRINUSE."""
    for f in IPC_FILES_REL + IPC_FILES_TMP:
        try:
            os.unlink(f)
            print(f"  [cleanup] removed {f}")
        except FileNotFoundError:
            pass


def recv_timeout(sock, timeout_ms=500):
    """Non-blocking receive with timeout.  Returns message string or None."""
    poller = zmq.Poller()
    poller.register(sock, zmq.POLLIN)
    events = dict(poller.poll(timeout_ms))
    if sock in events and events[sock] == zmq.POLLIN:
        return sock.recv_string()
    return None


def test_sensors_to_algorithms():
    """
    Verify that a mock sensor publisher reaches both WallE and R2 subscribers.
    sensors/main binds sensors.ipc; WallE.py and R2.py both connect to it.
    """
    print("\n[TEST 1] sensors.ipc  →  algorithm subscribers")
    ctx = zmq.Context()

    # Mock sensors/main: BIND publisher
    pub = ctx.socket(zmq.PUB)
    pub.bind(IPC_SENSORS)

    # Mock WallE: CONNECT subscriber
    walle_sub = ctx.socket(zmq.SUB)
    walle_sub.connect(IPC_SENSORS)
    walle_sub.set(zmq.SUBSCRIBE, b"")

    # Mock R2: CONNECT subscriber
    r2_sub = ctx.socket(zmq.SUB)
    r2_sub.connect(IPC_SENSORS)
    r2_sub.set(zmq.SUBSCRIBE, b"")

    time.sleep(0.3)  # ZMQ slow-joiner

    payload = json.dumps({
        "distance": 45,
        "temperature": 27,
        "color_front": "GREEN",
        "tape": False,
    })
    pub.send_string(payload)
    time.sleep(0.05)

    walle_msg = recv_timeout(walle_sub)
    r2_msg    = recv_timeout(r2_sub)

    check("WallE receives sensor data",  walle_msg is not None,
          f"got: {walle_msg}")
    check("R2 receives sensor data",     r2_msg    is not None,
          f"got: {r2_msg}")

    if walle_msg:
        d = json.loads(walle_msg)
        check("sensor JSON has 'distance'",     'distance'    in d)
        check("sensor JSON has 'tape'",         'tape'        in d)
        check("sensor JSON has 'color_front'",  'color_front' in d)
        check("distance value correct",         d['distance'] == 45)

    pub.close(); walle_sub.close(); r2_sub.close()
    ctx.term()


def test_walle_to_motors():
    """
    Verify that WallE.py's publisher reaches motors/main and communication.
    WallE.py BINDS WallE_info.ipc; motors and comm CONNECT to it.
    """
    print("\n[TEST 2] WallE_info.ipc  →  motors + comm")
    ctx = zmq.Context()

    # Mock WallE.py: BIND publisher
    walle_pub = ctx.socket(zmq.PUB)
    walle_pub.bind(IPC_WALLE_INFO)

    # Mock motors/main: CONNECT subscriber
    motor_sub = ctx.socket(zmq.SUB)
    motor_sub.connect(IPC_WALLE_INFO)
    motor_sub.set(zmq.SUBSCRIBE, b"")

    # Mock communication/main: CONNECT subscriber
    comm_sub = ctx.socket(zmq.SUB)
    comm_sub.connect(IPC_WALLE_INFO)
    comm_sub.set(zmq.SUBSCRIBE, b"")

    time.sleep(0.3)

    walle_data = json.dumps({
        "position":   [10.0, 20.0],
        "direction":  [0.0, 1.0],
        "speed":      0.4,
        "scanning":   False,
        "turn_angle": 0.0,
        "state":      "sweeping",
        "obstacles":  [],
        "hits":       [],
    })
    walle_pub.send_string(walle_data)
    time.sleep(0.05)

    motor_msg = recv_timeout(motor_sub)
    comm_msg  = recv_timeout(comm_sub)

    check("motors receives WallE state",  motor_msg is not None)
    check("comm receives WallE state",    comm_msg  is not None)

    if motor_msg:
        d = json.loads(motor_msg)
        check("WallE JSON has 'direction'",  'direction' in d)
        check("WallE JSON has 'speed'",      'speed'     in d)
        check("WallE JSON has 'scanning'",   'scanning'  in d)
        check("speed value correct",         abs(d['speed'] - 0.4) < 1e-6)

    walle_pub.close(); motor_sub.close(); comm_sub.close()
    ctx.term()


def test_r2_to_r2_motors():
    """
    Verify that R2.py's publisher reaches its motor subscriber.
    R2.py BINDS /tmp/R2_info.ipc; motors/main (R2 side) CONNECTS.
    """
    print("\n[TEST 3] /tmp/R2_info.ipc  →  R2 motor consumer")
    ctx = zmq.Context()

    r2_pub = ctx.socket(zmq.PUB)
    r2_pub.bind(IPC_R2_INFO)

    r2_motor_sub = ctx.socket(zmq.SUB)
    r2_motor_sub.connect(IPC_R2_INFO)
    r2_motor_sub.set(zmq.SUBSCRIBE, b"")

    time.sleep(0.3)

    r2_data = json.dumps({
        "position":   [0.0, 40.0],
        "direction":  [0.0, 1.0],
        "speed":      15000,
        "scanning":   False,
        "turn_angle": 0.0,
        "state":      "scanning",
        "obstacles":  [],
        "hits":       [],
    })
    r2_pub.send_string(r2_data)
    time.sleep(0.05)

    msg = recv_timeout(r2_motor_sub)
    check("R2 motor consumer receives R2 state",  msg is not None)

    if msg:
        d = json.loads(msg)
        check("R2 JSON has 'speed'",     'speed'    in d)
        check("R2 JSON has 'scanning'",  'scanning' in d)

    r2_pub.close(); r2_motor_sub.close()
    ctx.term()


def test_conflate_behaviour():
    """
    Verify ZMQ CONFLATE: when a slow consumer falls behind, only the newest
    message is delivered.  This is critical for the 15 Hz real-time loop.
    """
    print("\n[TEST 4] ZMQ CONFLATE behaviour (newest-only delivery)")
    ctx = zmq.Context()

    pub = ctx.socket(zmq.PUB)
    pub.bind("ipc://test_conflate.ipc")

    # Sub WITH conflate
    sub_c = ctx.socket(zmq.SUB)
    sub_c.setsockopt(zmq.CONFLATE, 1)
    sub_c.connect("ipc://test_conflate.ipc")
    sub_c.set(zmq.SUBSCRIBE, b"")

    time.sleep(0.3)

    # Send 5 messages rapidly — slow consumer should only see the last one
    for i in range(5):
        pub.send_string(json.dumps({"seq": i}))
    time.sleep(0.1)  # let them queue

    msg = recv_timeout(sub_c, 200)
    if msg:
        d = json.loads(msg)
        check("CONFLATE delivers newest message (seq=4)",
              d.get('seq') == 4, f"got seq={d.get('seq')}")
    else:
        check("CONFLATE delivers newest message", False, "no message received")

    pub.close(); sub_c.close()
    try:
        os.unlink("test_conflate.ipc")
    except FileNotFoundError:
        pass
    ctx.term()


def test_ipc_cleanup():
    """
    Verify that IPC socket files are created on bind and removed by unlink().
    This confirms the cleanup pattern used at launcher startup/shutdown works.

    Note: libzmq 4.3+ allows re-binding to the same IPC path (it replaces the
    underlying file), so we do NOT test for double-bind failure — the important
    invariant is that unlink() removes the file and a subsequent bind creates a
    fresh, working socket.
    """
    print("\n[TEST 5] IPC file lifecycle: bind creates file, unlink clears it")
    ctx = zmq.Context()
    test_file = "test_guard.ipc"
    test_ep   = f"ipc://{test_file}"

    # Ensure clean start
    try:
        os.unlink(test_file)
    except FileNotFoundError:
        pass

    # Bind should create the IPC file
    s1 = ctx.socket(zmq.PUB)
    try:
        s1.bind(test_ep)
        bind_ok = True
    except zmq.ZMQError as e:
        bind_ok = False
        print(f"    (bind error: {e})")

    check("Bind on fresh IPC succeeds", bind_ok)
    check("IPC socket file exists after bind", os.path.exists(test_file))

    s1.close()
    time.sleep(0.05)  # brief settle before unlink

    # Unlink should remove the file
    try:
        os.unlink(test_file)
        unlink_ok = True
    except FileNotFoundError:
        unlink_ok = False

    check("unlink() removes IPC file", unlink_ok)
    check("IPC file gone after unlink", not os.path.exists(test_file))

    # A fresh bind after unlink must succeed — this is the pattern used in
    # all our C and Python modules at startup to avoid EADDRINUSE on crash recovery.
    s2 = ctx.socket(zmq.PUB)
    try:
        s2.bind(test_ep)
        rebind_ok = True
    except zmq.ZMQError as e:
        rebind_ok = False
        print(f"    (rebind error: {e})")

    check("Re-bind after unlink() succeeds (crash-recovery works)", rebind_ok)

    s2.close()
    try:
        os.unlink(test_file)
    except FileNotFoundError:
        pass
    ctx.term()


def main():
    print("=" * 60)
    print(" Venus Robot — IPC Connection Test Suite")
    print("=" * 60)

    print("\n[SETUP] Cleaning up any stale IPC files...")
    cleanup_ipc()

    test_sensors_to_algorithms()
    cleanup_ipc()

    test_walle_to_motors()
    cleanup_ipc()

    test_r2_to_r2_motors()
    cleanup_ipc()

    test_conflate_behaviour()
    cleanup_ipc()

    test_ipc_cleanup()

    print("\n" + "=" * 60)
    print(f" Results: {passed} passed,  {failed} failed  "
          f"(total {passed + failed})")
    print("=" * 60)

    if failed == 0:
        print(" 🎉 ALL TESTS PASSED — IPC plumbing is correct!")
    else:
        print(" ⚠️  SOME TESTS FAILED — check output above.")
        sys.exit(1)


if __name__ == "__main__":
    main()
