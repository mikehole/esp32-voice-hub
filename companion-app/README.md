# Voice Hub Companion App

A simple Python app that receives commands from the ESP32 Voice Hub and translates them to system media key presses.

## Installation

```bash
pip install -r requirements.txt
```

## Usage

```bash
# Default: connects to 192.168.1.224:81
python voicehub_companion.py

# Custom host
python voicehub_companion.py --host 192.168.1.100

# Custom port
python voicehub_companion.py --host 192.168.1.100 --port 8080
```

## How it works

1. The companion app connects to the Voice Hub via WebSocket (port 81)
2. When you tap Music controls on the Voice Hub, it sends JSON commands like `{"cmd": "play_pause"}`
3. The companion app receives these and simulates the corresponding media key press

## Supported Commands

| Command | Media Key |
|---------|-----------|
| `play_pause` | Play/Pause |
| `next_track` | Next Track |
| `prev_track` | Previous Track |
| `volume_up` | Volume Up |
| `volume_down` | Volume Down |
| `mute` | Mute |

## Running on startup

### Windows

1. Create a shortcut to `pythonw.exe voicehub_companion.py`
2. Put shortcut in `shell:startup` folder

### macOS

Create a LaunchAgent plist in `~/Library/LaunchAgents/`

### Linux

Add to your desktop environment's autostart, or create a systemd user service.

## Troubleshooting

- **Connection refused**: Make sure the Voice Hub is connected to WiFi and you're on the same network
- **No response**: Enter Music mode on the Voice Hub first (tap the Music wedge)
- **Media keys not working**: Some apps need focus to receive media keys
