#!/usr/bin/env python3
"""
ESP32 Voice Hub Companion App

Connects to the Voice Hub via WebSocket and translates commands to system keypresses.
Works on Windows, macOS, and Linux.

Usage:
    pip install websockets pynput pygetwindow
    python voicehub_companion.py [--host 192.168.1.224] [--port 81]
"""

import asyncio
import argparse
import json
import subprocess
import sys
import platform
from typing import Optional

try:
    import websockets
except ImportError:
    print("Please install websockets: pip install websockets")
    sys.exit(1)

try:
    from pynput.keyboard import Key, Controller
except ImportError:
    print("Please install pynput: pip install pynput")
    sys.exit(1)

# Optional: window management (Windows only for now)
try:
    import pygetwindow as gw
    HAS_WINDOW_MGMT = True
except ImportError:
    HAS_WINDOW_MGMT = False
    print("Note: pygetwindow not installed, window focus won't work")
    print("      Install with: pip install pygetwindow")

keyboard = Controller()
IS_WINDOWS = platform.system() == "Windows"
IS_MAC = platform.system() == "Darwin"

# Map commands to key sequences
MEDIA_KEYS = {
    "play_pause": Key.media_play_pause,
    "next_track": Key.media_next,
    "prev_track": Key.media_previous,
    "volume_up": Key.media_volume_up,
    "volume_down": Key.media_volume_down,
    "mute": Key.media_volume_mute,
}

# App launch commands (app name -> executable/path)
APP_LAUNCH = {
    "spotify": {
        "windows": "spotify",  # Usually in PATH via Microsoft Store
        "windows_alt": r"C:\Users\{user}\AppData\Roaming\Spotify\Spotify.exe",
        "mac": "Spotify",
    },
    "zoom": {
        "windows": r"C:\Program Files\Zoom\bin\Zoom.exe",
        "mac": "zoom.us",
    },
}


def launch_app(app_name: str) -> bool:
    """Launch an application by name."""
    app_info = APP_LAUNCH.get(app_name.lower())
    if not app_info:
        print(f"  → Unknown app: {app_name}")
        return False
    
    try:
        if IS_WINDOWS:
            import os
            user = os.environ.get("USERNAME", "")
            
            # Try the simple command first (for Store apps)
            exe = app_info.get("windows", "")
            if exe:
                try:
                    subprocess.Popen(exe, shell=True)
                    print(f"  → Launched {app_name}")
                    return True
                except Exception:
                    pass
            
            # Try the full path
            exe = app_info.get("windows_alt", "").replace("{user}", user)
            if exe and os.path.exists(exe):
                subprocess.Popen([exe])
                print(f"  → Launched {app_name}")
                return True
                
        elif IS_MAC:
            app = app_info.get("mac", "")
            if app:
                subprocess.Popen(["open", "-a", app])
                print(f"  → Launched {app_name}")
                return True
                
    except Exception as e:
        print(f"  → Failed to launch {app_name}: {e}")
    
    return False


def focus_app(app_name: str) -> bool:
    """Bring an application window to the front."""
    if not HAS_WINDOW_MGMT:
        print(f"  → Window management not available (install pygetwindow)")
        return False
    
    # Map app names to window title patterns (in priority order for apps with multiple windows)
    # For Zoom: prioritize "Zoom Meeting" over "Zoom" to get the active meeting window
    title_priorities = {
        "spotify": ["Spotify"],
        "zoom": ["Zoom Meeting", "Zoom Webinar", "Zoom"],  # Meeting windows first
    }
    
    patterns = title_priorities.get(app_name.lower(), [app_name])
    
    try:
        # Try each pattern in priority order
        for pattern in patterns:
            windows = gw.getWindowsWithTitle(pattern)
            # Filter out tiny windows (like tray icons) - must be reasonably sized
            windows = [w for w in windows if w.width > 200 and w.height > 200]
            if windows:
                # For Zoom specifically, cycle through windows on repeated presses
                # by tracking which window we focused last time
                if app_name.lower() == "zoom" and len(windows) > 1:
                    # Cycle through windows
                    global _last_zoom_window
                    if '_last_zoom_window' not in globals():
                        _last_zoom_window = -1
                    _last_zoom_window = (_last_zoom_window + 1) % len(windows)
                    win = windows[_last_zoom_window]
                    print(f"  → Cycling Zoom windows ({_last_zoom_window + 1}/{len(windows)})")
                else:
                    win = windows[0]
                
                # Restore if minimized
                if hasattr(win, 'isMinimized') and win.isMinimized:
                    win.restore()
                win.activate()
                print(f"  → Focused {app_name} ({win.title[:40]})")
                return True
        
        print(f"  → No window found for {app_name}")
        return False
    except Exception as e:
        print(f"  → Failed to focus {app_name}: {e}")
        return False

