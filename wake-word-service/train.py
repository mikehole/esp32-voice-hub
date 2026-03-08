#!/usr/bin/env python3
"""
Train a custom wake word model for "Oi Minerva" using OpenWakeWord.

Uses synthetic data generation — no audio recordings needed.
Run once, then commit models/oi_minerva.tflite to the repo.

Usage:
    cd wake-word-service
    pip install -r requirements.txt
    python train.py

Takes 5-15 minutes on CPU. For better accuracy, increase num_steps to 25000.
"""

import os
from openwakeword.train import train_model

# Create output directory
os.makedirs("./models", exist_ok=True)

print("Training wake word model for 'Oi Minerva'...")
print("This will take 5-15 minutes on CPU.")
print()

train_model(
    target_phrase="Oi Minerva",
    output_dir="./models",
    num_steps=10000,        # Increase to 25000 for better accuracy
    learning_rate=1e-3,
    batch_size=64,
    device="cpu",           # Change to "cuda" if GPU available
)

print()
print("✓ Model saved to ./models/oi_minerva.tflite")
print("  Commit this file to the repo so it doesn't need retraining.")
