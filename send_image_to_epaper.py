#!/usr/bin/env python3
"""
ESP32 E6 E-Paper Image Sender with Fast Dithering
Enhanced version that uses PIL's optimized dithering for better image quality
"""

import os
import sys
import requests
import json
from PIL import Image, ImageOps
import time
import argparse
import numpy as np

# Default configuration
DEFAULT_ESP32_IP = "10.168.1.166"
DEFAULT_IMAGE_PATH = "photo.png"
API_ENDPOINT = "/api/display/frame"
STATUS_ENDPOINT = "/api/status"
TIMEOUT = 30  # seconds

# E6 display specifications
EPD_WIDTH = 800
EPD_HEIGHT = 480
EPD_BUFFER_SIZE = 192000  # 4 bits per pixel (800 * 480 / 2)

class EPaperImageSender:
    def __init__(self, esp32_ip, port=80):
        self.esp32_ip = esp32_ip
        self.port = port
        self.base_url = f"http://{esp32_ip}:{port}"
        
    def check_connection(self):
        """Check if ESP32 is reachable"""
        try:
            print(f"🔍 Checking connection to {self.base_url}...")
            response = requests.get(f"{self.base_url}{STATUS_ENDPOINT}", timeout=5)
            if response.status_code == 200:
                status_data = response.json()
                print(f"✅ Connected to ESP32!")
                print(f"   Device: {status_data.get('service', 'Unknown')}")
                print(f"   Status: {status_data.get('status', 'Unknown')}")
                print(f"   Display Ready: {status_data.get('display_initialized', False)}")
                print(f"   Buffer Size: {status_data.get('buffer_size', 'Unknown')} bytes")
                print(f"   Free Heap: {status_data.get('free_heap', 'Unknown')} bytes")
                return True
            else:
                print(f"❌ ESP32 responded with status {response.status_code}")
                return False
        except requests.RequestException as e:
            print(f"❌ Failed to connect to ESP32: {e}")
            return False
    
    def convert_image_for_epaper(self, image_path, resize_mode='fit', dither_mode='none', debug=False):
        """Convert image to E6 e-paper format with optional dithering"""
        try:
            print(f"🖼️  Processing image: {image_path}")
            
            if not os.path.exists(image_path):
                print(f"❌ Image file not found: {image_path}")
                return None
            
            # Open and process image
            with Image.open(image_path) as img:
                print(f"   Original size: {img.size} ({img.width}x{img.height})")
                print(f"   Original mode: {img.mode}")
                print(f"   Resize mode: {resize_mode}")
                print(f"   Dithering: {dither_mode}")
                
                # Convert to RGB if needed
                if img.mode != 'RGB':
                    img = img.convert('RGB')
                
                # Apply different resizing strategies
                if resize_mode == 'stretch':
                    resized_img = img.resize((EPD_WIDTH, EPD_HEIGHT), Image.Resampling.LANCZOS)
                    print(f"   Stretched to: {resized_img.size}")
                    
                elif resize_mode == 'fill':
                    img_ratio = img.width / img.height
                    display_ratio = EPD_WIDTH / EPD_HEIGHT
                    print(f"   Image ratio: {img_ratio:.3f}, Display ratio: {display_ratio:.3f}")
                    
                    if img_ratio > display_ratio:
                        new_height = EPD_HEIGHT
                        new_width = int(new_height * img_ratio)
                        temp_img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)
                        left = (new_width - EPD_WIDTH) // 2
                        resized_img = temp_img.crop((left, 0, left + EPD_WIDTH, EPD_HEIGHT))
                        print(f"   Cropped from x={left} to x={left + EPD_WIDTH}")
                    else:
                        new_width = EPD_WIDTH
                        new_height = int(new_width / img_ratio)
                        temp_img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)
                        top = (new_height - EPD_HEIGHT) // 2
                        resized_img = temp_img.crop((0, top, EPD_WIDTH, top + EPD_HEIGHT))
                        print(f"   Cropped from y={top} to y={top + EPD_HEIGHT}")
                        
                else:  # 'fit' mode (default)
                    img.thumbnail((EPD_WIDTH, EPD_HEIGHT), Image.Resampling.LANCZOS)
                    print(f"   Thumbnail size: {img.size}")
                    
                    resized_img = Image.new('RGB', (EPD_WIDTH, EPD_HEIGHT), 'white')
                    x = (EPD_WIDTH - img.width) // 2
                    y = (EPD_HEIGHT - img.height) // 2
                    resized_img.paste(img, (x, y))
                    print(f"   Centered at: ({x}, {y})")
                
                if resized_img.size != (EPD_WIDTH, EPD_HEIGHT):
                    print(f"❌ Size error! Got {resized_img.size}, expected ({EPD_WIDTH}, {EPD_HEIGHT})")
                    return None
                
                print(f"✅ Final size: {resized_img.size}")
                
                if debug:
                    debug_path = f"debug_resized_{resize_mode}.png"
                    resized_img.save(debug_path)
                    print(f"   Debug image saved: {debug_path}")
                
                # Define E6 palette
                e6_palette = [
                    (0x0, (0, 0, 0)),           # BLACK
                    (0x1, (255, 255, 255)),     # WHITE  
                    (0x2, (255, 255, 0)),       # YELLOW
                    (0x3, (255, 0, 0)),         # RED
                    (0x5, (0, 0, 255)),         # BLUE
                    (0x6, (0, 255, 0))          # GREEN
                ]
                
                # Apply quantization based on dither mode
                if dither_mode == 'fast':
                    quantized_img = self.pil_dither_to_e6(resized_img, e6_palette, debug)
                elif dither_mode == 'quality':
                    quantized_img = self.floyd_steinberg_dither(resized_img, e6_palette, debug)
                else:  # 'none'
                    quantized_img = self.quantize_to_e6_palette(resized_img, e6_palette, debug)
                
                # Convert to E6 format
                raw_data = self.convert_to_e6_format(quantized_img, e6_palette)
                
                print(f"   Raw data size: {len(raw_data)} bytes")
                print(f"   Expected size: {EPD_BUFFER_SIZE} bytes")
                
                if len(raw_data) != EPD_BUFFER_SIZE:
                    print(f"❌ Size mismatch! Expected {EPD_BUFFER_SIZE}, got {len(raw_data)}")
                    return None
                
                if debug:
                    print(f"   First 16 bytes: {' '.join(f'{b:02X}' for b in raw_data[:16])}")
                    print(f"   Last 16 bytes:  {' '.join(f'{b:02X}' for b in raw_data[-16:])}")
                
                return raw_data
                    
        except Exception as e:
            print(f"❌ Error processing image: {e}")
            import traceback
            traceback.print_exc()
            return None

    def pil_dither_to_e6(self, img, palette, debug=False):
        """Use PIL's built-in dithering - FAST"""
        print("   🚀 Applying PIL dithering (fast method)...")
        
        # Create a PIL palette image
        palette_img = Image.new('P', (1, 1))
        
        # Flatten the palette for PIL format
        pil_palette = []
        for code, (r, g, b) in palette:
            pil_palette.extend([r, g, b])
        
        # Pad palette to 256 colors (PIL requirement)
        while len(pil_palette) < 768:  # 256 * 3
            pil_palette.extend([0, 0, 0])
        
        palette_img.putpalette(pil_palette)
        
        # Apply dithering using PIL's built-in Floyd-Steinberg
        print("      Converting and dithering...")
        dithered = img.quantize(palette=palette_img, dither=Image.Dither.FLOYDSTEINBERG)
        
        # Convert back to RGB
        quantized = dithered.convert('RGB')
        
        # Snap to exact palette colors (PIL might introduce variations)
        width, height = quantized.size
        pixels = quantized.load()
        
        for y in range(height):
            for x in range(width):
                rgb = pixels[x, y]
                # Find closest exact match
                min_dist = float('inf')
                best_color = palette[0][1]
                for code, target_rgb in palette:
                    dist = sum((a - b) ** 2 for a, b in zip(rgb, target_rgb))
                    if dist < min_dist:
                        min_dist = dist
                        best_color = target_rgb
                pixels[x, y] = best_color
        
        # Count colors
        color_counts = {color_code: 0 for color_code, _ in palette}
        rgb_to_code = {rgb: code for code, rgb in palette}
        
        for y in range(height):
            for x in range(width):
                rgb = pixels[x, y]
                code = rgb_to_code.get(rgb, 0x0)
                color_counts[code] += 1
        
        # Print color distribution
        total_pixels = width * height
        print("   Color distribution (PIL dithering):")
        color_names = {0x0: "BLACK", 0x1: "WHITE", 0x2: "YELLOW", 0x3: "RED", 0x5: "BLUE", 0x6: "GREEN"}
        for code, count in color_counts.items():
            percentage = (count / total_pixels) * 100
            print(f"     {color_names.get(code, f'0x{code:X}')}: {count:6d} pixels ({percentage:5.1f}%)")
        
        if debug:
            debug_path = f"debug_pil_dithered.png"
            quantized.save(debug_path)
            print(f"   PIL dithered debug image saved: {debug_path}")
        
        return quantized

    def floyd_steinberg_dither(self, img, palette, debug=False):
        """Custom Floyd-Steinberg implementation - slower but more control"""
        print("   🎨 Applying custom Floyd-Steinberg dithering...")
        
        width, height = img.size
        img_array = np.array(img, dtype=np.float64)
        palette_colors = np.array([rgb for _, rgb in palette])
        palette_codes = [code for code, _ in palette]
        
        result_array = np.zeros((height, width, 3), dtype=np.uint8)
        color_counts = {color_code: 0 for color_code, _ in palette}
        
        print(f"      Processing {width}x{height} pixels...")
        
        for y in range(height):
            if y % 100 == 0:
                print(f"      Row {y}/{height} ({y*100//height}%)")
            
            for x in range(width):
                old_pixel = img_array[y, x]
                
                # Find closest color
                distances = np.sum((palette_colors - old_pixel) ** 2, axis=1)
                best_idx = np.argmin(distances)
                best_color = palette_colors[best_idx]
                best_code = palette_codes[best_idx]
                
                result_array[y, x] = best_color
                color_counts[best_code] += 1
                
                # Error diffusion
                error = old_pixel - best_color
                
                if x + 1 < width:
                    img_array[y, x + 1] += error * (7.0/16.0)
                if y + 1 < height:
                    if x > 0:
                        img_array[y + 1, x - 1] += error * (3.0/16.0)
                    img_array[y + 1, x] += error * (5.0/16.0)
                    if x + 1 < width:
                        img_array[y + 1, x + 1] += error * (1.0/16.0)
                
                img_array = np.clip(img_array, 0, 255)
        
        quantized = Image.fromarray(result_array, 'RGB')
        
        # Print color distribution
        total_pixels = width * height
        print("   Color distribution (custom dithering):")
        color_names = {0x0: "BLACK", 0x1: "WHITE", 0x2: "YELLOW", 0x3: "RED", 0x5: "BLUE", 0x6: "GREEN"}
        for code, count in color_counts.items():
            percentage = (count / total_pixels) * 100
            print(f"     {color_names.get(code, f'0x{code:X}')}: {count:6d} pixels ({percentage:5.1f}%)")
        
        if debug:
            debug_path = f"debug_custom_dithered.png"
            quantized.save(debug_path)
            print(f"   Custom dithered debug image saved: {debug_path}")
        
        return quantized

    def quantize_to_e6_palette(self, img, palette, debug=False):
        """Simple nearest-color quantization - no dithering"""
        print("   🎨 Quantizing to E6 palette (no dithering)...")
        
        width, height = img.size
        pixels = img.load()
        
        quantized = Image.new('RGB', (width, height))
        quantized_pixels = quantized.load()
        
        color_counts = {color_code: 0 for color_code, _ in palette}
        
        for y in range(height):
            for x in range(width):
                r, g, b = pixels[x, y]
                
                min_distance = float('inf')
                best_color = palette[0][1]
                best_code = palette[0][0]
                
                for color_code, rgb in palette:
                    distance = (
                        0.299 * (r - rgb[0]) ** 2 +
                        0.587 * (g - rgb[1]) ** 2 +
                        0.114 * (b - rgb[2]) ** 2
                    ) ** 0.5
                    
                    if distance < min_distance:
                        min_distance = distance
                        best_color = rgb
                        best_code = color_code
                
                quantized_pixels[x, y] = best_color
                color_counts[best_code] += 1
        
        # Print color distribution
        total_pixels = width * height
        print("   Color distribution (no dithering):")
        color_names = {0x0: "BLACK", 0x1: "WHITE", 0x2: "YELLOW", 0x3: "RED", 0x5: "BLUE", 0x6: "GREEN"}
        for code, count in color_counts.items():
            percentage = (count / total_pixels) * 100
            print(f"     {color_names.get(code, f'0x{code:X}')}: {count:6d} pixels ({percentage:5.1f}%)")
        
        if debug:
            debug_path = f"debug_quantized.png"
            quantized.save(debug_path)
            print(f"   Quantized debug image saved: {debug_path}")
        
        return quantized

    def convert_to_e6_format(self, img, palette):
        """Convert quantized image to E6 4-bit format"""
        print("   Converting to E6 4-bit format...")
        
        width, height = img.size
        pixels = img.load()
        
        if width != EPD_WIDTH or height != EPD_HEIGHT:
            raise ValueError(f"Image must be exactly {EPD_WIDTH}x{EPD_HEIGHT}, got {width}x{height}")
        
        raw_data = bytearray()
        
        rgb_to_code = {}
        for color_code, rgb in palette:
            rgb_to_code[rgb] = color_code
        
        bytes_written = 0
        for y in range(height):
            for x in range(0, width, 2):
                r1, g1, b1 = pixels[x, y]
                color1 = rgb_to_code.get((r1, g1, b1), 0x0)
                
                if x + 1 < width:
                    r2, g2, b2 = pixels[x + 1, y]
                    color2 = rgb_to_code.get((r2, g2, b2), 0x1)
                else:
                    color2 = 0x1
                
                byte_value = (color1 << 4) | color2
                raw_data.append(byte_value)
                bytes_written += 1
        
        print(f"   Packed {bytes_written} bytes from {width}x{height} pixels")
        
        expected_bytes = (EPD_WIDTH * EPD_HEIGHT) // 2
        if len(raw_data) != expected_bytes:
            raise ValueError(f"Buffer size mismatch: expected {expected_bytes}, got {len(raw_data)}")
        
        return bytes(raw_data)

    def create_test_pattern(self):
        """Create E6 test pattern"""
        print("📊 Creating E6 test pattern...")
        
        raw_data = bytearray()
        colors = [0x0, 0x1, 0x2, 0x3, 0x5, 0x6]
        
        for y in range(EPD_HEIGHT):
            color_index = min(y // 80, 5)
            color = colors[color_index]
            
            for x in range(EPD_WIDTH // 2):
                byte_value = (color << 4) | color
                raw_data.append(byte_value)
        
        print(f"   Test pattern size: {len(raw_data)} bytes")
        return bytes(raw_data)
    
    def send_image_data(self, image_data):
        """Send raw image data to ESP32"""
        try:
            print(f"📡 Sending image data to ESP32...")
            print(f"   Endpoint: {self.base_url}{API_ENDPOINT}")
            print(f"   Data size: {len(image_data)} bytes")
            
            files = {
                'file': ('image.bin', image_data, 'application/octet-stream')
            }
            
            start_time = time.time()
            
            response = requests.post(
                f"{self.base_url}{API_ENDPOINT}",
                files=files,
                timeout=TIMEOUT
            )
            
            elapsed_time = time.time() - start_time
            
            print(f"   Response status: {response.status_code}")
            
            if response.status_code == 200:
                try:
                    response_data = response.json()
                    print(f"✅ Image sent successfully!")
                    print(f"   Response: {response_data.get('message', 'Success')}")
                    print(f"   Upload time: {elapsed_time:.2f} seconds")
                except:
                    print(f"✅ Image sent successfully!")
                    print(f"   Response text: {response.text}")
                    print(f"   Upload time: {elapsed_time:.2f} seconds")
                return True
            else:
                print(f"❌ Failed to send image. Status: {response.status_code}")
                try:
                    error_data = response.json()
                    print(f"   Error: {error_data.get('error', 'Unknown error')}")
                except:
                    print(f"   Response text: {response.text}")
                return False
                
        except requests.Timeout:
            print(f"❌ Request timed out after {TIMEOUT} seconds")
            return False
        except requests.RequestException as e:
            print(f"❌ Network error: {e}")
            return False
    
    def send_test_pattern(self):
        """Send E6 test pattern"""
        test_data = self.create_test_pattern()
        return self.send_image_data(test_data)

def main():
    parser = argparse.ArgumentParser(description='Send image to ESP32 E6 E-Paper display')
    parser.add_argument('--ip', default=DEFAULT_ESP32_IP, help='ESP32 IP address')
    parser.add_argument('--image', default=DEFAULT_IMAGE_PATH, help='Image file path')
    parser.add_argument('--test', action='store_true', help='Send test pattern instead of image')
    parser.add_argument('--resize', choices=['fit', 'fill', 'stretch'], default='fit',
                       help='Resize mode: fit (maintain ratio), fill (crop to fill), stretch (may distort)')
    parser.add_argument('--dither', choices=['fast', 'quality', 'none'], default='none',
                       help='Dithering method: fast (PIL), quality (Floyd-Steinberg), none (nearest color)')
    parser.add_argument('--debug', action='store_true', help='Save debug images and show detailed output')
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("🖼️  ESP32 E6 E-Paper Image Sender")
    if args.dither != 'none':
        print(f"🎨 With {args.dither.upper()} dithering")
    print("=" * 60)
    print(f"Display: 7.3\" E6 Spectra 6-color ({EPD_WIDTH}x{EPD_HEIGHT})")
    print(f"Buffer size: {EPD_BUFFER_SIZE} bytes")
    print()
    
    sender = EPaperImageSender(args.ip)
    
    if not sender.check_connection():
        print("\n💡 Troubleshooting tips:")
        print("   1. Make sure ESP32 is powered on and connected to WiFi")
        print("   2. Check that you're on the same network as the ESP32")
        print("   3. Verify the IP address is correct")
        print(f"   4. Try accessing http://{args.ip}/api/status in your web browser")
        sys.exit(1)
    
    success = False
    
    if args.test:
        print(f"\n🧪 Sending E6 test pattern...")
        success = sender.send_test_pattern()
    else:
        print(f"\n🖼️  Processing image: {args.image}")
        image_data = sender.convert_image_for_epaper(args.image, args.resize, args.dither, args.debug)
        if image_data:
            success = sender.send_image_data(image_data)
        else:
            print(f"❌ Failed to process image: {args.image}")
            sys.exit(1)
    
    print("\n" + "=" * 60)
    if success:
        print("🎉 Image sent successfully!")
        if args.dither != 'none':
            print("   Dithering should improve image quality on the display")
        print("   The E6 display should update shortly...")
        if args.debug:
            print("   Debug images saved in current directory")
    else:
        print("❌ Failed to send image")
    print("=" * 60)

if __name__ == "__main__":
    main()