_last_zoom_window = -1  # Track for cycling


def launch_and_focus(app_name: str):
    """Launch app if not running, or focus it if already running."""
    # Try to focus first
    if HAS_WINDOW_MGMT:
        title_patterns = {
            "spotify": "Spotify",
            "zoom": "Zoom",
        }
        pattern = title_patterns.get(app_name.lower(), app_name)
        windows = gw.getWindowsWithTitle(pattern)
        if windows:
            focus_app(app_name)
            return
    
    # Not running, launch it
    launch_app(app_name)


def handle_command(cmd: str, arg: Optional[str] = None):
    """Execute a command."""
    # Media key commands
    if cmd in MEDIA_KEYS:
        key = MEDIA_KEYS[cmd]
        print(f"  → Pressing {key}")
        keyboard.press(key)
        keyboard.release(key)
        return
    
    # Launch commands: "launch:appname"
    if cmd.startswith("launch:"):
        app = cmd.split(":", 1)[1]
        launch_and_focus(app)
        return
    
    # Focus commands: "focus:appname"
    if cmd.startswith("focus:"):
        app = cmd.split(":", 1)[1]
        focus_app(app)
        return
    
    # Zoom shortcuts (all use Alt+key)
    zoom_shortcuts = {
        "zoom_mute": ("a", "Toggle mute"),
        "zoom_video": ("v", "Toggle video"),
        "zoom_share": ("s", "Share screen"),
        "zoom_chat": ("h", "Open chat"),
        "zoom_participants": ("u", "Participants"),
        "zoom_leave": ("q", "Leave meeting"),
    }
    
    if cmd in zoom_shortcuts:
        key, desc = zoom_shortcuts[cmd]
        print(f"  → Zoom: {desc} (Alt+{key.upper()})")
        keyboard.press(Key.alt)
        keyboard.press(key)
        keyboard.release(key)
        keyboard.release(Key.alt)
        return
    
    print(f"  → Unknown command: {cmd}")


async def connect_and_listen(host: str, port: int):
    """Connect to the Voice Hub and listen for commands."""
    uri = f"ws://{host}:{port}/ws"
    
    while True:
        try:
            print(f"Connecting to {uri}...")
            async with websockets.connect(uri) as ws:
                print(f"✓ Connected to Voice Hub at {host}:{port}")
                print("Listening for commands... (Ctrl+C to quit)\n")
                
                async for message in ws:
                    try:
                        data = json.loads(message)
                        cmd = data.get("cmd")
                        arg = data.get("arg")
                        print(f"Received: {cmd}" + (f" ({arg})" if arg else ""))
                        handle_command(cmd, arg)
                    except json.JSONDecodeError:
                        print(f"Invalid JSON: {message}")
                        
        except websockets.exceptions.ConnectionClosed:
            print("\n✗ Connection closed, reconnecting in 3s...")
        except ConnectionRefusedError:
            print(f"\n✗ Connection refused. Is Voice Hub running? Retrying in 5s...")
            await asyncio.sleep(5)
        except Exception as e:
            print(f"\n✗ Error: {e}. Retrying in 5s...")
            await asyncio.sleep(5)
        
        await asyncio.sleep(3)


def main():
    parser = argparse.ArgumentParser(description="ESP32 Voice Hub Companion App")
    parser.add_argument("--host", default="192.168.1.224", help="Voice Hub IP address")
    parser.add_argument("--port", type=int, default=81, help="WebSocket port")
    args = parser.parse_args()
    
    print("=" * 50)
    print("  ESP32 Voice Hub Companion")
    print("=" * 50)
    print(f"Host: {args.host}")
    print(f"Port: {args.port}")
    print(f"Platform: {platform.system()}")
    print(f"Window management: {'Yes' if HAS_WINDOW_MGMT else 'No'}")
    print()
    
    try:
        asyncio.run(connect_and_listen(args.host, args.port))
    except KeyboardInterrupt:
        print("\n\nGoodbye!")


if __name__ == "__main__":
    main()
