from flask import Flask, request
import csv
import os
import time

app = Flask(__name__)

DATA_DIR = "data"
EVENTS_CSV = os.path.join(DATA_DIR, "events.csv")

def ensure_csv():
    os.makedirs(DATA_DIR, exist_ok=True)
    if not os.path.exists(EVENTS_CSV):
        with open(EVENTS_CSV, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["ts", "ax", "ay", "az", "event"])

@app.route("/")
def home():
    return "OK - IoT server running"

@app.route("/data", methods=["GET"])
def receive_data():
    ensure_csv()

    ts = time.time()
    ax = request.args.get("ax", "0")
    ay = request.args.get("ay", "0")
    az = request.args.get("az", "0")
    event = request.args.get("event", "0")

    with open(EVENTS_CSV, "a", newline="") as f:
        w = csv.writer(f)
        w.writerow([ts, ax, ay, az, event])

    print(f"Received: ts={ts:.3f}, ax={ax}, ay={ay}, az={az}, event={event}")
    return "OK"

if __name__ == "__main__":
    # Listen on all interfaces so Arduino can reach it over WiFi
    app.run(host="0.0.0.0", port=5000, debug=False)
