#!/usr/bin/env python3
"""
Wake Word Detection Service for ESP32 Voice Hub.
Uses Picovoice Porcupine for "Oi Minerva" detection.

Reads raw 16kHz mono 16-bit PCM from stdin.
Writes "WAKE\n" to stdout when "Oi Minerva" is detected.
The server.py reads stdout and sends wake_detected to the ESP32.

Protocol:
    stdin:  Raw PCM bytes (512 samples = 1024 bytes per frame at 16kHz)
    stdout: "WAKE\n" when wake word detected
    stderr: Debug/status logging

Requires:
    pip install pvporcupine

Usage (standalone test):
    export PICOVOICE_ACCESS_KEY="your-key-here"
    python -c "import sys; sys.stdout.buffer.write(b'\\x00' * 1024 * 100)" | python service.py
"""

import sys
import os
import struct
import time

# Picovoice requires an access key (free tier available)
ACCESS_KEY = os.environ.get("PICOVOICE_ACCESS_KEY", "")

# Model path - relative to this script
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(SCRIPT_DIR, "models", "oi_minerva_linux_v4.ppn")

# Cooldown after detection (seconds) — prevents double-triggers
COOLDOWN_SECONDS = 1.5


def main():
    if not ACCESS_KEY:
        sys.stderr.write("[WakeWord] ERROR: PICOVOICE_ACCESS_KEY environment variable required\n")
        sys.stderr.write("[WakeWord] Get a free key at: https://console.picovoice.ai/\n")
        sys.stderr.flush()
        sys.exit(1)

    if not os.path.exists(MODEL_PATH):
        sys.stderr.write(f"[WakeWord] ERROR: Model not found at {MODEL_PATH}\n")
        sys.stderr.flush()
        sys.exit(1)

    # Import here so we get clear error messages above first
    import pvporcupine

    sys.stderr.write(f"[WakeWord] Loading Porcupine model from {MODEL_PATH}\n")
    sys.stderr.flush()

    try:
        porcupine = pvporcupine.create(
            access_key=ACCESS_KEY,
            keyword_paths=[MODEL_PATH],
            sensitivities=[0.5],  # 0.0-1.0, higher = more sensitive
        )
    except pvporcupine.PorcupineActivationError as e:
        sys.stderr.write(f"[WakeWord] ERROR: Invalid access key: {e}\n")
        sys.stderr.flush()
        sys.exit(1)
    except pvporcupine.PorcupineError as e:
        sys.stderr.write(f"[WakeWord] ERROR: Porcupine init failed: {e}\n")
        sys.stderr.flush()
        sys.exit(1)

    # Porcupine expects exactly frame_length samples per call
    frame_length = porcupine.frame_length  # Usually 512 samples
    sample_rate = porcupine.sample_rate    # Should be 16000
    bytes_per_frame = frame_length * 2     # 16-bit = 2 bytes per sample

    sys.stderr.write(f"[WakeWord] Porcupine ready — frame_length={frame_length}, sample_rate={sample_rate}\n")
    sys.stderr.write("[WakeWord] Listening for 'Oi Minerva'...\n")
    sys.stderr.flush()

    last_detection_time = 0

    try:
        while True:
            # Read exactly one frame
            data = sys.stdin.buffer.read(bytes_per_frame)

            if len(data) < bytes_per_frame:
                # stdin closed or EOF
                sys.stderr.write("[WakeWord] stdin closed, exiting\n")
                sys.stderr.flush()
                break

            # Convert bytes to int16 array
            pcm = struct.unpack(f'{frame_length}h', data)

            # Process frame
            keyword_index = porcupine.process(pcm)

            if keyword_index >= 0:
                now = time.time()
                if now - last_detection_time > COOLDOWN_SECONDS:
                    # Wake word detected!
                    sys.stdout.write("WAKE\n")
                    sys.stdout.flush()

                    sys.stderr.write("[WakeWord] Detected 'Oi Minerva'!\n")
                    sys.stderr.flush()

                    last_detection_time = now

    except KeyboardInterrupt:
        pass
    finally:
        porcupine.delete()
        sys.stderr.write("[WakeWord] Shutdown complete\n")
        sys.stderr.flush()


if __name__ == "__main__":
    main()
