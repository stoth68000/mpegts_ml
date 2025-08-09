import os
import numpy as np
import tensorflow as tf
from joblib import load

# Define feature order
feature_names = [
#   "day_of_week", "hour", "minute", "second",
#   "unixtime",
    "avc_ibp_total_slice_count", "avc_ibp_total_slice_size",
    "transport_bit_count", "i_count", "p_count", "b_count"
]

# Input record for prediction
new_record = {
#   "day_of_week": 6,
#   "hour": 8,
#   "minute": 10,
#   "second": 32,
#   "unixtime": 1754742475,
    "avc_ibp_total_slice_count": 60,
    "avc_ibp_total_slice_size":   700728,
    "transport_bit_count":      20266400,
    "i_count": 1,
    "p_count": 14,
    "b_count": 45
}

# Prepare input array
X_new = np.array([[new_record[fn] for fn in feature_names]], dtype=np.float32)

# Check for model and scaler files
model_path = "uc01-model-on_air_classifier.keras"
scaler_path = "uc01-scaler.joblib"

if not os.path.exists(model_path):
    raise FileNotFoundError(f"Model file not found: {model_path}. Please train the model first.")

if not os.path.exists(scaler_path):
    raise FileNotFoundError(f"Scaler file not found: {scaler_path}. Run train_model.py to generate it.")

# Load model and scaler
model = tf.keras.models.load_model(model_path)
scaler = load(scaler_path)

# Scale input and predict
X_new_scaled = scaler.transform(X_new)
proba = model.predict(X_new_scaled)[0][0]
pred = proba >= 0.5

print(f"Predicted probability of on_air=True: {proba:.4f}")
print(f"Predicted label: {'on_air' if pred else 'off_air'}")
