import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import classification_report, confusion_matrix, accuracy_score
import joblib

DATA = "data/labeled.csv"
MODEL_OUT = "data/model.joblib"

df = pd.read_csv(DATA)

if df.shape[0] < 10:
    print("ERROR: Not enough labeled data to train (need at least ~10 rows).")
    print(f"Rows found: {df.shape[0]}")
    raise SystemExit(1)

X = df[["ax", "ay", "az"]]
y = df["label"]

# Stratify helps if you have imbalanced classes, but it requires both classes to exist
if y.nunique() < 2:
    print("ERROR: Only one class found in labels. You need both NORMAL (0) and FALL (1) samples.")
    print("Tip: record streaming (event=9) and also trigger at least one fall event (event=1).")
    raise SystemExit(1)

X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.3, random_state=42, stratify=y
)

model = RandomForestClassifier(
    n_estimators=200,
    random_state=42
)

model.fit(X_train, y_train)

pred = model.predict(X_test)

print("Accuracy:", accuracy_score(y_test, pred))
print("\nConfusion matrix:\n", confusion_matrix(y_test, pred))
print("\nClassification report:\n", classification_report(y_test, pred))

joblib.dump(model, MODEL_OUT)
print(f"\nOK: Model saved to {MODEL_OUT}")
