#!/usr/bin/env python3
"""
ESP32 Voice Hub Companion App

Connects to the Voice Hub via WebSocket and translates commands to system keypresses.
Works on Windows, macOS, and Linux.

Usage:
    pip install websockets pynput
    python voicehub_companion.py [--host 192.168.1.224] [--port 81]
"""

import asyncio
import argparse
import json
import sys
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

keyboard = Controller()

# Map commands to key sequences
COMMAND_MAP = {
    "play_pause": Key.media_play_pause,
    "next_track": Key.media_next,
    "prev_track": Key.media_previous,
    "volume_up": Key.media_volume_up,
    "volume_down": Key.media_volume_down,
    "mute": Key.media_volume_mute,
}


def handle_command(cmd: str, arg: Optional[str] = None):
    """Execute a command by simulating a keypress."""
    key = COMMAND_MAP.get(cmd)
    if key:
        print(f"  → Pressing {key}")
        keyboard.press(key)
        keyboard.release(key)
    else:
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
    print()
    
    try:
        asyncio.run(connect_and_listen(args.host, args.port))
    except KeyboardInterrupt:
        print("\n\nGoodbye!")


if __name__ == "__main__":
    main()
