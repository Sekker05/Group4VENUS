import subprocess, sys, time, os

# PIPE_SE = "/tmp/sen_to_emb"   # sensors   -> embedded
# PIPE_EA = "/tmp/emb_to_alg"   # embedded  -> algorithm
# PIPE_AE = "/tmp/alg_to_emb"   # algorithm -> embedded

# # FIFOs
# for p in [PIPE_SE, PIPE_EA, PIPE_AE]:
#     if not os.path.exists(p):
#         os.mkfifo(p)

processes = [
    ["python3", "sensors/sensors.py"],
    ["python3", "algorithm/algorithm.py"],
    # ["python3", "embedded/embedded.py"],
    ["./embedded/main"]
]

procs = [subprocess.Popen(p) for p in processes]
print("All modules running.")

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    for p in procs:
        p.terminate()
    print("Stopped.")