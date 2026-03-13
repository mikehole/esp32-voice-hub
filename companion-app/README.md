# Voice Hub Companion App

A Python app that receives commands from the ESP32 Voice Hub via WebSocket and translates them to system actions — media keys, app launching, window focus, and Zoom meeting controls.

## Requirements

- Python 3.8+
- Windows (primary), macOS, or Linux
- Voice Hub on the same network

## Installation

```bash
pip install -r requirements.txt
```

### Windows Dependencies

For full functionality on Windows, you need:

```bash
pip install websockets pynput pygetwindow pywin32
```

- `websockets` — WebSocket client
- `pynput` — Keyboard/media key simulation
- `pygetwindow` — Window enumeration
- `pywin32` — Native Windows API for window focus

## Usage

```bash
# Default: connects to 192.168.1.224:81
python voicehub_companion.py

# Custom host
python voicehub_companion.py --host 192.168.1.100
```

On Windows, the app will automatically request admin privileges via UAC. This is required for reliable window focus (Windows restricts `SetForegroundWindow` for non-elevated processes).

## Startup Output

```
==================================================
  ESP32 Voice Hub Companion
==================================================
Host: 192.168.1.224
Port: 81
Platform: Windows
Admin: Yes
Window management: Yes

Connecting to ws://192.168.1.224:81/ws...
✓ Connected to Voice Hub at 192.168.1.224:81
Listening for commands... (Ctrl+C to quit)
```

## Supported Commands

### Media Controls

| Command | Action |
|---------|--------|
| `play_pause` | Media Play/Pause key |
| `next_track` | Media Next Track key |
| `prev_track` | Media Previous Track key |
| `volume_up` | Volume Up key |
| `volume_down` | Volume Down key |
| `mute` | Mute key |

### App Launch/Focus

| Command | Action |
|---------|--------|
| `launch:spotify` | Launch Spotify (or focus if running) |
| `launch:zoom` | Launch Zoom (or focus if running) |
| `focus:spotify` | Focus Spotify window |
| `focus:zoom` | Focus Zoom Meeting window |

For Zoom, the app intelligently prioritizes the meeting window over the main Zoom Workplace window.

### Zoom Meeting Controls

| Command | Action | Keyboard Shortcut |
|---------|--------|-------------------|
| `zoom_mute` | Toggle microphone | Alt+A |
| `zoom_video` | Toggle camera | Alt+V |
| `zoom_share` | Share screen | Alt+S |
| `zoom_chat` | Open chat panel | Alt+H |
| `zoom_participants` | Show participants | Alt+U |
| `zoom_leave` | Leave meeting | Alt+Q |

## How It Works

```
┌─────────────────────┐                           ┌─────────────────────┐
│   ESP32 Voice Hub   │ ────── WebSocket ───────▶ │   Companion App     │
│   (port 81)         │   {"cmd":"zoom_mute"}     │   (Python)          │
└─────────────────────┘                           └─────────────────────┘
         │                                                  │
         │ User taps                                        │ Simulates
         │ Zoom wedge                                       │ Alt+A
         ▼                                                  ▼
┌─────────────────────┐                           ┌─────────────────────┐
│   1.8" Touch LCD    │                           │   Zoom Meeting      │
│   "Mic" button      │                           │   Mute toggled!     │
└─────────────────────┘                           └─────────────────────┘
```

The command server starts automatically when the Voice Hub connects to WiFi — no need to be in a specific menu mode.

## Running on Startup

### Windows

**Option 1: Startup folder**
1. Press `Win+R`, type `shell:startup`, press Enter
2. Create a shortcut to: `pythonw.exe "C:\path\to\voicehub_companion.py"`

**Option 2: Task Scheduler (recommended for admin)**
1. Open Task Scheduler
2. Create a new task with "Run with highest privileges"
3. Trigger: At log on
4. Action: Start `python.exe` with arguments `C:\path\to\voicehub_companion.py`

### macOS

Create `~/Library/LaunchAgents/com.voicehub.companion.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.voicehub.companion</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/bin/python3</string>
        <string>/path/to/voicehub_companion.py</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
```

Then: `launchctl load ~/Library/LaunchAgents/com.voicehub.companion.plist`

### Linux (systemd)

Create `~/.config/systemd/user/voicehub-companion.service`:

```ini
[Unit]
Description=Voice Hub Companion

[Service]
ExecStart=/usr/bin/python3 /path/to/voicehub_companion.py
Restart=always

[Install]
WantedBy=default.target
```

Then: `systemctl --user enable --now voicehub-companion`

## Troubleshooting

### Connection refused
- Verify the Voice Hub is connected to WiFi (check the display shows an IP)
- Ensure your PC is on the same network
- Try pinging the device: `ping 192.168.1.224`

### Window focus not working
- Ensure the app shows `Admin: Yes` on startup
- If not, run PowerShell/Terminal as Administrator
- Some apps (like Zoom) need the companion app to be elevated

### Zoom Meeting window not focusing
- The app prioritizes "Zoom Meeting" windows over "Zoom Workplace"
- If Zoom shows an error window, it may interfere — close it and retry

### Media keys not affecting current app
- Some apps (Spotify, VLC) need to be the focused window
- Try clicking in the app first, then using media controls

## Adding Custom Commands

Edit `voicehub_companion.py` to add new commands:

```python
# In handle_command():
if cmd == "my_custom_command":
    # Do something
    subprocess.Popen(["notepad.exe"])
    return
```

Then add the corresponding command on the ESP32 firmware side in `wedge_ui.c`.
