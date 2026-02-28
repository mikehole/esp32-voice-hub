# ESP32 Voice Hub

A desktop voice assistant built on the Waveshare ESP32-S3-Knob-Touch-LCD-1.8 — featuring a radial menu UI, push-to-talk voice interaction, and a warm Nordic design language.

![UI Mockup](assets/mockups/ui-v5a-blue-mono.svg)

## Features

- 🎤 **Push-to-talk voice assistant** — Whisper STT → AI → TTS
- 🎛️ **Radial menu navigation** — rotary encoder + touch gestures
- 🌅 **Organic Nordic UI** — warm amber accents, soft shapes, breathing room
- 🔊 **External speaker output** — 3.5mm DAC with PCM5101A
- 📳 **Haptic feedback** — DRV2605 vibration motor
- 🖨️ **3D printable enclosure** — custom stand/dock designs

## Hardware

| Component | Details |
|-----------|---------|
| **Board** | [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8) |
| **Display** | 1.8" round LCD, 360×360, touch (CST816) |
| **Audio Out** | PCM5101A DAC, 3.5mm jack |
| **Audio In** | Onboard microphone |
| **Input** | Rotary encoder with push button |
| **Haptics** | DRV2605 vibration motor |
| **MCU** | ESP32-S3 (8MB PSRAM, 16MB Flash) |

## Getting Started

### Prerequisites

- Python 3.10+
- [esptool](https://github.com/espressif/esptool) for flashing
- USB-C cable

### Flash Firmware

```bash
# Coming soon — MicroPython + LVGL firmware
```

### Connect to REPL

```bash
# Coming soon
```

## Project Structure

```
esp32-voice-hub/
├── firmware/
│   ├── micropython/    # MicroPython + LVGL code
│   └── platformio/     # C++ fallback (ESP-IDF/Arduino)
├── assets/
│   ├── icons/          # SVG source icons
│   └── mockups/        # UI design mockups
├── enclosure/
│   └── stl/            # 3D printable parts
└── docs/
    └── design.md       # UI design language documentation
```

## Design Language

**Organic Nordic Minimal** — warm charcoal background, amber/sun selection accents, soft organic shapes, generous whitespace.

See [docs/design.md](docs/design.md) for full palette and guidelines.

## Roadmap

- [x] Hardware selection
- [x] UI design mockups
- [ ] Flash LVGL MicroPython firmware
- [ ] Display + knob proof of concept
- [ ] Audio loopback test (mic → speaker)
- [ ] Radial menu implementation
- [ ] Voice assistant integration
- [ ] 3D printed enclosure

## License

MIT — see [LICENSE](LICENSE)

## Contributing

This is an early-stage personal project, but PRs and ideas are welcome!
