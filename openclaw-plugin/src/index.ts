/**
 * ESP32 Audio Terminal Plugin
 *
 * Provides WebSocket server for ESP32 Voice Hub devices with:
 * - Speech-to-text via OpenClaw runtime
 * - Agent integration via OpenClaw
 * - Text-to-speech via OpenClaw runtime
 * 
 * Note: Wake word detection runs on the ESP32 device itself (ESP-SR),
 * not on the server side.
 */

import type { OpenClawPluginApi } from "openclaw/plugin-sdk";
import { WebSocketServer, WebSocket } from "ws";
import { writeFileSync, unlinkSync, createReadStream } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { FormData } from "formdata-node";
import { fileFromPath } from "formdata-node/file-from-path";
import { createServer, IncomingMessage, ServerResponse } from "node:http";

export interface Esp32AudioConfig {
  enabled: boolean;
  wsPort: number;
  wsBind: string;
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
  };
}

const esp32AudioPlugin = {
  id: "esp32-audio",
  name: "ESP32 Audio Terminal",
  description: "ESP32 Voice Hub with STT/TTS",

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

    const clients = new Map<string, ClientSession>();
    const pendingCallbacks = new Map<string, ClientSession>();
    
    // HTTP server for receiving TTS callbacks from the agent
    const HTTP_PORT = 8766;
    const httpServer = createServer(async (req: IncomingMessage, res: ServerResponse) => {
      // Handle callback POSTs: /callback/<id>
      if (req.method === "POST" && req.url?.startsWith("/callback/")) {
        const callbackId = req.url.split("/callback/")[1];
        const client = pendingCallbacks.get(callbackId);
        
        if (!client) {
          log.warn(`[esp32-audio] Unknown callback: ${callbackId}`);
          res.writeHead(404);
          res.end("Unknown callback");
          return;
        }
        
        // Collect the audio data
        const chunks: Buffer[] = [];
        for await (const chunk of req) {
          chunks.push(Buffer.from(chunk));
        }
        const audioData = Buffer.concat(chunks);
        
        log.info(`[esp32-audio] Received callback ${callbackId}: ${audioData.length} bytes`);
        pendingCallbacks.delete(callbackId);
        
        // Send audio to the ESP32 client
        if (audioData.length > 0 && client.ws.readyState === WebSocket.OPEN) {
          // The TTS tool outputs MP3/OGG — convert to PCM using ffmpeg
          const { execSync } = await import("node:child_process");
          const inputPath = `/tmp/tts-input-${callbackId}`;
          const outputPath = `/tmp/tts-output-${callbackId}.pcm`;
          
          writeFileSync(inputPath, audioData);
          
          try {
            // Convert to 24kHz 16-bit mono PCM
            execSync(`ffmpeg -y -i ${inputPath} -f s16le -acodec pcm_s16le -ar 24000 -ac 1 ${outputPath}`, { stdio: 'pipe' });
            const pcmData = await import("node:fs").then(fs => fs.readFileSync(outputPath));
            
            log.info(`[esp32-audio] Converted to PCM: ${pcmData.length} bytes`);
            
            sendJson(client.ws, { type: "audio_stream_start", sampleRate: 24000 });
            
            const CHUNK_SIZE = 4096;
            for (let i = 0; i < pcmData.length; i += CHUNK_SIZE) {
              const chunk = pcmData.slice(i, Math.min(i + CHUNK_SIZE, pcmData.length));
              client.ws.send(chunk);
            }
            
            sendJson(client.ws, { type: "audio_stream_end" });
            
            // Cleanup
            unlinkSync(inputPath);
            unlinkSync(outputPath);
          } catch (err) {
            log.error(`[esp32-audio] FFmpeg conversion failed: ${err}`);
            sendJson(client.ws, { type: "error", message: "Audio conversion failed" });
          }
        }
        
        res.writeHead(200);
        res.end("OK");
        return;
      }
      
      res.writeHead(404);
      res.end("Not found");
    });
    
    httpServer.listen(HTTP_PORT, "0.0.0.0", () => {
      log.info(`[esp32-audio] HTTP callback server listening on 0.0.0.0:${HTTP_PORT}`);
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

      ws.on("message", async (data, isBinary) => {
        const buf = Buffer.isBuffer(data) ? data : Buffer.from(data as ArrayBuffer);
        
        // Detect JSON vs binary by first byte
        const looksLikeJson = buf[0] === 0x7B; // '{' character
        
        if (looksLikeJson) {
          // JSON control message
          try {
            const text = buf.toString("utf8");
            const msg = JSON.parse(text);
            await handleControlMessage(client, msg);
          } catch (err) {
            log.warn(`[esp32-audio] Error handling message from ${clientId}: ${err}`);
          }
        } else {
          // Binary audio data
          handleAudioData(client, buf);
        }
      });

      ws.on("close", () => {
        log.info(`[esp32-audio] Client disconnected: ${clientId}`);
        clients.delete(clientId);
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

        case "speak":
          // Direct TTS request - speak text without agent processing
          const text = msg.text as string;
          if (text) {
            log.info(`[esp32-audio] Speak request from ${client.id}: ${text}`);
            await speakText(client, text);
          }
          break;
      }
    }

    async function speakText(client: ClientSession, text: string) {
      try {
        log.info(`[esp32-audio] Calling TTS for: "${text}"`);
        
        const ttsResult = await api.runtime.tts.textToSpeechTelephony({
          text,
          cfg: api.config as Parameters<typeof api.runtime.tts.textToSpeechTelephony>[0]["cfg"],
        });

        if (ttsResult?.success && ttsResult.audioBuffer) {
          const sampleRate = ttsResult.sampleRate || 24000;
          const pcm = ttsResult.audioBuffer;

          log.info(`[esp32-audio] Sending TTS audio: ${pcm.length} bytes @ ${sampleRate}Hz`);

          sendJson(client.ws, {
            type: "audio_stream_start",
            sampleRate,
          });

          const CHUNK_SIZE = 4096;
          for (let i = 0; i < pcm.length; i += CHUNK_SIZE) {
            const chunk = pcm.slice(i, Math.min(i + CHUNK_SIZE, pcm.length));
            if (client.ws.readyState === WebSocket.OPEN) {
              client.ws.send(chunk);
            }
          }

          sendJson(client.ws, { type: "audio_stream_end" });
        } else {
          log.warn(`[esp32-audio] TTS failed for text: ${text}`);
          sendJson(client.ws, { type: "error", message: "TTS failed" });
        }
      } catch (err) {
        log.error(`[esp32-audio] Speak error: ${err}`);
        sendJson(client.ws, { type: "error", message: `TTS error: ${err}` });
      }
    }

    function handleAudioData(client: ClientSession, data: Buffer) {
      if (client.state === "recording") {
        client.audioBuffer.push(data);
      }
    }

    async function processRecording(client: ClientSession, audio: Buffer) {
      try {
        // Create WAV file for transcription (fixed path for debugging)
        const wavPath = `/tmp/esp32-last-recording.wav`;
        const wavBuffer = createWav(audio, client.sampleRate);
        writeFileSync(wavPath, wavBuffer);

        try {
          // Step 1: Transcribe using OpenAI Whisper API directly
          const sttStart = Date.now();
          log.info(`[esp32-audio] Calling Whisper API for ${wavPath} (${wavBuffer.length} bytes)`);
          
          // Get API key from config - check multiple locations
          const cfg = api.config as any;
          const apiKey = cfg?.providers?.openai?.apiKey 
            || cfg?.messages?.tts?.openai?.apiKey
            || cfg?.skills?.["openai-whisper-api"]?.apiKey
            || process.env.OPENAI_API_KEY;
          
          log.info(`[esp32-audio] API key found: ${apiKey ? 'yes (' + apiKey.slice(0,10) + '...)' : 'NO'}`);
          log.info(`[esp32-audio] Config paths checked: providers.openai=${!!cfg?.providers?.openai?.apiKey}, messages.tts.openai=${!!cfg?.messages?.tts?.openai?.apiKey}`);
          
          if (!apiKey) {
            log.error(`[esp32-audio] No OpenAI API key found!`);
            sendJson(client.ws, { type: "error", message: "No API key configured" });
            return;
          }
          
          // Call Whisper API directly
          const form = new FormData();
          form.set("file", await fileFromPath(wavPath, "recording.wav"));
          form.set("model", "whisper-1");
          
          const response = await fetch("https://api.openai.com/v1/audio/transcriptions", {
            method: "POST",
            headers: {
              "Authorization": `Bearer ${apiKey}`,
            },
            body: form as any,
          });
          
          if (!response.ok) {
            const errText = await response.text();
            log.error(`[esp32-audio] Whisper API error: ${response.status} ${errText}`);
            sendJson(client.ws, { type: "error", message: `Whisper API error: ${response.status}` });
            return;
          }
          
          const result = await response.json() as { text?: string };
          const transcript = result?.text;
          
          if (!transcript || transcript.trim().length === 0) {
            log.info(`[esp32-audio] STT (${Date.now() - sttStart}ms): no speech detected`);
            sendJson(client.ws, { type: "error", message: "No speech detected" });
            return;
          }

          log.info(`[esp32-audio] STT (${Date.now() - sttStart}ms): "${transcript}"`);
          sendJson(client.ws, { type: "transcript", text: transcript });

          // Step 2: Send to Minerva via OpenClaw hooks system
          log.info(`[esp32-audio] Sending to Minerva via hooks...`);
          
          const callbackId = `voice-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`;
          const callbackUrl = `http://localhost:${HTTP_PORT}/callback/${callbackId}`;
          
          // Store client for callback
          pendingCallbacks.set(callbackId, client);
          
          // Get hooks token from config
          const hooksToken = (cfg?.hooks?.token) as string | undefined;
          
          if (!hooksToken) {
            log.error(`[esp32-audio] No hooks token configured!`);
            sendJson(client.ws, { type: "error", message: "No hooks token" });
            pendingCallbacks.delete(callbackId);
            return;
          }
          
          // POST to OpenClaw hooks endpoint
          const gatewayPort = (cfg?.gateway?.port) || 18789;
          const hookResponse = await fetch(`http://127.0.0.1:${gatewayPort}/hooks/voice-stream`, {
            method: "POST",
            headers: {
              "Authorization": `Bearer ${hooksToken}`,
              "Content-Type": "application/json",
            },
            body: JSON.stringify({
              transcript,
              callback_url: callbackUrl,
            }),
          });
          
          if (!hookResponse.ok) {
            const errText = await hookResponse.text();
            log.error(`[esp32-audio] Hook error: ${hookResponse.status} ${errText}`);
            sendJson(client.ws, { type: "error", message: "Hook failed" });
            pendingCallbacks.delete(callbackId);
            return;
          }
          
          log.info(`[esp32-audio] Hook triggered, waiting for agent callback at ${callbackUrl}`);
          
          // Timeout after 60s
          setTimeout(() => {
            if (pendingCallbacks.has(callbackId)) {
              log.warn(`[esp32-audio] Callback timeout for ${callbackId}`);
              pendingCallbacks.delete(callbackId);
              sendJson(client.ws, { type: "error", message: "Response timeout" });
            }
          }, 60000);
          
          // Don't generate TTS here - agent will POST it to callback
          return;

        } finally {
          // Keep file for debugging - don't delete
          log.info(`[esp32-audio] Keeping WAV file for debug: ${wavPath}`);
        }

      } catch (err) {
        log.error(`[esp32-audio] Processing error: ${err}`);
        sendJson(client.ws, { type: "error", message: `Processing error: ${err}` });
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
  },
};

export default esp32AudioPlugin;
