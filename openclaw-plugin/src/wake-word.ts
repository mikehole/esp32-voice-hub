/**
 * Wake Word Detection using Picovoice Porcupine
 */

import { Porcupine } from "@picovoice/porcupine-node";

export interface WakeWordConfig {
  accessKey: string;
  modelPath: string;
  sensitivity: number;
  onWakeWord: (clientId: string) => void;
}

interface ClientState {
  buffer: Int16Array;
  bufferPos: number;
  lastDetection: number;
}

// Cooldown between detections (ms)
const DETECTION_COOLDOWN_MS = 1500;

export class WakeWordDetector {
  private porcupine: Porcupine | null = null;
  private config: WakeWordConfig;
  private clients = new Map<string, ClientState>();
  private frameLength: number = 512;

  constructor(config: WakeWordConfig) {
    this.config = config;
    this.init();
  }

  private init() {
    try {
      this.porcupine = new Porcupine(
        this.config.accessKey,
        [this.config.modelPath],
        [this.config.sensitivity]
      );
      this.frameLength = this.porcupine.frameLength;
      console.log(`[WakeWord] Initialized with frameLength=${this.frameLength}`);
    } catch (err) {
      console.error(`[WakeWord] Failed to initialize Porcupine: ${err}`);
      throw err;
    }
  }

  /**
   * Process incoming audio from a client.
   * Audio should be 16kHz mono 16-bit PCM.
   */
  processAudio(clientId: string, data: Buffer) {
    if (!this.porcupine) return;

    // Get or create client state
    let state = this.clients.get(clientId);
    if (!state) {
      state = {
        buffer: new Int16Array(this.frameLength),
        bufferPos: 0,
        lastDetection: 0,
      };
      this.clients.set(clientId, state);
    }

    // Convert Buffer to Int16Array
    const samples = new Int16Array(
      data.buffer,
      data.byteOffset,
      data.length / 2
    );

    // Process samples, accumulating into frame buffer
    for (let i = 0; i < samples.length; i++) {
      state.buffer[state.bufferPos++] = samples[i];

      // When we have a full frame, process it
      if (state.bufferPos >= this.frameLength) {
        const keywordIndex = this.porcupine.process(state.buffer);

        if (keywordIndex >= 0) {
          const now = Date.now();
          if (now - state.lastDetection > DETECTION_COOLDOWN_MS) {
            state.lastDetection = now;
            this.config.onWakeWord(clientId);
          }
        }

        state.bufferPos = 0;
      }
    }
  }

  /**
   * Remove a client's state.
   */
  removeClient(clientId: string) {
    this.clients.delete(clientId);
  }

  /**
   * Clean up resources.
   */
  destroy() {
    if (this.porcupine) {
      this.porcupine.release();
      this.porcupine = null;
    }
    this.clients.clear();
  }
}
