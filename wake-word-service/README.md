# Wake Word Service

Server-side wake word detection for ESP32 Voice Hub using [OpenWakeWord](https://github.com/dscripka/openWakeWord).

## Why Server-Side?

Neither Picovoice nor Espressif's wake word solutions support the ESP32-S3's Xtensa LX7 cores natively. Running wake word detection on the server:
- Simplifies firmware (no ML inference on device)
- Allows using any wake phrase via synthetic training
- Uses the same machine that runs OpenClaw

## Architecture

```
ESP32 (IDLE):           WebSocket Server              Wake Word Service
I2S mic → 80ms chunks → ─────────────────────────── → OpenWakeWord
                                                          │
                        ←── wake_detected ─────────────── │
                                                          
ESP32 (RECORDING):      WebSocket Server              
I2S mic → full audio →  ─── STT (Whisper) ─── OpenClaw Agent ─── TTS
     ←── TTS audio ───  ────────────────────────────────────────────
```

## Setup

### 1. Install dependencies

```bash
cd wake-word-service
pip install -r requirements.txt
```

### 2. Train the wake word model

```bash
python train.py
# Takes 5-15 minutes. Creates models/oi_minerva.tflite
```

### 3. Run the server

```bash
python server.py \
    --port 8765 \
    --openai-api-key YOUR_KEY \
    --openclaw-url http://localhost:3007 \
    --openclaw-token YOUR_HOOKS_TOKEN
```

Or with environment variables:
```bash
export OPENAI_API_KEY=sk-...
export OPENCLAW_URL=http://localhost:3007
export OPENCLAW_TOKEN=your-hooks-token
python server.py --port 8765
```

## WebSocket Protocol

### From ESP32

**Idle audio (continuous):**
- Binary frames, exactly 2560 bytes (80ms at 16kHz mono 16-bit)
- Sent continuously while device is idle

**Recording start:**
```json
{"type": "audio_start", "sampleRate": 16000, "byteLength": 32000}
```

**Recording chunks:**
- Binary frames, 1024 bytes each

**Recording end:**
```json
{"type": "audio_end"}
```

### To ESP32

**Wake word detected:**
```json
{"type": "wake_detected"}
```

**Transcript ready:**
```json
{"type": "transcript", "text": "turn on the lights"}
```

**TTS start:**
```json
{"type": "tts_start", "sampleRate": 16000, "byteLength": 48000}
```

**TTS chunks:**
- Binary frames, 1024 bytes each

**TTS end:**
```json
{"type": "tts_end"}
```

**Error:**
```json
{"type": "error", "message": "Transcription failed"}
```

## Tuning Wake Word Detection

Edit `service.py`:

- **THRESHOLD** (default 0.5): Lower = more sensitive, higher = stricter
- **COOLDOWN_SECONDS** (default 1.0): Time after detection before re-arming

If too many false triggers, raise THRESHOLD to 0.6-0.7.
If detection is too strict, lower to 0.3-0.4.

For better accuracy, increase `num_steps` in `train.py` to 25000 and retrain.

## Running as a Service

### systemd (Linux)

Create `/etc/systemd/system/esp32-wakeword.service`:

```ini
[Unit]
Description=ESP32 Wake Word Server
After=network.target

[Service]
Type=simple
User=youruser
WorkingDirectory=/path/to/wake-word-service
Environment=OPENAI_API_KEY=sk-...
Environment=OPENCLAW_URL=http://localhost:3007
Environment=OPENCLAW_TOKEN=your-token
ExecStart=/usr/bin/python3 server.py --port 8765
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable esp32-wakeword
sudo systemctl start esp32-wakeword
```

## Files

- `train.py` — Train custom wake word model (run once)
- `service.py` — Wake word detection subprocess (spawned by server)
- `server.py` — Main WebSocket server
- `models/` — Trained wake word models (gitignore'd except committed models)
