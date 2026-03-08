#!/usr/bin/env python3
"""
ESP32 Voice Hub WebSocket Server

Handles two modes:
1. IDLE: Continuous 80ms audio chunks for wake word detection
2. RECORDING: Buffered audio → STT → OpenClaw → TTS → response

This is a standalone WebSocket server that complements the existing OpenClaw
voice hook integration. It runs alongside OpenClaw but doesn't require deep
plugin integration.

Architecture:
    ESP32 ←→ WebSocket Server ←→ OpenWakeWord (subprocess)
                    ↓
            OpenClaw (via existing /hooks/voice endpoint)

Usage:
    python server.py --port 8765 --openclaw-url http://192.168.1.100:8765

The ESP32 connects via WebSocket and streams audio. When wake word is detected,
the server signals the ESP32 to start recording. When recording ends, it:
1. Transcribes via Whisper API
2. Sends to OpenClaw agent
3. Gets TTS response
4. Streams back to ESP32
"""

import argparse
import asyncio
import json
import logging
import os
import signal
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path
from typing import Optional

import aiohttp
import websockets
from websockets.server import WebSocketServerProtocol

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("WakeWordServer")

# OpenWakeWord chunk size: exactly 80ms at 16kHz
CHUNK_SAMPLES = 1280
CHUNK_BYTES = CHUNK_SAMPLES * 2  # 16-bit samples

# Global state
wake_word_process: Optional[subprocess.Popen] = None
clients: dict[str, "ClientSession"] = {}
shutdown_event = asyncio.Event()


class ClientSession:
    """Tracks state for a connected ESP32 client."""

    def __init__(self, ws: WebSocketServerProtocol, peer_id: str):
        self.ws = ws
        self.peer_id = peer_id
        self.state = "idle"  # idle | recording
        self.audio_buffer: list[bytes] = []
        self.sample_rate = 16000
        self.expected_bytes = 0

    def reset_recording(self):
        self.audio_buffer = []
        self.expected_bytes = 0


async def pipe_to_wake_word(audio_chunk: bytes):
    """Pipe audio chunk to wake word subprocess."""
    global wake_word_process
    if wake_word_process and wake_word_process.stdin:
        try:
            wake_word_process.stdin.write(audio_chunk)
            wake_word_process.stdin.flush()
        except BrokenPipeError:
            log.warning("Wake word process stdin broken, will restart")
            wake_word_process = None


async def read_wake_word_output():
    """Read wake word detections from subprocess stdout."""
    global wake_word_process
    while not shutdown_event.is_set():
        if not wake_word_process or not wake_word_process.stdout:
            await asyncio.sleep(0.5)
            continue

        try:
            # Non-blocking read
            line = wake_word_process.stdout.readline()
            if not line:
                await asyncio.sleep(0.01)
                continue

            line = line.decode("utf-8").strip()
            if line == "WAKE":
                log.info("Wake word detected!")
                # Notify all idle clients
                for client in clients.values():
                    if client.state == "idle":
                        try:
                            await client.ws.send(json.dumps({"type": "wake_detected"}))
                            log.info(f"Sent wake_detected to {client.peer_id}")
                        except Exception as e:
                            log.warning(f"Failed to notify {client.peer_id}: {e}")

        except Exception as e:
            log.error(f"Wake word read error: {e}")
            await asyncio.sleep(0.1)


