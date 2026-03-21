# ═══════════════════════════════════════════════════════════════════
# RUN ONCE ON LAPTOP, PRODUCE .h5 file
#  FALL PREDICTOR — LSTM Training Script
#  Run: pip install tensorflow numpy pandas scikit-learn
#  Then: python train_fall_model.py
# ═══════════════════════════════════════════════════════════════════

import numpy as np
import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
import tensorflow as tf
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import LSTM, Dense, Dropout
from tensorflow.keras.callbacks import EarlyStopping
import pickle

# ── CONFIG ────────────────────────────────────────────────────────
WINDOW_SIZE = 50     # 50 samples × 10ms = 500ms prediction window
FEATURES    = 6      # ax, ay, az, gx, gy, gz
BATCH_SIZE  = 32
EPOCHS      = 50

# ─────────────────────────────────────────────────────────────────
#  STEP 1: GENERATE SYNTHETIC TRAINING DATA
#  For your hackathon, this simulates normal walking vs fall events.
#  Replace with real recordings if you have time (see note below).
# ─────────────────────────────────────────────────────────────────
def generate_synthetic_data(n_samples=2000):
    """
    Returns X: (n_samples, WINDOW_SIZE, FEATURES)
             y: (n_samples,)  — 0=stable, 1=fall
    """
    X, y = [], []
    for _ in range(n_samples):
        is_fall = np.random.rand() > 0.7   # 30% falls, 70% normal

        if is_fall:
            # Fall signature: spike in accel + large gyro at some point in window
            window = np.random.normal(0, 0.5, (WINDOW_SIZE, FEATURES))
            spike_at = np.random.randint(30, 45)  # spike near end of window
            window[spike_at:, :3] *= np.random.uniform(8, 15)  # accel spike
            window[spike_at:, 3:] *= np.random.uniform(5, 10)  # gyro spike
            label = 1
        else:
            # Normal walking: low-amplitude rhythmic noise
            t = np.linspace(0, 2*np.pi, WINDOW_SIZE)
            window = np.column_stack([
                0.3 * np.sin(t) + np.random.normal(0, 0.1, WINDOW_SIZE),  # ax
                0.2 * np.sin(t + 0.5) + np.random.normal(0, 0.1, WINDOW_SIZE),
                9.81 + np.random.normal(0, 0.2, WINDOW_SIZE),              # az (gravity)
                np.random.normal(0, 5, WINDOW_SIZE),                       # gx
                np.random.normal(0, 5, WINDOW_SIZE),
                np.random.normal(0, 5, WINDOW_SIZE),
            ])
            label = 0

        X.append(window)
        y.append(label)

    return np.array(X), np.array(y)

# ─────────────────────────────────────────────────────────────────
#  NOTE: To use REAL data instead of synthetic:
#  Download SisFall dataset: http://sistemic.udea.edu.co/en/research/projects/english-falls/
#  Or record your own:
#    1. Wear IMU, walk normally for 5 min → label all windows as 0
#    2. Quickly lurch/stumble (safely!) → label those windows as 1
#    3. Load your CSV and replace the call below with your real data
# ─────────────────────────────────────────────────────────────────

print("Generating training data...")
X, y = generate_synthetic_data(n_samples=3000)

# ── STEP 2: NORMALIZE ─────────────────────────────────────────────
# Reshape to 2D for scaler, then back to 3D
n, w, f = X.shape
X_2d = X.reshape(n * w, f)
scaler = StandardScaler()
X_scaled = scaler.fit_transform(X_2d).reshape(n, w, f)

# Save scaler — you'll need its mean/std to normalize live ESP32 data
with open('scaler.pkl', 'wb') as file:
    pickle.dump(scaler, file)
print(f"Scaler saved. Mean: {scaler.mean_}, Std: {scaler.scale_}")
print("→ Copy these values into ACCEL_MEAN/STD in your ESP32 code")

# ── STEP 3: SPLIT ─────────────────────────────────────────────────
X_train, X_test, y_train, y_test = train_test_split(
    X_scaled, y, test_size=0.2, random_state=42, stratify=y
)

# ── STEP 4: BUILD LSTM ────────────────────────────────────────────
model = Sequential([
    LSTM(64, input_shape=(WINDOW_SIZE, FEATURES), return_sequences=True),
    Dropout(0.3),
    LSTM(32),
    Dropout(0.3),
    Dense(16, activation='relu'),
    Dense(1, activation='sigmoid')   # 0=safe, 1=fall
])
model.compile(
    optimizer='adam',
    loss='binary_crossentropy',
    metrics=['accuracy', tf.keras.metrics.AUC(name='auc')]
)
model.summary()

# ── STEP 5: TRAIN ─────────────────────────────────────────────────
early_stop = EarlyStopping(
    monitor='val_auc', patience=8, mode='max',
    restore_best_weights=True
)
history = model.fit(
    X_train, y_train,
    validation_data=(X_test, y_test),
    epochs=EPOCHS,
    batch_size=BATCH_SIZE,
    callbacks=[early_stop]
)

# ── STEP 6: EVALUATE ──────────────────────────────────────────────
loss, acc, auc = model.evaluate(X_test, y_test, verbose=0)
print(f"\n✅ Test accuracy: {acc:.1%}  AUC: {auc:.3f}")
print("   AUC > 0.85 = good enough for a demo")

# ── STEP 7: SAVE ──────────────────────────────────────────────────
model.save('fall_model.h5')
print("Model saved to fall_model.h5")
print("Next step: load this on a Raspberry Pi or use EloquentML to port to ESP32")
