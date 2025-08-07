import json
import numpy as np
import tensorflow as tf
from sklearn.preprocessing import StandardScaler

# Define the feature names in correct order
feature_names = [
    "day_of_week", "hour", "minute", "second", "unixtime",
    "avc_ibp_total_slice_count", "avc_ibp_total_slice_size",
    "transport_bit_count", "i_count", "p_count", "b_count"
]

# Load saved model
model = tf.keras.models.load_model("uc01-model-on_air_classifier.keras")

# Load the same scaler used during training (optional: persist with joblib)
scaler = StandardScaler()

# Refit or reuse scaler based on your original training data
# For demo purposes, here we refit using same method:
with open("uc01-training.json") as f:
    train_data = json.load(f)

X_train = np.array([[r[fn] for fn in feature_names] for r in train_data], dtype=np.float32)
scaler.fit(X_train)

# New data for prediction
new_record = {
    "day_of_week": 4,
    "hour": 7,
    "minute": 9,
    "second": 32,
    "unixtime": 1754564972,
    "avc_ibp_total_slice_count": 46,
    "avc_ibp_total_slice_size": 15120336,
    "transport_bit_count": 19592608,
    "i_count": 1,
    "p_count": 15,
    "b_count": 30
}

# Convert and scale input
X_new = np.array([[new_record[fn] for fn in feature_names]], dtype=np.float32)
X_new_scaled = scaler.transform(X_new)

# Predict
proba = model.predict(X_new_scaled)[0][0]
pred = proba >= 0.5

print(f"Predicted probability of on_air=True: {proba:.4f}")
print(f"Predicted label: {'on_air' if pred else 'off_air'}")

