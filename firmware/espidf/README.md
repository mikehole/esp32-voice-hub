# ESP32 Voice Hub — ESP-IDF Firmware

Pure ESP-IDF 5.5 implementation of the Voice Hub firmware, featuring:

- 🎤 **Wake word detection** — "Hi ESP" using Espressif's ESP-SR
- 🗣️ **Voice assistant** — WebSocket streaming to OpenClaw plugin
- 🎛️ **Wedge UI** — 8-segment radial menu with nested submenus
- 🔄 **OTA updates** — Over-the-air firmware updates via HTTP
- 📸 **Screenshot API** — Capture display for debugging

## Building

### Prerequisites

- ESP-IDF v5.5+
- Python 3.8+

### Setup

```bash
# Clone ESP-IDF (if not already installed)
git clone -b v5.5.3 --recursive https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf
./install.sh esp32s3

# Source environment
source ~/esp-idf/export.sh
```

### Build & Flash

```bash
cd firmware/espidf

# Build
idf.py build

# Flash via USB
idf.py -p /dev/ttyUSB0 flash monitor

# Or OTA (device must be in OTA mode)
curl -X POST --data-binary "@build/esp32_voice_hub.bin" \
  "http://192.168.1.224/api/ota/upload" \
  -H "Content-Type: application/octet-stream"
```

## Configuration

WiFi and server settings are in `main/main.c`:

```c
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"
#define WS_SERVER "ws://192.168.1.223:8765"  // OpenClaw plugin WebSocket
```

## UI Navigation

### Main Menu

| Wedge | Function |
|-------|----------|
| Minerva | Voice assistant (tap center to talk) |
| Music | 🚧 Placeholder |
| Home | 🚧 Placeholder |
| Weather | 🚧 Placeholder |
| News | 🚧 Placeholder |
| Timer | 🚧 Placeholder |
| Zoom | 🚧 Placeholder |
| Settings | Opens settings submenu |

### Settings Submenu

| Wedge | Function |
|-------|----------|
| < Back | Return to main menu |
| OTA | Pause wake word for OTA updates |
| Wake | Toggle wake word detection on/off |
| Bright | Adjust display brightness (use encoder) |
| Volume | Adjust audio volume (use encoder) |
| WiFi | 🚧 Placeholder |
| About | 🚧 Placeholder |
| Restart | Reboot device |

### Controls

- **Rotary encoder**: Navigate wedges / adjust values
- **Touch wedge**: Select that wedge
- **Touch center**: Activate selected wedge

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Device info (version, IP, heap, etc.) |
| `/api/ota/upload` | POST | Upload firmware binary |
| `/api/play` | POST | Play raw PCM audio |
| `/api/screenshot` | GET | Capture display as BMP |
| `/api/notify` | POST | Show notification with message |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Main Task (Core 1)                       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Touch       │  │ LVGL UI     │  │ Encoder Polling     │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                   Background Tasks                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Wakeword    │  │ WebSocket   │  │ Audio Playback      │  │
│  │ Detection   │  │ Voice Client│  │ (I2S DAC)          │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

| File | Purpose |
|------|---------|
| `main.c` | Entry point, WiFi init, component startup |
| `display.c` | LVGL setup, SH8601 driver, screenshot capture |
| `wedge_ui.c` | Radial menu, nested menus, adjustment mode |
| `voice_client.c` | WebSocket connection, audio streaming |
| `wakeword.c` | ESP-SR wake word detection ("Hi ESP") |
| `audio.c` | I2S mic recording + DAC playback |
| `encoder.c` | Rotary encoder with accumulate+poll pattern |
| `web_server.c` | HTTP API endpoints |
| `ota_update.c` | OTA firmware updates |

### Thread Safety

LVGL is **not thread-safe**. Any code touching LVGL objects from outside the LVGL task must use:

```c
if (display_lock(timeout_ms)) {
    // LVGL operations here
    display_unlock();
}
```

The encoder task and WebSocket callbacks both use this pattern.

## Voice Flow

1. **Wake word** — "Hi ESP" detected by ESP-SR
2. **Recording** — Audio captured to PSRAM buffer (max 10s)
3. **VAD** — Silence detection stops recording after 1.2s quiet
4. **Upload** — PCM sent via WebSocket to OpenClaw plugin
5. **Response** — Plugin sends transcript → agent → TTS audio
6. **Playback** — Audio streamed back and played via I2S DAC

## OTA Updates

The wakeword AFE task is CPU-intensive and can starve the HTTP server during large uploads. To update safely:

1. Navigate to Settings → OTA
2. Tap center to enter OTA mode (pauses wake word)
3. Upload firmware via `/api/ota/upload`
4. Device reboots automatically on success

## Differences from PlatformIO Version

| Feature | PlatformIO | ESP-IDF |
|---------|------------|---------|
| Framework | Arduino + ESP-IDF | Pure ESP-IDF |
| Wake word | None (tap only) | ESP-SR "Hi ESP" |
| Voice backend | Direct OpenAI API | WebSocket to plugin |
| Settings UI | Web admin only | On-device wedge menu |
| Build system | PlatformIO | idf.py |

## License

MIT — see [LICENSE](../../LICENSE)
