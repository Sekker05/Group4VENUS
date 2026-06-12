import zmq, json, time

ctx = zmq.Context()

pub = ctx.socket(zmq.PUB)
pub.bind("ipc:///tmp/emb2alg.ipc")
sub = ctx.socket(zmq.SUB)
sub.bind("ipc:///tmp/alg2emb.ipc")

sub.setsockopt_string(zmq.SUBSCRIBE, "")       # subscribe to all messages

time.sleep(0.5)  # give sockets a moment to bind before sending
counter = 0
print("<MODULE_EMB> Ready")

while True:
    payload = {
        "type":    "sensor_data",
        "counter": counter,
        "speed":   75,
        "turn":    0.0
    }
    pub.send_string(json.dumps(payload))
    print(f"[ALGO] Sent: {payload}")

    # Non-blocking receive — same as dontwait in C++
    try:
        msg = sub.recv_string(flags=zmq.NOBLOCK)
        data = json.loads(msg)
        print(f"[ALGO] Received: {data}")
    except zmq.Again:
        print("[ALGO] Nothing back yet")

    counter += 1
    time.sleep(1)