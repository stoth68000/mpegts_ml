import json
import numpy as np
import tensorflow as tf
import keras
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from joblib import dump

# Load data
with open("uc01-training.json") as f:
    records = json.load(f)

feature_names = [
    "day_of_week", "hour", "minute", "second", "unixtime",
    "avc_ibp_total_slice_count", "avc_ibp_total_slice_size",
    "transport_bit_count", "i_count", "p_count", "b_count"
]

X = np.array([[r[fn] for fn in feature_names] for r in records], dtype=np.float32)
y = np.array([r["on_air"] for r in records], dtype=np.float32)

# Split and scale
X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)
scaler = StandardScaler()
X_train = scaler.fit_transform(X_train)
X_test = scaler.transform(X_test)

# Build model
model = tf.keras.Sequential([
    tf.keras.Input(shape=(X.shape[1],)),
    tf.keras.layers.Dense(32, activation='relu'),
    tf.keras.layers.Dense(16, activation='relu'),
    tf.keras.layers.Dense(1, activation='sigmoid')
])

model.compile(optimizer='adam', loss='binary_crossentropy', metrics=['accuracy'])
model.fit(X_train, y_train, epochs=20, batch_size=8, validation_data=(X_test, y_test))

# Save model and scaler
keras.saving.save_model(model, "uc01-model-on_air_classifier.keras")
dump(scaler, "scaler.joblib")

print("Model and scaler saved.")

