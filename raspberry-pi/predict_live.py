import csv
import joblib
from collections import deque

EVENTS = "data/events.csv"
MODEL = "data/model.joblib"

model = joblib.load(MODEL)

def tail_streaming_samples(path, n=20):
    q = deque(maxlen=n)
    with open(path, "r") as f:
        r = csv.DictReader(f)
        for row in r:
            if row.get("event") == "9":
                try:
                    ax = int(float(row["ax"]))
                    ay = int(float(row["ay"]))
                    az = int(float(row["az"]))
                    q.append([ax, ay, az])
                except:
                    pass

    return list(q)

samples = tail_streaming_samples(EVENTS, n=30)

if not samples:
    print("ERROR: No streaming samples (event=9) found in events.csv.")
    raise SystemExit(1)

pred = model.predict(samples)
fall_votes = sum(int(x) for x in pred)
total = len(pred)

print(f"Predictions on last {total} streaming samples:")
print(f"FALL votes: {fall_votes}/{total}")

if fall_votes >= (total * 0.5):
    print("Result: FALL likely (majority vote).")
else:
    print("Result: NORMAL likely (majority vote).")
