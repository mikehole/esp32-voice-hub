/**
 * ESP32 Audio Terminal Plugin
 *
 * Provides WebSocket server for ESP32 Voice Hub devices with:
 * - Wake word detection via Picovoice Porcupine
 * - Speech-to-text via OpenClaw runtime
 * - Agent integration via OpenClaw
 * - Text-to-speech via OpenClaw runtime
 */

import type { OpenClawPluginApi } from "openclaw/plugin-sdk";
import { WebSocketServer, WebSocket } from "ws";
import { WakeWordDetector } from "./wake-word.js";
import { writeFileSync, unlinkSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

export interface Esp32AudioConfig {
  enabled: boolean;
  wsPort: number;
  wsBind: string;
  picovoice?: {
    accessKey?: string;
    modelPath?: string;
    sensitivity?: number;
  };
  openai?: {
    apiKey?: string;
    ttsVoice?: string;
    ttsModel?: string;
  };
}

interface ClientSession {
  id: string;
  ws: WebSocket;
  state: "idle" | "recording";
  audioBuffer: Buffer[];
  sampleRate: number;
}

function sendJson(ws: WebSocket, obj: unknown) {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(obj));
  }
}

function parseConfig(value: unknown): Esp32AudioConfig {
  const raw = (value && typeof value === "object" ? value : {}) as Record<string, unknown>;
  return {
    enabled: typeof raw.enabled === "boolean" ? raw.enabled : true,
    wsPort: typeof raw.wsPort === "number" ? raw.wsPort : 8765,
    wsBind: typeof raw.wsBind === "string" ? raw.wsBind : "0.0.0.0",
    picovoice: raw.picovoice as Esp32AudioConfig["picovoice"],
    openai: raw.openai as Esp32AudioConfig["openai"],
  };
}

