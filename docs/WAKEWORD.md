# Wake Word Detection — "Oi Minerva"

This document describes the server-side wake word detection system for ESP32 Voice Hub.

## Why Server-Side?

Neither Picovoice nor Espressif's wake word solutions support the ESP32-S3's Xtensa LX7 cores natively. Running wake word detection on the server:

- **Simplifies firmware** — no ML inference on device
- **Custom wake phrases** — train any phrase via synthetic data
- **Better accuracy** — full Python ML stack with larger models
- **Same machine** — runs on your OpenClaw host

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        ESP32 Voice Hub                           │
│                                                                   │
│  IDLE STATE:                                                     │
│  ┌──────────┐    80ms PCM chunks    ┌──────────────────┐        │
│  │ I2S Mic  │ ─────────────────────→│ WebSocket Client │        │
│  └──────────┘                        └────────┬─────────┘        │
│                                               │                  │
│  When wake_detected received:                 │                  │
│  ┌──────────┐    play 880Hz beep             │                  │
│  │ I2S DAC  │ ←───────────────────────────────│                  │
│  └──────────┘                                 │                  │
│                                               │                  │
│  RECORDING STATE:                             │                  │
│  ┌──────────┐    full utterance PCM   ┌──────┴─────────┐        │
│  │ I2S Mic  │ ───────────────────────→│ WebSocket TX   │        │
│  └──────────┘                          └───────────────┘        │
│                                               │                  │
│  TTS PLAYBACK:                                │                  │
│  ┌──────────┐    TTS PCM chunks      ┌───────┴──────────┐       │
│  │ I2S DAC  │ ←──────────────────────│ WebSocket RX     │       │
│  └──────────┘                         └─────────────────┘       │
└─────────────────────────────────────────────────────────────────┘
                              │ WebSocket
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Wake Word Server (Python)                     │
│                                                                   │
│  ┌───────────────┐                                              │
│  │ server.py     │ ←── Handles WebSocket connections            │
│  │               │                                               │
│  │  IDLE:        │     stdin    ┌───────────────┐               │
│  │  Audio ───────┼────────────→ │ service.py    │               │
│  │               │              │ OpenWakeWord  │               │
│  │               │    "WAKE"    │               │               │
│  │  Detection ←──┼──────────────┤ (subprocess)  │               │
│  │               │     stdout   └───────────────┘               │
│  │               │                                               │
│  │  RECORDING:   │                                              │
│  │  Audio ───────┼─→ Whisper STT ─→ OpenClaw ─→ TTS            │
│  │  Response ←───┼────────────────────────────────              │
│  └───────────────┘                                              │
└─────────────────────────────────────────────────────────────────┘
```

## Setup

### 1. Install Python Dependencies

```bash
cd wake-word-service
pip install -r requirements.txt
```

### 2. Train the Wake Word Model

```bash
python train.py
# Creates models/oi_minerva.tflite (5-15 minutes)
```

### 3. Start the Server

```bash
python server.py \
    --port 8765 \
    --openai-api-key $OPENAI_API_KEY \
    --openclaw-url http://localhost:3007 \
    --openclaw-token $OPENCLAW_TOKEN
```

### 4. Configure ESP32

In the web admin panel, set:
- **Wake Word Host:** IP of the machine running server.py
- **Wake Word Port:** 8765 (default)

The ESP32 will automatically connect when WiFi is available.

## Flow

1. **Idle Streaming:** ESP32 continuously streams 80ms audio chunks via WebSocket
2. **Wake Detection:** Server pipes audio to OpenWakeWord subprocess
3. **Wake Detected:** Server sends `{"type":"wake_detected"}` to ESP32
4. **Ack Beep:** ESP32 plays 880Hz confirmation tone
5. **Recording:** ESP32 records user's command, sends via WebSocket
6. **Processing:** Server transcribes (Whisper), sends to OpenClaw, gets TTS
7. **Response:** Server streams TTS audio back to ESP32
8. **Playback:** ESP32 plays TTS response
9. **Return to Idle:** ESP32 resumes wake word streaming

## WebSocket Protocol

See `wake-word-service/README.md` for full protocol documentation.

## Fallback

If the wake word server is unavailable, tap-to-talk still works using the original HTTP hook method. The firmware automatically detects connection status and falls back.

## Tuning

### Sensitivity

Edit `wake-word-service/service.py`:

```python
THRESHOLD = 0.5  # Lower = more sensitive, higher = stricter
```

### Cooldown

```python
COOLDOWN_SECONDS = 1.0  # Time after detection before re-arming
```

### Better Accuracy

Increase training steps in `train.py`:

```python
num_steps=25000  # Default is 10000
```

Then retrain the model.

## Files

### Firmware

- `ws_client.h/cpp` — WebSocket client wrapper
- `wakeword_integration.h/cpp` — High-level integration layer
- `audio_capture.h/cpp` — Added idle streaming functions

### Server

- `wake-word-service/server.py` — Main WebSocket server
- `wake-word-service/service.py` — OpenWakeWord subprocess
- `wake-word-service/train.py` — Model training script
- `wake-word-service/models/` — Trained wake word models
