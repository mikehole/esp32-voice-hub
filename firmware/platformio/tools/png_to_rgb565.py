#!/usr/bin/env python3
"""
Convert PNG to RGB565 C array for ESP32 LVGL
"""

import sys
from PIL import Image

def png_to_rgb565(input_path, var_name):
    """Convert PNG to RGB565 C array"""
    img = Image.open(input_path).convert('RGB')
    width, height = img.size
    
    pixels = []
    for y in range(height):
        for x in range(width):
            r, g, b = img.getpixel((x, y))
            # Convert to RGB565
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            pixels.append(rgb565)
    
    # Output C array
    print(f"// {var_name}: {width}x{height} RGB565")
    print(f"const uint16_t {var_name}[{len(pixels)}] PROGMEM = {{")
    
    for i in range(0, len(pixels), 16):
        row = pixels[i:i+16]
        hex_vals = ", ".join(f"0x{p:04X}" for p in row)
        if i + 16 < len(pixels):
            print(f"    {hex_vals},")
        else:
            print(f"    {hex_vals}")
    
    print("};")
    print()
    return width, height, len(pixels)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: png_to_rgb565.py <input.png> <var_name>")
        sys.exit(1)
    
    png_to_rgb565(sys.argv[1], sys.argv[2])