const esp32AudioPlugin = {
  id: "esp32-audio",
  name: "ESP32 Audio Terminal",
  description: "ESP32 Voice Hub with wake word detection",

  configSchema: {
    parse: parseConfig,
  },

  register(api: OpenClawPluginApi) {
    const config = parseConfig(api.pluginConfig);
    const log = api.logger;

    if (!config.enabled) {
      log.info("[esp32-audio] Plugin disabled");
      return;
    }

    // Validate required config
    if (!config.picovoice?.accessKey) {
      log.error("[esp32-audio] picovoice.accessKey is required");
      return;
    }
    if (!config.picovoice?.modelPath) {
      log.error("[esp32-audio] picovoice.modelPath is required");
      return;
    }

    const clients = new Map<string, ClientSession>();
    let wakeWordDetector: WakeWordDetector | null = null;

    // Initialize wake word detector
    try {
      wakeWordDetector = new WakeWordDetector({
        accessKey: config.picovoice.accessKey,
        modelPath: config.picovoice.modelPath,
        sensitivity: config.picovoice.sensitivity ?? 0.5,
        onWakeWord: (clientId: string) => {
          const client = clients.get(clientId);
          if (client && client.state === "idle") {
            log.info(`[esp32-audio] Wake word detected for ${clientId}`);
            sendJson(client.ws, { type: "wake_detected" });
          }
        },
      });
      log.info("[esp32-audio] Wake word detector initialized");
    } catch (err) {
      log.error(`[esp32-audio] Failed to init wake word: ${err}`);
      return;
    }

    // Create WebSocket server
    const wss = new WebSocketServer({
      port: config.wsPort,
      host: config.wsBind,
    });

    log.info(`[esp32-audio] WebSocket server listening on ${config.wsBind}:${config.wsPort}`);

    wss.on("connection", (ws, req) => {
      const clientId = `esp32-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
      const client: ClientSession = {
        id: clientId,
        ws,
        state: "idle",
        audioBuffer: [],
        sampleRate: 16000,
      };
      clients.set(clientId, client);

      log.info(`[esp32-audio] Client connected: ${clientId} from ${req.socket.remoteAddress}`);

      ws.on("message", async (data) => {
        if (typeof data === "string") {
          // JSON control message
          try {
            const msg = JSON.parse(data);
            await handleControlMessage(client, msg);
          } catch (err) {
            log.warn(`[esp32-audio] Error handling message from ${clientId}: ${err}`);
          }
        } else if (Buffer.isBuffer(data)) {
          // Binary audio data
          handleAudioData(client, data);
        }
      });

      ws.on("close", () => {
        log.info(`[esp32-audio] Client disconnected: ${clientId}`);
        clients.delete(clientId);
        wakeWordDetector?.removeClient(clientId);
      });

      ws.on("error", (err) => {
        log.error(`[esp32-audio] Client error ${clientId}: ${err}`);
      });
    });

    async function handleControlMessage(client: ClientSession, msg: { type: string; [key: string]: unknown }) {
      switch (msg.type) {
        case "audio_start":
          client.state = "recording";
          client.audioBuffer = [];
          client.sampleRate = (msg.sampleRate as number) ?? 16000;
          log.info(`[esp32-audio] Recording started for ${client.id}`);
          break;

        case "audio_end":
          client.state = "idle";
          const audio = Buffer.concat(client.audioBuffer);
          client.audioBuffer = [];
          log.info(`[esp32-audio] Recording ended for ${client.id}: ${audio.length} bytes`);
          await processRecording(client, audio);
          break;

        case "ping":
          sendJson(client.ws, { type: "pong" });
          break;
      }
    }

    function handleAudioData(client: ClientSession, data: Buffer) {
      if (client.state === "idle") {
        // Idle audio - send to wake word detector
        wakeWordDetector?.processAudio(client.id, data);
      } else {
        // Recording audio - buffer it
        client.audioBuffer.push(data);
      }
    }

    async function processRecording(client: ClientSession, audio: Buffer) {
      try {
        // Create WAV file for transcription
        const wavPath = join(tmpdir(), `esp32-${Date.now()}.wav`);
        const wavBuffer = createWav(audio, client.sampleRate);
        writeFileSync(wavPath, wavBuffer);

        try {
          // Step 1: Transcribe using OpenClaw's STT
          const sttResult = await api.runtime.stt.transcribeAudioFile({
            filePath: wavPath,
            cfg: api.config as Parameters<typeof api.runtime.stt.transcribeAudioFile>[0]["cfg"],
          });

          const transcript = sttResult?.text;
          if (!transcript || transcript.trim().length === 0) {
            sendJson(client.ws, { type: "error", message: "No speech detected" });
            client.state = "idle";
            return;
          }

          log.info(`[esp32-audio] Transcript: ${transcript}`);
          sendJson(client.ws, { type: "transcript", text: transcript });

          // Step 2: Get TTS response (this uses the agent internally)
          // For now, just echo back - real implementation would route through agent
          const response = `You said: ${transcript}`;

          // Step 3: Generate TTS using OpenClaw's TTS
          const ttsResult = await api.runtime.tts.textToSpeechTelephony({
            text: response,
            cfg: api.config as Parameters<typeof api.runtime.tts.textToSpeechTelephony>[0]["cfg"],
          });

          if (ttsResult?.success && ttsResult.audioBuffer) {
            // Resample from whatever rate to 16kHz
            const pcm16k = resample(
              ttsResult.audioBuffer,
              ttsResult.sampleRate || 24000,
              16000
            );

            // Send to client
            sendJson(client.ws, {
              type: "tts_start",
              sampleRate: 16000,
              byteLength: pcm16k.length,
            });

            // Send in chunks
            const CHUNK_SIZE = 1024;
            for (let i = 0; i < pcm16k.length; i += CHUNK_SIZE) {
              const chunk = pcm16k.slice(i, Math.min(i + CHUNK_SIZE, pcm16k.length));
              if (client.ws.readyState === WebSocket.OPEN) {
                client.ws.send(chunk);
              }
            }

            sendJson(client.ws, { type: "tts_end" });
          }

        } finally {
          try {
            unlinkSync(wavPath);
          } catch {
            // Ignore cleanup errors
          }
        }

        client.state = "idle";

      } catch (err) {
        log.error(`[esp32-audio] Processing error: ${err}`);
        sendJson(client.ws, { type: "error", message: `Processing error: ${err}` });
        client.state = "idle";
      }
    }

    function createWav(pcm: Buffer, sampleRate: number): Buffer {
      const header = Buffer.alloc(44);

      header.write("RIFF", 0);
      header.writeUInt32LE(36 + pcm.length, 4);
      header.write("WAVE", 8);
      header.write("fmt ", 12);
      header.writeUInt32LE(16, 16);
      header.writeUInt16LE(1, 20);
      header.writeUInt16LE(1, 22);
      header.writeUInt32LE(sampleRate, 24);
      header.writeUInt32LE(sampleRate * 2, 28);
      header.writeUInt16LE(2, 32);
      header.writeUInt16LE(16, 34);
      header.write("data", 36);
      header.writeUInt32LE(pcm.length, 40);

      return Buffer.concat([header, pcm]);
    }

    function resample(audio: Buffer, fromRate: number, toRate: number): Buffer {
      if (fromRate === toRate) {
        return audio;
      }

      const ratio = fromRate / toRate;
      const inSamples = audio.length / 2;
      const outSamples = Math.floor(inSamples / ratio);
      const output = Buffer.alloc(outSamples * 2);

      for (let i = 0; i < outSamples; i++) {
        const srcIndex = Math.min(Math.floor(i * ratio), inSamples - 1);
        output.writeInt16LE(audio.readInt16LE(srcIndex * 2), i * 2);
      }

      return output;
    }
  },
};

export default esp32AudioPlugin;
