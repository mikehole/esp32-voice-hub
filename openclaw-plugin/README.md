# ESP32 Audio Terminal — OpenClaw Plugin

OpenClaw plugin for ESP32 Voice Hub devices. Provides:

- **Wake word detection** via Picovoice Porcupine ("Oi Minerva")
- **Speech-to-text** via OpenAI Whisper
- **Agent integration** via OpenClaw
- **Text-to-speech** via OpenAI TTS

## Installation

```bash
# From the repo
openclaw plugins install ./openclaw-plugin

# Restart gateway
openclaw gateway restart
```

## Configuration

Add to your `openclaw.json`:

```json5
{
  plugins: {
    entries: {
      "esp32-audio": {
        enabled: true,
        config: {
          wsPort: 8765,
          picovoice: {
            accessKey: "your-picovoice-access-key",
            modelPath: "./models/oi_minerva_linux_v4.ppn",
            sensitivity: 0.5
          },
          openai: {
            apiKey: "sk-...",
            ttsVoice: "shimmer"
          }
        }
      }
    }
  }
}
```

## Getting API Keys

### Picovoice

1. Sign up at https://console.picovoice.ai/
2. Get your free access key
3. Train a custom wake word or use the included model

### OpenAI

1. Get an API key from https://platform.openai.com/
2. Needs access to Whisper (STT) and TTS APIs

## ESP32 Configuration

In the ESP32 web admin panel, set:

- **Wake Word Host:** IP/hostname of machine running OpenClaw
- **Wake Word Port:** 8765 (or your configured port)

## How It Works

1. ESP32 connects via WebSocket
2. Continuously streams 32ms audio chunks
3. Plugin runs Porcupine wake word detection
4. On detection, sends `wake_detected` to ESP32
5. ESP32 plays beep, starts recording
6. Recording sent to plugin
7. Plugin: Whisper STT → OpenClaw Agent → OpenAI TTS
8. TTS audio streamed back to ESP32
9. ESP32 plays response, returns to idle

## Development

```bash
cd openclaw-plugin
npm install
npm run build
```

## Files

- `src/index.ts` — Main plugin entry point
- `src/wake-word.ts` — Porcupine wake word detector
- `src/audio-processor.ts` — STT/TTS processing
- `openclaw.plugin.json` — Plugin manifest
