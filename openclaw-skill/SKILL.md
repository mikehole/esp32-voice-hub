# ESP32 Voice Hub Skill

Handle voice commands from the ESP32 Voice Hub (desk orb). The orb sends audio via webhook, and you respond with synthesized speech.

## Trigger

This skill is triggered by the `/hooks/voice` webhook with payload:
```json
{
  "audio_url": "http://<orb-ip>/api/audio/download",
  "callback_url": "http://<orb-ip>/api/play?rate=12000"
}
```

## Workflow

When you receive a VoiceHub message:

### 1. Fetch and Transcribe Audio

Download the audio from `audio_url` and transcribe it:

```bash
curl -s "{{audio_url}}" -o /tmp/voice-command.wav
/app/skills/openai-whisper-api/scripts/transcribe.sh /tmp/voice-command.wav
```

Or use the `openai-whisper-api` skill directly.

### 2. Process the Command

Handle the transcribed request using your normal capabilities:
- Answer questions
- Set reminders (use `cron add`)
- Check weather, calendar, etc.
- Control smart home devices
- Any other tools available to you

### 3. Generate Speech Response

Convert your response to speech using OpenAI TTS:

```bash
curl -s "https://api.openai.com/v1/audio/speech" \
  -H "Authorization: Bearer $OPENAI_API_KEY" \
  -H "Content-Type: application/json" \
  -d '{
    "model": "tts-1",
    "voice": "shimmer",
    "input": "Your response text here",
    "response_format": "pcm"
  }' -o /tmp/response.pcm
```

### 4. Send Audio to Orb

POST the PCM audio to the callback URL:

```bash
curl -X POST "{{callback_url}}" \
  --data-binary @/tmp/response.pcm
```

**Important:** The callback URL includes `rate=12000` — this is correct for 24kHz PCM audio due to the ESP32's stereo I2S frame rate handling.

## Response Guidelines

- **Keep it short** — this is voice, not text. Aim for 1-2 sentences.
- **Be conversational** — speak naturally, not like a document.
- **Confirm actions** — "Done, I've set a reminder for 3pm" not just "OK".
- **Handle errors gracefully** — "Sorry, I couldn't do that" with brief reason.

## Reminders and Timers

When the user asks to be reminded or pinged in X minutes, create a cron job that **delivers via the voice hub as a notification**, not chat:

```bash
openclaw cron add \
  --name "voice-reminder" \
  --at "Xm" \
  --message "Send a voice notification to the desk orb. Generate TTS for: '<reminder text>' and POST to http://192.168.1.224/api/notify-audio?rate=12000 (NOT /api/play). This shows the notification ring and requires tap to acknowledge." \
  --delete-after-run
```

**Important:** Use `/api/notify-audio` (not `/api/play`) for reminders:
- Shows purple notification ring with attention chime
- User must tap to acknowledge and hear the message
- Ensures reminders aren't missed if user is away

**Do NOT use `--announce`** for voice reminders — that would send text to Slack instead of audio to the orb.

## Example Flow

**User says:** "What's the weather like?"

**You:**
1. Fetch audio from `http://192.168.1.224/api/audio/download`
2. Transcribe → "What's the weather like?"
3. Check weather for user's location (Penarth, UK)
4. Generate TTS: "It's currently 12 degrees and cloudy in Penarth, with rain expected this afternoon."
5. POST audio to `http://192.168.1.224/api/play?rate=12000`

## Hardware Reference

- **Device:** Waveshare ESP32-S3 Knob Touch LCD 1.8"
- **Audio format:** 16kHz 16-bit mono PCM (recording), 24kHz PCM (playback)
- **Display:** 360x360 round LCD with avatar
- **Default IP:** `192.168.1.224` (configurable via captive portal)

## Related Skills

- `openai-whisper-api` — Audio transcription
- `desk-minerva` — Direct orb control (notifications, avatar, etc.)
- `weather` — Weather queries
