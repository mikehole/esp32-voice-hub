/**
 * ESP32 Audio Terminal Plugin
 *
 * Provides WebSocket server for ESP32 Voice Hub devices with:
 * - Wake word detection via Picovoice Porcupine
 * - Speech-to-text via OpenAI Whisper
 * - Agent integration via OpenClaw
 * - Text-to-speech via OpenAI TTS
 */

import type { OpenClawPluginApi } from "openclaw/plugin-sdk";
import { WebSocketServer, WebSocket } from "ws";
import { WakeWordDetector } from "./wake-word.js";
import { AudioProcessor } from "./audio-processor.js";

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

const esp32AudioPlugin = {
  id: "esp32-audio",
  name: "ESP32 Audio Terminal",
  description: "ESP32 Voice Hub with wake word detection",

  configSchema: {
    parse(value: unknown): Esp32AudioConfig {
      const raw = (value && typeof value === "object" ? value : {}) as Record<string, unknown>;
      return {
        enabled: typeof raw.enabled === "boolean" ? raw.enabled : true,
        wsPort: typeof raw.wsPort === "number" ? raw.wsPort : 8765,
        wsBind: typeof raw.wsBind === "string" ? raw.wsBind : "0.0.0.0",
        picovoice: raw.picovoice as Esp32AudioConfig["picovoice"],
        openai: raw.openai as Esp32AudioConfig["openai"],
      };
    },
  },

  register(api: OpenClawPluginApi) {
    const config = this.configSchema.parse(api.pluginConfig);
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
    if (!config.openai?.apiKey) {
      log.error("[esp32-audio] openai.apiKey is required");
      return;
    }

    const clients = new Map<string, ClientSession>();
    let wakeWordDetector: WakeWordDetector | null = null;
    let audioProcessor: AudioProcessor | null = null;

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

    // Initialize audio processor
    audioProcessor = new AudioProcessor({
      openaiApiKey: config.openai.apiKey,
      ttsVoice: config.openai.ttsVoice ?? "shimmer",
      ttsModel: config.openai.ttsModel ?? "tts-1",
      onTranscript: (clientId, text) => {
        const client = clients.get(clientId);
        if (client) {
          sendJson(client.ws, { type: "transcript", text });
        }
      },
      onTtsStart: (clientId, sampleRate, byteLength) => {
        const client = clients.get(clientId);
        if (client) {
          sendJson(client.ws, { type: "tts_start", sampleRate, byteLength });
        }
      },
      onTtsChunk: (clientId, chunk) => {
        const client = clients.get(clientId);
        if (client && client.ws.readyState === WebSocket.OPEN) {
          client.ws.send(chunk);
        }
      },
      onTtsEnd: (clientId) => {
        const client = clients.get(clientId);
        if (client) {
          sendJson(client.ws, { type: "tts_end" });
          client.state = "idle";
        }
      },
      onError: (clientId, error) => {
        const client = clients.get(clientId);
        if (client) {
          sendJson(client.ws, { type: "error", message: error });
          client.state = "idle";
        }
      },
      sendToAgent: async (text: string) => {
        // Use OpenClaw's agent system
        // This is a simplified version - real implementation would use proper routing
        try {
          const response = await api.runtime.agent.run({
            messages: [{ role: "user", content: text }],
          });
          return response.content || "";
        } catch (err) {
          log.error(`[esp32-audio] Agent error: ${err}`);
          return "Sorry, I encountered an error processing your request.";
        }
      },
    });

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
            handleControlMessage(client, msg);
          } catch {
            log.warn(`[esp32-audio] Invalid JSON from ${clientId}`);
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

    function handleControlMessage(client: ClientSession, msg: { type: string; [key: string]: unknown }) {
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
          audioProcessor?.processRecording(client.id, audio, client.sampleRate);
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

    function sendJson(ws: WebSocket, obj: unknown) {
      if (ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(obj));
      }
    }

    // Cleanup on shutdown
    api.onShutdown?.(() => {
      log.info("[esp32-audio] Shutting down...");
      wss.close();
      wakeWordDetector?.destroy();
    });
  },
};

export default esp32AudioPlugin;
