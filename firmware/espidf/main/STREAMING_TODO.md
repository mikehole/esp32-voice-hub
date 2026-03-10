# Streaming TTS - Firmware TODO

The plugin now supports streaming TTS via WebSocket. The orb firmware needs these changes:

## New WebSocket Messages

### `audio_stream_start` (JSON)
```json
{"type": "audio_stream_start", "sampleRate": 24000}
```
- Prepare for streaming playback
- Initialize audio output at specified sample rate
- Start a ring buffer for incoming chunks

### Binary frames (after audio_stream_start)
- Raw PCM audio data (24kHz 16-bit signed LE mono)
- Add to ring buffer
- Start playback after ~100ms of audio buffered

### `audio_stream_end` (JSON)
```json
{"type": "audio_stream_end"}
```
- End of stream
- Continue playing until buffer empty
- Return to idle state

## Implementation Notes

1. **Ring buffer**: Need ~0.5s buffer (24000 samples = 48KB)
2. **Playback trigger**: Start playing after buffer has ~100ms of data
3. **Underrun handling**: If buffer empties before stream ends, pause and wait
4. **State machine**: IDLE -> BUFFERING -> PLAYING -> DRAINING -> IDLE

## Fallback

If orb doesn't support streaming, plugin will detect closed WebSocket and fall back to HTTP POST (existing `/api/play` endpoint).
