#!/usr/bin/env python3
"""
Generate a simple attention "ding" sound as PCM for ESP32
Two-tone chime, 24kHz sample rate, 16-bit signed mono
"""

import math
import struct

def generate_chime(sample_rate=24000, duration_ms=300):
    """Generate a pleasant two-tone chime"""
    samples = []
    total_samples = int(sample_rate * duration_ms / 1000)
    
    # Two tones: C5 (523Hz) then E5 (659Hz)
    freq1, freq2 = 523, 659
    
    for i in range(total_samples):
        t = i / sample_rate
        
        # Envelope: quick attack, gentle decay
        env = math.exp(-t * 6) * min(1.0, t * 50)
        
        # First half: first tone, second half: second tone
        if i < total_samples // 2:
            freq = freq1
        else:
            freq = freq2
        
        # Generate sine wave
        value = math.sin(2 * math.pi * freq * t) * env * 0.7
        
        # Convert to 16-bit signed
        sample = int(value * 32000)
        sample = max(-32768, min(32767, sample))
        samples.append(sample)
    
    return samples

def output_c_array(samples, var_name="attention_sound"):
    """Output as C array"""
    print(f"// {var_name}: {len(samples)} samples, 24kHz, 16-bit signed mono")
    print(f"const int16_t {var_name}[{len(samples)}] PROGMEM = {{")
    
    for i in range(0, len(samples), 16):
        row = samples[i:i+16]
        vals = ", ".join(str(s) for s in row)
        if i + 16 < len(samples):
            print(f"    {vals},")
        else:
            print(f"    {vals}")
    
    print("};")
    print(f"const size_t {var_name}_size = sizeof({var_name});")
    print()

if __name__ == "__main__":
    samples = generate_chime(24000, 400)  # 400ms chime
    output_c_array(samples, "attention_sound")
