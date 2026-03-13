#!/usr/bin/env python3
"""
ESP32 Voice Hub Companion App

Connects to the Voice Hub via WebSocket and translates commands to system keypresses.
Works on Windows, macOS, and Linux.

Usage:
    pip install websockets pynput pygetwindow pywin32
    python voicehub_companion.py [--host 192.168.1.224] [--port 81]
"""

import asyncio
import argparse
import json
import subprocess
import sys
import platform
import os
import ctypes
from typing import Optional


def is_admin():
    """Check if running with admin privileges (Windows)."""
    if platform.system() != "Windows":
        return True  # Not applicable on other platforms
    try:
        return ctypes.windll.shell32.IsUserAnAdmin()
    except:
        return False


def run_as_admin():
    """Re-launch this script with admin privileges."""
    if platform.system() != "Windows":
        return False
    
    try:
        # Re-run the script with elevation
        script = os.path.abspath(sys.argv[0])
        params = ' '.join([f'"{arg}"' for arg in sys.argv[1:]])
        
        # Use ShellExecute with 'runas' to trigger UAC
        ctypes.windll.shell32.ShellExecuteW(
            None,           # hwnd
            "runas",        # operation (run as admin)
            sys.executable, # program
            f'"{script}" {params}',  # parameters
            None,           # directory
            1               # show window
        )
        return True
    except Exception as e:
        print(f"Failed to elevate: {e}")
        return False

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
HAS_WINDOW_MGMT = False
gw = None
win32gui = None

try:
    import pygetwindow as gw
    HAS_WINDOW_MGMT = True
except ImportError:
    pass

# Try win32gui for more reliable window activation
try:
    import win32gui
    import win32con
    import win32api
    import win32process
except ImportError:
    win32gui = None

if not HAS_WINDOW_MGMT:
    print("Note: pygetwindow not installed, window focus won't work")
    print("      Install with: pip install pygetwindow")
if not win32gui:
    print("Note: pywin32 not installed, using fallback activation")
    print("      Install with: pip install pywin32")

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


def choose_best_window(app_name: str, windows):
    """Choose the best window to activate for a given app."""
    
    if app_name == "zoom":
        # Priority order for Zoom windows:
        # 1. Active meeting window ("Zoom Meeting")
        # 2. Webinar window
        # 3. Workplace/main window (fallback)
        for w in windows:
            title = w.title.lower()
            if "zoom meeting" in title:
                return w
        for w in windows:
            title = w.title.lower()
            if "webinar" in title:
                return w
        # Fallback to first non-workplace window, or workplace if that's all there is
        for w in windows:
            if "workplace" not in w.title.lower():
                return w
        return windows[0]
    
    # Default: return first window
    return windows[0]


def focus_app(app_name: str) -> bool:
    """Bring an application window to the front."""
    if not HAS_WINDOW_MGMT:
        print(f"  → Window management not available (install pygetwindow)")
        return False
    
    try:
        # Get all windows containing the app name
        all_windows = gw.getAllWindows()
        
        # Filter to matching windows (visible, reasonably sized)
        if app_name.lower() == "zoom":
            windows = [w for w in all_windows if "Zoom" in w.title and w.width > 200 and w.height > 200]
        elif app_name.lower() == "spotify":
            windows = [w for w in all_windows if "Spotify" in w.title and w.width > 200 and w.height > 200]
        else:
            windows = [w for w in all_windows if app_name.lower() in w.title.lower() and w.width > 200 and w.height > 200]
        
        if not windows:
            print(f"  → No window found for {app_name}")
            return False
        
        # Debug: show all matching windows
        print(f"  → Found {len(windows)} {app_name} window(s):")
        for i, w in enumerate(windows):
            print(f"      [{i}] {w.title[:50]}")
        
        # Choose the best window (don't cycle - pick the right one)
        win = choose_best_window(app_name.lower(), windows)
        
        print(f"  → Activating: {win.title[:50]}")
        
        # Use win32gui if available (more reliable than pygetwindow.activate)
        if win32gui:
            try:
                import win32process
                hwnd = win._hWnd
                
                # Restore if minimized
                if win32gui.IsIconic(hwnd):
                    win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
                
                # Windows blocks SetForegroundWindow unless we're the foreground app
                # Workaround: attach to the target window's thread input
                current_thread = win32api.GetCurrentThreadId()
                target_thread, _ = win32process.GetWindowThreadProcessId(hwnd)
                
                if current_thread != target_thread:
                    win32process.AttachThreadInput(current_thread, target_thread, True)
                    try:
                        win32gui.SetForegroundWindow(hwnd)
                        win32gui.BringWindowToTop(hwnd)
                    finally:
                        win32process.AttachThreadInput(current_thread, target_thread, False)
                else:
                    win32gui.SetForegroundWindow(hwnd)
                
                return True
            except Exception as e:
                print(f"  → win32gui failed: {e}, trying fallback...")
        
        # Fallback to pygetwindow
        try:
            if hasattr(win, 'isMinimized') and win.isMinimized:
                win.restore()
            win.activate()
        except Exception as e:
            print(f"  → Fallback also failed: {e}")
            # Last resort: just try to bring it up via minimize/restore trick
            try:
                win.minimize()
                win.restore()
            except:
                pass
        return True
        
    except Exception as e:
        print(f"  → Failed to focus {app_name}: {e}")
        import traceback
        traceback.print_exc()
        return False


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
    # Check for admin on Windows (needed for window focus to work reliably)
    if platform.system() == "Windows" and not is_admin():
        print("Requesting admin privileges for window management...")
        if run_as_admin():
            sys.exit(0)  # Exit this instance, elevated one will take over
        else:
            print("Warning: Running without admin - window focus may not work")
            print()
    
    parser = argparse.ArgumentParser(description="ESP32 Voice Hub Companion App")
    parser.add_argument("--host", default="192.168.1.224", help="Voice Hub IP address")
    parser.add_argument("--port", type=int, default=81, help="WebSocket port")
    parser.add_argument("--no-elevate", action="store_true", help="Don't request admin")
    args = parser.parse_args()
    
    print("=" * 50)
    print("  ESP32 Voice Hub Companion")
    print("=" * 50)
    print(f"Host: {args.host}")
    print(f"Port: {args.port}")
    print(f"Platform: {platform.system()}")
    print(f"Admin: {'Yes' if is_admin() else 'No'}")
    print(f"Window management: {'Yes' if HAS_WINDOW_MGMT else 'No'}")
    print()
    
    try:
        asyncio.run(connect_and_listen(args.host, args.port))
    except KeyboardInterrupt:
        print("\n\nGoodbye!")


if __name__ == "__main__":
    main()
