# Hardware Notes: Waveshare ESP32-S3-Knob-Touch-LCD-1.8

Technical notes from bringing up the hardware.

## Dual MCU Architecture

This board has **two microcontrollers** sharing one USB-C port:

| MCU | Model | Purpose | COM Port |
|-----|-------|---------|----------|
| **Main** | ESP32-S3R8 | Display, touch, LVGL UI, WiFi | COM4* |
| **Secondary** | ESP32-U4WDH | Bluetooth audio, AIDA64 mode | COM3* |

*Port numbers may vary on your system.

A **CH445P analog switch** controls which MCU the USB connects to. By default, the stock firmware routes USB to the ESP32 (for Bluetooth audio features).

## Entering Download Mode (ESP32-S3)

The wiki mentions a BOOT button, but it's **not accessible externally** — it's either on the internal PCB or requires disassembly.

### The Easy Way: USB Reset Sequence

The ESP32-S3 supports entering download mode via USB protocol. Use the `--before default-reset` flag with esptool:

```bash
python -m esptool --port COM4 --before default-reset --chip esp32s3 chip_id
```

Output should show:
```
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
```

### Alternative: WebSerial Flasher

Browser-based flashing works without any buttons:
1. Open Chrome or Edge
2. Use any ESP32 web flasher (e.g., [ESP Web Tools](https://esphome.github.io/esp-web-tools/))
3. Select the COM port when prompted

This proves the USB download mode works via software.

## Switching USB Target

The stock firmware has a **"Reboot to MSC"** option in the settings menu. This switches the CH445P to route USB to the ESP32-S3 side.

After selecting MSC mode:
- A new COM port appears (e.g., COM4)
- The ESP32-S3 is now accessible via esptool

## Chip Identification Commands

Check which MCU you're connected to:

```bash
# Quick check (auto-detect chip)
python -m esptool --port COMx chip_id

# Force ESP32-S3 with download mode trigger
python -m esptool --port COM4 --before default-reset --chip esp32s3 chip_id
```

**ESP32-S3** response:
```
Chip type: ESP32-S3 (QFN56)
Features: Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB
```

**ESP32** (secondary) response:
```
Chip type: ESP32-U4WDH (revision v3.1)
Features: Wi-Fi, BT, Dual Core + LP Core, 240MHz, Embedded Flash
```

## Hardware Specs

| Component | Specification |
|-----------|---------------|
| Main MCU | ESP32-S3R8 (240MHz, 8MB PSRAM) |
| Flash | 16MB external |
| Display | 1.8" round LCD, 360×360, ST77916 driver |
| Touch | CST816D capacitive (I2C) |
| Audio | PCM5100A DAC (I2S) |
| Mic | Digital MEMS |
| Haptics | DRV2605 vibration motor |
| Storage | MicroSD slot (SDMMC) |
| Battery | LiPo support with charging IC |

## Pin Assignments

See the [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8) for full pinout.

Key connections:
- **Display**: QSPI (SH8601 quad-SPI protocol)
- **Touch**: I2C (GPIO configurable)
- **Encoder**: GPIO with interrupt
- **Audio I2S**: Connected to PCM5100A

## Stock Firmware Features

The factory firmware includes:
- AIDA64 PC monitoring (via Bluetooth)
- Music player (MP3 from SD card)
- MJPEG video player
- Photo album viewer
- Bluetooth audio sink
- Spectrum analyser (mic input)
- Theme clock with NTP sync
- Text reader

WiFi AP: `My Ap` / password: `12345678`  
Web config: `http://192.168.4.1` (after connecting to AP)

## Resources

- [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8)
- [muness/roon-knob](https://github.com/muness/roon-knob) — Custom firmware project with excellent hardware documentation
- [ESP-IDF Examples](https://github.com/espressif/esp-idf) — Official Espressif framework
