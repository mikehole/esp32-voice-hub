# ESP32 Voice Hub

A desktop voice assistant built on the Waveshare ESP32-S3-Knob-Touch-LCD-1.8 — featuring expressive avatar states, radial wedge menu UI, and integration with OpenClaw AI.

<p align="center">
  <img src="assets/product-hero.png" alt="ESP32 Voice Hub" width="500"/>
</p>

## ✨ Features

- 🎤 **Tap-to-talk voice assistant** — Whisper STT → OpenClaw AI → OpenAI TTS
- 🦀 **Expressive avatar** — Minerva shows emotions: idle, listening, thinking, speaking, connecting
- 🎛️ **Radial menu navigation** — rotary encoder + touch gestures
- 🔵 **Blue Mono UI** — deep navy background, cyan accents, matches hardware bezel
- 💾 **Conversation memory** — persists chat history to SD card
- 🌐 **Web admin panel** — configure WiFi, API keys, test audio
- 🔊 **Audio output** — onboard speaker + 3.5mm DAC

## 🎭 Avatar States

Minerva (the assistant) has expressive avatar states that change based on what she's doing:

| State | Avatar | Ring Animation |
|-------|--------|----------------|
| **Idle** | Neutral expression | — |
| **Connecting** | ⚡ Electrocuted! | 🔵 Blue spinning |
| **Recording** | 👂 Hand to ear, listening | 🔴 Red pulsing |
| **Thinking** | 🤔 Contemplative (2 variants) | — |
| **Speaking** | ✨ Excited (2 variants) | 💠 Cyan pulsing |

Avatar images are embedded in firmware (~200KB for 7 images).

## 🛠️ Tech Stack

| Layer | Technology |
|-------|------------|
| **Framework** | Arduino + ESP-IDF (pioarduino) |
| **UI Library** | [LVGL](https://lvgl.io/) v8 |
| **Display Driver** | SH8601 (QSPI) via esp_lcd |
| **Touch Driver** | CST816 (I2C) |
| **Audio** | I2S PDM microphone + I2S DAC |
| **Build System** | PlatformIO |
| **AI Backend** | [OpenClaw](https://github.com/openclaw/openclaw) (or direct OpenAI API) |

## 🔧 Hardware

| Component | Details |
|-----------|---------|
| **Board** | [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8) |
| **Display** | 1.8" round LCD, 360×360, SH8601 (QSPI) |
| **Touch** | CST816 capacitive (I2C) |
| **Audio Out** | Onboard speaker + PCM5101A DAC (3.5mm) |
| **Audio In** | Onboard PDM digital microphone |
| **Input** | Rotary encoder with push button |
| **Storage** | SD card slot (for conversation history) |
| **MCU** | ESP32-S3R8 (8MB PSRAM, 16MB Flash) |

## 🚀 Getting Started

### Prerequisites

- [VS Code](https://code.visualstudio.com/)
- [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- USB-C cable
- OpenAI API key (for Whisper + TTS)
- OpenClaw instance or OpenAI API key for chat

### Clone & Build

```bash
git clone https://github.com/mikehole/esp32-voice-hub.git
cd esp32-voice-hub/firmware/platformio

# Build
pio run

# Upload
pio run -t upload
```

### First Boot

1. Device creates WiFi AP: `ESP32-Voice-Hub`
2. Connect and open `http://192.168.4.1/admin`
3. Configure:
   - WiFi credentials
   - OpenAI API key
   - OpenClaw endpoint & token (or leave blank for direct OpenAI)
4. Reboot — device connects to your WiFi

### Using the Device

1. **Select Minerva** (center wedge) using encoder or touch
2. **Tap center** to start recording (she cups her ear to listen)
3. **Tap again** to stop recording
4. Watch her **think** (contemplative pose)
5. Hear her **respond** (excited pose with cyan pulsing ring)

## 📁 Project Structure

```
esp32-voice-hub/
├── firmware/
│   └── platformio/
│       ├── src/
│       │   ├── main.cpp           # Main application
│       │   ├── avatar.cpp/h       # Avatar state management
│       │   ├── avatar_images.h    # Embedded avatar images (RGB565)
│       │   ├── status_ring.cpp/h  # Animated ring effects
│       │   ├── audio_capture.cpp/h # Microphone & speaker
│       │   ├── openai_client.cpp/h # Whisper, TTS, OpenClaw
│       │   ├── conversation.cpp/h  # Chat history (SD card)
│       │   ├── wifi_manager.cpp/h  # WiFi with captive portal
│       │   └── web_admin.cpp/h     # Admin panel
│       └── platformio.ini
├── avatar-poses/                   # Source avatar images
│   └── mike-picks/cropped/         # Processed 130x130 images
├── assets/
│   └── mockups/                    # UI design mockups
├── docs/
│   ├── design.md                   # UI design language
│   └── hardware-notes.md           # Hardware details
└── enclosure/
    └── stl/                        # 3D printable parts
```

## 🎨 Design Language

**Blue Mono** — deep navy background (#0F2744), cyan accents (#5DADE2), 8-wedge Trivial Pursuit layout.

The avatar's circuit traces glow cyan to match the UI accent color.

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Main Loop (Core 1)                    │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Touch/Encoder│  │ LVGL UI     │  │ Status Ring Anim   │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────▼─────────┐
                    │  Avatar State     │ (image swap)
                    └───────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                    Background Task (Core 0)                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Whisper STT │  │ OpenClaw AI │  │ OpenAI TTS          │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

API calls run on Core 0 (FreeRTOS task) so UI stays responsive. Avatar images swap instead of LVGL animations during background processing (LVGL is not thread-safe).

## ⚙️ Configuration

Via web admin panel (`http://<device-ip>/admin`) or NVS storage:

| Setting | Description |
|---------|-------------|
| WiFi SSID/Password | Network credentials |
| OpenAI API Key | For Whisper transcription + TTS |
| OpenClaw Endpoint | e.g., `https://your-server.com` |
| OpenClaw Token | Gateway authentication token |

## 📝 License

MIT — see [LICENSE](LICENSE)

## 🙏 Acknowledgments

- [Waveshare](https://www.waveshare.com/) for the excellent hardware
- [LVGL](https://lvgl.io/) for the UI library
- [OpenClaw](https://github.com/openclaw/openclaw) for the AI gateway
- [pioarduino](https://github.com/pioarduino) for ESP-IDF 5.x PlatformIO support
