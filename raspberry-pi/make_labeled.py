import csv

INPUT = "data/events.csv"
OUTPUT = "data/labeled.csv"

# Label rule:
# - event=9 are streaming samples
# - if a streaming sample is within WINDOW_SEC of a fall event (event=1),
#   label it as 1 (fall). Otherwise label 0 (normal).
WINDOW_SEC = 1.0

with open(INPUT, "r") as f:
  r = csv.DictReader(f)
  rows = list(r)

if not rows:
  print("ERROR: events.csv is empty. Generate data first.")
  raise SystemExit(1)

fall_times = [float(row["ts"]) for row in rows if row.get("event") == "1"]
print(f"Fall events found: {len(fall_times)}")

out = []
stream_count = 0
fall_labeled = 0

for row in rows:
  if row.get("event") != "9":
    continue

  stream_count += 1
  ts = float(row["ts"])
  ax = int(float(row["ax"]))
  ay = int(float(row["ay"]))
  az = int(float(row["az"]))

  label = 0
  for t in fall_times:
    if abs(ts - t) <= WINDOW_SEC:
      label = 1
      break

  if label == 1:
    fall_labeled += 1

  out.append([ax, ay, az, label])

if not out:
  print("ERROR: No streaming samples found (event=9). Turn ON recording mode and try again.")
  raise SystemExit(1)

with open(OUTPUT, "w", newline="") as f:
  w = csv.writer(f)
  w.writerow(["ax", "ay", "az", "label"])
  w.writerows(out)

print(f"Streaming samples: {stream_count}")
print(f"Labeled as FALL (1): {fall_labeled}")
print(f"Labeled as NORMAL (0): {stream_count - fall_labeled}")
print(f"OK: labeled.csv written to {OUTPUT} with {len(out)} rows.")
