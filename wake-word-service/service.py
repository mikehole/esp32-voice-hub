#!/usr/bin/env python3
"""
Wake Word Detection Service for ESP32 Voice Hub.

Reads raw 16kHz mono 16-bit PCM from stdin in 80ms chunks (1280 samples = 2560 bytes).
Writes "WAKE\n" to stdout when "Oi Minerva" is detected.
The OpenClaw plugin reads stdout and sends wake_detected to the ESP32.

Protocol:
    stdin:  Raw PCM bytes (2560 bytes per chunk, continuous)
    stdout: "WAKE\n" when wake word detected
    stderr: Debug/status logging

Usage (standalone test):
    # Generate test tone and pipe to service
    python -c "import sys; sys.stdout.buffer.write(b'\\x00' * 2560 * 100)" | python service.py

    # Or with actual audio file (must be 16kHz mono 16-bit PCM)
    ffmpeg -i test.wav -f s16le -ar 16000 -ac 1 - | python service.py
"""

import sys
import os
import numpy as np

# Model path - relative to this script
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(SCRIPT_DIR, "models", "oi_minerva.tflite")

# OpenWakeWord expects exactly 80ms chunks at 16kHz
CHUNK_SAMPLES = 1280        # 80ms at 16kHz
BYTES_PER_CHUNK = CHUNK_SAMPLES * 2  # 16-bit = 2 bytes per sample

# Detection threshold — tune based on testing:
# - Lower (0.3-0.4): More sensitive, may have false triggers
# - Higher (0.6-0.8): More strict, may miss some detections
THRESHOLD = 0.5

# Cooldown after detection (seconds) — prevents double-triggers
COOLDOWN_SECONDS = 1.0
COOLDOWN_BYTES = int(16000 * 2 * COOLDOWN_SECONDS)


def main():
    # Check if model exists
    if not os.path.exists(MODEL_PATH):
        sys.stderr.write(f"[WakeWord] ERROR: Model not found at {MODEL_PATH}\n")
        sys.stderr.write("[WakeWord] Run 'python train.py' first to generate the model.\n")
        sys.stderr.flush()
        sys.exit(1)
    
    # Import openwakeword here so we get a clear error if model is missing first
    from openwakeword.model import Model
    
    sys.stderr.write(f"[WakeWord] Loading model from {MODEL_PATH}\n")
    sys.stderr.flush()
    
    model = Model(wakeword_models=[MODEL_PATH], inference_framework="tflite")
    
    sys.stderr.write("[WakeWord] Service ready — listening for 'Oi Minerva'\n")
    sys.stderr.flush()
    
    while True:
        # Read exactly one chunk (blocking)
        data = sys.stdin.buffer.read(BYTES_PER_CHUNK)
        
        if len(data) < BYTES_PER_CHUNK:
            # stdin closed or EOF — plugin is shutting down
            sys.stderr.write("[WakeWord] stdin closed, exiting\n")
            sys.stderr.flush()
            break
        
        # Convert to float32 normalized [-1, 1] for model
        audio = np.frombuffer(data, dtype=np.int16).astype(np.float32) / 32768.0
        
        # Run inference
        prediction = model.predict(audio)
        
        # OpenWakeWord normalizes model names — "Oi Minerva" becomes "oi_minerva"
        score = prediction.get("oi_minerva", 0.0)
        
        if score >= THRESHOLD:
            # Wake word detected!
            sys.stdout.write("WAKE\n")
            sys.stdout.flush()
            
            sys.stderr.write(f"[WakeWord] Detected! (score={score:.3f})\n")
            sys.stderr.flush()
            
            # Cooldown: drain audio to prevent immediate re-trigger
            # This also gives time for the user to start speaking their command
            drained = 0
            while drained < COOLDOWN_BYTES:
                chunk = sys.stdin.buffer.read(min(BYTES_PER_CHUNK, COOLDOWN_BYTES - drained))
                if len(chunk) == 0:
                    break
                drained += len(chunk)


if __name__ == "__main__":
    main()
