import zmq, json, time

ctx = zmq.Context()

pub = ctx.socket(zmq.PUB)
pub.bind("ipc:///tmp/alg2emb.ipc")
sub = ctx.socket(zmq.SUB)
sub.bind("ipc:///tmp/emb2alg.ipc")

sub.setsockopt_string(zmq.SUBSCRIBE, "") # Subscribe to all messages

time.sleep(0.5) # Give sockets a moment to bind before sending

print("<MODULE_ALG> Ready")
counter = 0

while True:
    # payload = {
    #     "type":    "motor_command",
    #     "counter": counter,
    #     "speed":   75,
    #     "turn":    0.0
    # }
    # pub.send_string(json.dumps(payload))
    # print(f"[ALGO] Sent: {payload}")
    
    try:
        msg = sub.recv_string(flags=zmq.NOBLOCK)
        data = json.loads(msg)
        print(f"[{counter}][ALG] Received: {data}")
    except zmq.Again:
        print(f"[{counter}][ALG] Nothing back yet")

    counter += 1
    time.sleep(1)