def start_wake_word_service():
    """Start the wake word subprocess."""
    global wake_word_process

    script_dir = Path(__file__).parent
    service_script = script_dir / "service.py"

    if not service_script.exists():
        log.error(f"service.py not found at {service_script}")
        return False

    model_path = script_dir / "models" / "oi_minerva.tflite"
    if not model_path.exists():
        log.warning(f"Model not found at {model_path}")
        log.warning("Run 'python train.py' to generate the wake word model")
        return False

    log.info(f"Starting wake word service: {service_script}")
    wake_word_process = subprocess.Popen(
        [sys.executable, str(service_script)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=sys.stderr,  # Pass through for debugging
        bufsize=0,  # Unbuffered
    )
    return True


async def transcribe_audio(audio_data: bytes, sample_rate: int, openai_api_key: str) -> Optional[str]:
    """Transcribe audio using OpenAI Whisper API."""
    # Create WAV file in memory
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        wav_path = f.name
        with wave.open(f, "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(sample_rate)
            wav.writeframes(audio_data)

    try:
        async with aiohttp.ClientSession() as session:
            with open(wav_path, "rb") as f:
                data = aiohttp.FormData()
                data.add_field("file", f, filename="audio.wav", content_type="audio/wav")
                data.add_field("model", "whisper-1")
                data.add_field("language", "en")

                async with session.post(
                    "https://api.openai.com/v1/audio/transcriptions",
                    headers={"Authorization": f"Bearer {openai_api_key}"},
                    data=data,
                ) as resp:
                    if resp.status != 200:
                        log.error(f"Whisper API error: {resp.status} {await resp.text()}")
                        return None
                    result = await resp.json()
                    return result.get("text", "")
    finally:
        os.unlink(wav_path)


async def get_tts_audio(text: str, openai_api_key: str) -> Optional[bytes]:
    """Get TTS audio from OpenAI."""
    async with aiohttp.ClientSession() as session:
        async with session.post(
            "https://api.openai.com/v1/audio/speech",
            headers={
                "Authorization": f"Bearer {openai_api_key}",
                "Content-Type": "application/json",
            },
            json={
                "model": "tts-1",
                "input": text,
                "voice": "shimmer",
                "response_format": "pcm",  # Raw 24kHz 16-bit mono PCM
            },
        ) as resp:
            if resp.status != 200:
                log.error(f"TTS API error: {resp.status} {await resp.text()}")
                return None
            return await resp.read()


async def send_to_openclaw(text: str, openclaw_url: str, openclaw_token: str) -> Optional[str]:
    """Send message to OpenClaw agent and get response."""
    # Use the existing /hooks/voice pattern but with direct message
    async with aiohttp.ClientSession() as session:
        async with session.post(
            f"{openclaw_url}/hooks/message",
            headers={
                "Authorization": f"Bearer {openclaw_token}",
                "Content-Type": "application/json",
            },
            json={
                "message": text,
                "channel": "esp32-voice",
                "peer_id": "esp32-desk",
            },
        ) as resp:
            if resp.status != 200:
                log.error(f"OpenClaw API error: {resp.status} {await resp.text()}")
                return None
            result = await resp.json()
            return result.get("reply", result.get("text", ""))


async def process_recording(client: ClientSession, config: dict):
    """Process a completed recording: STT → Agent → TTS."""
    audio_data = b"".join(client.audio_buffer)
    client.reset_recording()

    if len(audio_data) < 3200:  # Less than 100ms
        log.warning(f"Recording too short ({len(audio_data)} bytes), ignoring")
        await client.ws.send(json.dumps({"type": "error", "message": "Recording too short"}))
        return

    log.info(f"Processing {len(audio_data)} bytes of audio")

    # Send thinking status
    await client.ws.send(json.dumps({"type": "status", "state": "thinking"}))

    # Transcribe
    openai_key = config.get("openai_api_key")
    if not openai_key:
        log.error("No OpenAI API key configured")
        await client.ws.send(json.dumps({"type": "error", "message": "No API key"}))
        return

    transcript = await transcribe_audio(audio_data, client.sample_rate, openai_key)
    if not transcript:
        await client.ws.send(json.dumps({"type": "error", "message": "Transcription failed"}))
        return

    log.info(f"Transcript: {transcript}")
    await client.ws.send(json.dumps({"type": "transcript", "text": transcript}))

    # Send to OpenClaw agent
    openclaw_url = config.get("openclaw_url")
    openclaw_token = config.get("openclaw_token")

    if openclaw_url and openclaw_token:
        response = await send_to_openclaw(transcript, openclaw_url, openclaw_token)
    else:
        # Fallback: echo for testing
        response = f"You said: {transcript}"

    if not response:
        await client.ws.send(json.dumps({"type": "error", "message": "Agent error"}))
        return

    log.info(f"Response: {response[:100]}...")

    # Get TTS
    tts_audio = await get_tts_audio(response, openai_key)
    if not tts_audio:
        await client.ws.send(json.dumps({"type": "error", "message": "TTS failed"}))
        return

    # Stream TTS back to client
    # OpenAI TTS outputs 24kHz PCM, we need to resample to 16kHz for ESP32
    resampled = resample_audio(tts_audio, 24000, 16000)

    log.info(f"Sending TTS: {len(resampled)} bytes")
    await client.ws.send(json.dumps({
        "type": "tts_start",
        "sampleRate": 16000,
        "byteLength": len(resampled),
    }))

    # Send in 1KB chunks
    chunk_size = 1024
    for i in range(0, len(resampled), chunk_size):
        chunk = resampled[i : i + chunk_size]
        await client.ws.send(chunk)

    await client.ws.send(json.dumps({"type": "tts_end"}))
    log.info("TTS complete")


def resample_audio(audio: bytes, from_rate: int, to_rate: int) -> bytes:
    """Simple linear resampling."""
    if from_rate == to_rate:
        return audio

    samples = len(audio) // 2
    ratio = from_rate / to_rate
    out_samples = int(samples / ratio)

    result = bytearray(out_samples * 2)
    for i in range(out_samples):
        src_idx = min(int(i * ratio), samples - 1)
        src_offset = src_idx * 2
        result[i * 2] = audio[src_offset]
        result[i * 2 + 1] = audio[src_offset + 1]

    return bytes(result)


async def handle_client(ws: WebSocketServerProtocol, config: dict):
    """Handle a connected ESP32 client."""
    peer_id = f"esp32-{id(ws)}"
    client = ClientSession(ws, peer_id)
    clients[peer_id] = client

    log.info(f"Client connected: {peer_id} from {ws.remote_address}")

    try:
        async for message in ws:
            if isinstance(message, str):
                # JSON control message
                try:
                    msg = json.loads(message)
                    msg_type = msg.get("type", "")

                    if msg_type == "audio_start":
                        client.state = "recording"
                        client.sample_rate = msg.get("sampleRate", 16000)
                        client.expected_bytes = msg.get("byteLength", 0)
                        client.audio_buffer = []
                        log.info(f"Recording started: {client.expected_bytes} bytes expected")

                    elif msg_type == "audio_end":
                        client.state = "idle"
                        log.info(f"Recording ended: {len(b''.join(client.audio_buffer))} bytes received")
                        asyncio.create_task(process_recording(client, config))

                    elif msg_type == "ping":
                        await ws.send(json.dumps({"type": "pong"}))

                except json.JSONDecodeError:
                    log.warning(f"Invalid JSON from {peer_id}: {message[:50]}")

            elif isinstance(message, bytes):
                # Binary audio data
                if client.state == "idle":
                    # Idle audio for wake word detection
                    if len(message) == CHUNK_BYTES:
                        await pipe_to_wake_word(message)
                    # else: ignore mismatched chunks
                else:
                    # Recording audio
                    client.audio_buffer.append(message)

    except websockets.ConnectionClosed:
        log.info(f"Client disconnected: {peer_id}")
    except Exception as e:
        log.error(f"Client error: {peer_id}: {e}")
    finally:
        del clients[peer_id]


async def main():
    parser = argparse.ArgumentParser(description="ESP32 Voice Hub WebSocket Server")
    parser.add_argument("--port", type=int, default=8765, help="WebSocket port")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address")
    parser.add_argument("--openclaw-url", help="OpenClaw gateway URL")
    parser.add_argument("--openclaw-token", help="OpenClaw hooks token")
    parser.add_argument("--openai-api-key", help="OpenAI API key (or use OPENAI_API_KEY env)")
    args = parser.parse_args()

    config = {
        "openclaw_url": args.openclaw_url or os.environ.get("OPENCLAW_URL"),
        "openclaw_token": args.openclaw_token or os.environ.get("OPENCLAW_TOKEN"),
        "openai_api_key": args.openai_api_key or os.environ.get("OPENAI_API_KEY"),
    }

    if not config["openai_api_key"]:
        log.error("OpenAI API key required (--openai-api-key or OPENAI_API_KEY)")
        sys.exit(1)

    # Start wake word service
    if not start_wake_word_service():
        log.warning("Wake word service not available — detection disabled")
    else:
        # Start background task to read wake word output
        asyncio.create_task(read_wake_word_output())

    # Handle shutdown gracefully
    loop = asyncio.get_event_loop()
    for sig in (signal.SIGTERM, signal.SIGINT):
        loop.add_signal_handler(sig, lambda: shutdown_event.set())

    log.info(f"Starting WebSocket server on {args.host}:{args.port}")

    async with websockets.serve(
        lambda ws: handle_client(ws, config),
        args.host,
        args.port,
    ):
        await shutdown_event.wait()

    log.info("Shutting down...")
    if wake_word_process:
        wake_word_process.terminate()


if __name__ == "__main__":
    asyncio.run(main())
