import zmq, json, time

ctx = zmq.Context()

pub = ctx.socket(zmq.PUB)
pub.bind("ipc:///tmp/sen2emb.ipc")
# sub = ctx.socket(zmq.SUB)
# sub.bind("ipc:///tmp/emb2alg.ipc")

# sub.setsockopt_string(zmq.SUBSCRIBE, "")       # subscribe to all messages

time.sleep(0.5)  # give sockets a moment to bind before sending

# print("<MODULE_SEN> Not implemented yet")
print("<MODULE_SEN> Ready")
counter = 0

while True:
    payload = {
        "distance":118,
        "temperature":20,
        "color_front":"red",
        "color_ground":"black"
    }
    pub.send_string(json.dumps(payload))
    print(f"[{counter}][SEN] Sent: {payload}")

    # try:
    #     msg = sub.recv_string(flags=zmq.NOBLOCK)
    #     data = json.loads(msg)
    #     print(f"[ALGO] Received: {data}")
    # except zmq.Again:
    #     print("[ALGO] Nothing back yet")

    counter += 1
    time.sleep(1)

distance = 100      # Milimeters
temperature = 20    # Celcius Degrees
color_ground
color_front