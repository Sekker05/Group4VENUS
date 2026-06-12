# TEST MODULE
import zmq, json, time

ctx = zmq.Context()

pub = ctx.socket(zmq.PUB)
pub.bind("ipc:///tmp/sen2emb.ipc")
# sub = ctx.socket(zmq.SUB)
# sub.bind("ipc:///tmp/emb2alg.ipc")

# sub.setsockopt_string(zmq.SUBSCRIBE, "")       # subscribe to all messages

time.sleep(0.5)  # give sockets a moment to bind before sending

print("<MODULE_SEN> Ready")
counter = 0

while True:
    # Send
    payload = {
        "distance":1 + counter,
        "temperature":1 + counter,
        "color_front":"red",
        "color_ground":"black",
        "tape":True
    }
    pub.send_string(json.dumps(payload))
    print(f"[{counter}][SEN] Sent: {payload}")

    counter += 1
    time.sleep(1)