import subprocess, sys, time, os

# PIPE_SE = "/tmp/sen_to_emb"   # sensors   -> embedded
# PIPE_EA = "/tmp/emb_to_alg"   # embedded  -> algorithm
# PIPE_AE = "/tmp/alg_to_emb"   # algorithm -> embedded

# # FIFOs
# for p in [PIPE_SE, PIPE_EA, PIPE_AE]:
#     if not os.path.exists(p):
#         os.mkfifo(p)

# MAIN PROCESSES
processes = [
    ["./sensors/main"],
    ["./communication/main"],
    ["./embedded/main"],
    ["python3", "algorithm/R2.py"],
    ["./motors/main"]
]

# DEBUG PROCESSES
# processes = [
#     ["python3", "debug_sender/debug_sender.py"],
#     ["./communication/main"],
# ]


procs = [subprocess.Popen(p) for p in processes]
print("All modules running.")

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    for p in procs:
        p.terminate()
    print("Stopped.")