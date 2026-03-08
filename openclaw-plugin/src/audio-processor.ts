/**
 * Audio Processing: STT (Whisper) → Agent → TTS (OpenAI)
 */

import { writeFileSync, unlinkSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

export interface AudioProcessorConfig {
  openaiApiKey: string;
  ttsVoice: string;
  ttsModel: string;
  onTranscript: (clientId: string, text: string) => void;
  onTtsStart: (clientId: string, sampleRate: number, byteLength: number) => void;
  onTtsChunk: (clientId: string, chunk: Buffer) => void;
  onTtsEnd: (clientId: string) => void;
  onError: (clientId: string, error: string) => void;
  sendToAgent: (text: string) => Promise<string>;
}

export class AudioProcessor {
  private config: AudioProcessorConfig;

  constructor(config: AudioProcessorConfig) {
    this.config = config;
  }

  /**
   * Process a complete recording: STT → Agent → TTS
   */
  async processRecording(clientId: string, audio: Buffer, sampleRate: number) {
    try {
      // Step 1: Transcribe with Whisper
      const transcript = await this.transcribe(audio, sampleRate);
      if (!transcript || transcript.trim().length === 0) {
        this.config.onError(clientId, "No speech detected");
        return;
      }

      console.log(`[AudioProcessor] Transcript: ${transcript}`);
      this.config.onTranscript(clientId, transcript);

      // Step 2: Send to agent
      const response = await this.config.sendToAgent(transcript);
      if (!response || response.trim().length === 0) {
        this.config.onError(clientId, "No response from agent");
        return;
      }

      console.log(`[AudioProcessor] Response: ${response.substring(0, 100)}...`);

      // Step 3: Generate TTS
      await this.synthesize(clientId, response);

    } catch (err) {
      console.error(`[AudioProcessor] Error: ${err}`);
      this.config.onError(clientId, `Processing error: ${err}`);
    }
  }

  /**
   * Transcribe audio using OpenAI Whisper API.
   */
  private async transcribe(audio: Buffer, sampleRate: number): Promise<string> {
    // Create WAV file
    const wavPath = join(tmpdir(), `esp32-${Date.now()}.wav`);
    const wavBuffer = this.createWav(audio, sampleRate);
    writeFileSync(wavPath, wavBuffer);

    try {
      const formData = new FormData();
      const blob = new Blob([wavBuffer], { type: "audio/wav" });
      formData.append("file", blob, "audio.wav");
      formData.append("model", "whisper-1");
      formData.append("language", "en");

      const response = await fetch("https://api.openai.com/v1/audio/transcriptions", {
        method: "POST",
        headers: {
          Authorization: `Bearer ${this.config.openaiApiKey}`,
        },
        body: formData,
      });

      if (!response.ok) {
        const error = await response.text();
        throw new Error(`Whisper API error: ${response.status} ${error}`);
      }

      const result = await response.json() as { text?: string };
      return result.text || "";

    } finally {
      try {
        unlinkSync(wavPath);
      } catch {
        // Ignore cleanup errors
      }
    }
  }

  /**
   * Synthesize speech using OpenAI TTS API.
   */
  private async synthesize(clientId: string, text: string) {
    const response = await fetch("https://api.openai.com/v1/audio/speech", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${this.config.openaiApiKey}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        model: this.config.ttsModel,
        input: text,
        voice: this.config.ttsVoice,
        response_format: "pcm", // Raw 24kHz 16-bit mono PCM
      }),
    });

    if (!response.ok) {
      const error = await response.text();
      throw new Error(`TTS API error: ${response.status} ${error}`);
    }

    // Get the audio data
    const arrayBuffer = await response.arrayBuffer();
    const pcm24k = Buffer.from(arrayBuffer);

    // Resample from 24kHz to 16kHz for ESP32
    const pcm16k = this.resample(pcm24k, 24000, 16000);

    // Send to client
    this.config.onTtsStart(clientId, 16000, pcm16k.length);

    // Send in chunks
    const CHUNK_SIZE = 1024;
    for (let i = 0; i < pcm16k.length; i += CHUNK_SIZE) {
      const chunk = pcm16k.slice(i, Math.min(i + CHUNK_SIZE, pcm16k.length));
      this.config.onTtsChunk(clientId, chunk);
    }

    this.config.onTtsEnd(clientId);
  }

  /**
   * Create a WAV file buffer from raw PCM data.
   */
  private createWav(pcm: Buffer, sampleRate: number): Buffer {
    const header = Buffer.alloc(44);

    // RIFF header
    header.write("RIFF", 0);
    header.writeUInt32LE(36 + pcm.length, 4);
    header.write("WAVE", 8);

    // fmt chunk
    header.write("fmt ", 12);
    header.writeUInt32LE(16, 16); // chunk size
    header.writeUInt16LE(1, 20);  // PCM format
    header.writeUInt16LE(1, 22);  // mono
    header.writeUInt32LE(sampleRate, 24);
    header.writeUInt32LE(sampleRate * 2, 28); // byte rate
    header.writeUInt16LE(2, 32);  // block align
    header.writeUInt16LE(16, 34); // bits per sample

    // data chunk
    header.write("data", 36);
    header.writeUInt32LE(pcm.length, 40);

    return Buffer.concat([header, pcm]);
  }

  /**
   * Simple linear resampling.
   */
  private resample(audio: Buffer, fromRate: number, toRate: number): Buffer {
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
}
