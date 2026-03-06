# OpenClaw Skill for ESP32 Voice Hub

This skill enables OpenClaw to handle voice commands from the ESP32 Voice Hub.

## How It Works

```
┌─────────────┐     POST /hooks/voice      ┌──────────────┐
│   ESP32     │  ───────────────────────►  │   OpenClaw   │
│  Voice Hub  │     {audio_url, callback}  │   Gateway    │
└─────────────┘                            └──────┬───────┘
      ▲                                           │
      │                                           ▼
      │                                    ┌──────────────┐
      │         GET /api/audio/download    │    Agent     │
      │  ◄─────────────────────────────    │  (Minerva)   │
      │                                    └──────┬───────┘
      │                                           │
      │         POST /api/play?rate=12000         │
      │  ◄────────────────────────────────────────┘
      │              (TTS audio)
```

1. **Orb records audio** and exposes it at `/api/audio/download`
2. **Orb triggers webhook** at `/hooks/voice` with URLs
3. **Agent fetches audio** from the orb
4. **Agent transcribes** using Whisper
5. **Agent processes** the command (tools, skills, etc.)
6. **Agent generates TTS** response
7. **Agent POSTs audio** back to the orb's `/api/play` endpoint

## Installation

### 1. Copy the skill

Copy the `SKILL.md` file to your OpenClaw skills directory:

```bash
cp SKILL.md ~/.openclaw/skills/esp32-voice-hub/SKILL.md
```

Or symlink it:

```bash
ln -s /path/to/esp32-voice-hub/openclaw-skill ~/.openclaw/skills/esp32-voice-hub
```

### 2. Configure the webhook

Add the hook mapping to your `~/.openclaw/openclaw.json`:

```json5
{
  "hooks": {
    "enabled": true,
    "token": "generate-a-secure-token",
    "mappings": [
      {
        "match": { "path": "voice" },
        "action": "agent",
        "name": "VoiceHub", 
        "wakeMode": "now",
        "deliver": false,
        "messageTemplate": "[VoiceHub] Voice command from ESP32 Voice Hub.\n\nAudio URL: {{audio_url}}\nCallback URL: {{callback_url}}\n\nFollow the esp32-voice-hub skill instructions to handle this voice command."
      }
    ]
  }
}
```

### 3. Configure the orb

In the orb's web admin (`http://<orb-ip>/`):

- **OpenClaw Endpoint:** `https://your-openclaw-host:port`
- **OpenClaw Token:** The `hooks.token` value from your config

### 4. Dependencies

The skill uses:
- `openai-whisper-api` skill for transcription
- OpenAI TTS API for speech synthesis
- `curl` for HTTP requests

Make sure `OPENAI_API_KEY` is set in your environment.

## Testing

Trigger a test webhook:

```bash
curl -X POST "https://your-openclaw-host/hooks/voice" \
  -H "Authorization: Bearer YOUR_HOOKS_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "audio_url": "http://192.168.1.224/api/audio/download",
    "callback_url": "http://192.168.1.224/api/play?rate=12000"
  }'
```

## Customization

Edit `SKILL.md` to customize:
- TTS voice (default: `shimmer`)
- Response style and length
- Available commands and integrations
- Error handling behavior

## License

MIT - Same as the ESP32 Voice Hub project.
