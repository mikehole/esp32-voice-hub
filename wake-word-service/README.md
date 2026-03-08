# Wake Word Service

Server-side wake word detection for ESP32 Voice Hub using [Picovoice Porcupine](https://picovoice.ai/platform/porcupine/).

## Why Server-Side?

Neither Picovoice nor Espressif's wake word solutions support the ESP32-S3's Xtensa LX7 cores natively. Running wake word detection on the server:
- Simplifies firmware (no ML inference on device)
- Uses Picovoice's accurate, production-ready engine
- Runs on the same machine as OpenClaw

## Architecture

```
ESP32 (IDLE):           WebSocket Server              Porcupine
I2S mic → 32ms chunks → ─────────────────────────── → service.py
                                                          │
                        ←── wake_detected ─────────────── │
                                                          
ESP32 (RECORDING):      WebSocket Server              
I2S mic → full audio →  ─── STT (Whisper) ─── OpenClaw Agent ─── TTS
     ←── TTS audio ───  ────────────────────────────────────────────
```

## Setup

### 1. Get Picovoice Access Key

1. Sign up at https://console.picovoice.ai/
2. Get your free access key (allows 1 custom wake word)

### 2. Install Dependencies

```bash
cd wake-word-service
pip install -r requirements.txt
```

### 3. Set Environment Variables

```bash
export PICOVOICE_ACCESS_KEY="your-key-here"
export OPENAI_API_KEY="sk-..."
export OPENCLAW_URL="http://localhost:3007"  # optional
export OPENCLAW_TOKEN="your-hooks-token"      # optional
```

### 4. Run the Server

```bash
python server.py --port 8765
```

### 5. Configure ESP32

In the web admin panel, set:
- **Wake Word Host:** IP of the machine running server.py
- **Wake Word Port:** 8765 (default)

## Custom Wake Word

The included model `oi_minerva_linux_v4.ppn` was trained at:
https://console.picovoice.ai/ppn

To create a new wake word:
1. Go to Picovoice Console → Porcupine
2. Enter your wake phrase
3. Select "Linux (x86_64)" platform
4. Download the `.ppn` file
5. Replace `models/oi_minerva_linux_v4.ppn`

## WebSocket Protocol

### From ESP32

**Idle audio (continuous):**
- Binary frames, exactly 1024 bytes (512 samples at 16kHz mono 16-bit)
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

## Tuning Sensitivity

Edit `service.py`:

```python
porcupine = pvporcupine.create(
    access_key=ACCESS_KEY,
    keyword_paths=[MODEL_PATH],
    sensitivities=[0.5],  # 0.0-1.0, higher = more sensitive
)
```

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
Environment=PICOVOICE_ACCESS_KEY=your-key
Environment=OPENAI_API_KEY=sk-...
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

- `service.py` — Porcupine wake word subprocess
- `server.py` — Main WebSocket server
- `models/oi_minerva_linux_v4.ppn` — "Oi Minerva" wake word model
