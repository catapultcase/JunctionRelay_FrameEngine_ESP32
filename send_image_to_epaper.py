#!/usr/bin/env python3
"""
ESP32 E6 E-Paper Image Sender
Sends images to ESP32 E6 e-paper display via HTTP API
"""

import os
import sys
import requests
import json
from PIL import Image, ImageOps
import time
import argparse

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
    
    def convert_image_for_epaper(self, image_path, resize_mode='fit', debug=False):
        """Convert image to E6 e-paper format with proper sizing and debugging"""
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
                
                # Convert to RGB if needed
                if img.mode != 'RGB':
                    img = img.convert('RGB')
                
                # Apply different resizing strategies with exact dimensions
                if resize_mode == 'stretch':
                    # Stretch to exact display size
                    resized_img = img.resize((EPD_WIDTH, EPD_HEIGHT), Image.Resampling.LANCZOS)
                    print(f"   Stretched to: {resized_img.size}")
                    
                elif resize_mode == 'fill':
                    # Fill entire display, crop if needed
                    img_ratio = img.width / img.height
                    display_ratio = EPD_WIDTH / EPD_HEIGHT
                    print(f"   Image ratio: {img_ratio:.3f}, Display ratio: {display_ratio:.3f}")
                    
                    if img_ratio > display_ratio:
                        # Image is wider - fit by height and crop width
                        new_height = EPD_HEIGHT
                        new_width = int(new_height * img_ratio)
                        temp_img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)
                        print(f"   Temp resize: {temp_img.size}")
                        
                        # Crop to center
                        left = (new_width - EPD_WIDTH) // 2
                        resized_img = temp_img.crop((left, 0, left + EPD_WIDTH, EPD_HEIGHT))
                        print(f"   Cropped from x={left} to x={left + EPD_WIDTH}")
                    else:
                        # Image is taller - fit by width and crop height
                        new_width = EPD_WIDTH
                        new_height = int(new_width / img_ratio)
                        temp_img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)
                        print(f"   Temp resize: {temp_img.size}")
                        
                        # Crop to center
                        top = (new_height - EPD_HEIGHT) // 2
                        resized_img = temp_img.crop((0, top, EPD_WIDTH, top + EPD_HEIGHT))
                        print(f"   Cropped from y={top} to y={top + EPD_HEIGHT}")
                        
                else:  # 'fit' mode (default)
                    # Calculate thumbnail size that fits within display
                    img.thumbnail((EPD_WIDTH, EPD_HEIGHT), Image.Resampling.LANCZOS)
                    print(f"   Thumbnail size: {img.size}")
                    
                    # Create exact size image with white background
                    resized_img = Image.new('RGB', (EPD_WIDTH, EPD_HEIGHT), 'white')
                    
                    # Center the image
                    x = (EPD_WIDTH - img.width) // 2
                    y = (EPD_HEIGHT - img.height) // 2
                    resized_img.paste(img, (x, y))
                    print(f"   Centered at: ({x}, {y})")
                
                # Verify final size is exactly correct
                if resized_img.size != (EPD_WIDTH, EPD_HEIGHT):
                    print(f"❌ Size error! Got {resized_img.size}, expected ({EPD_WIDTH}, {EPD_HEIGHT})")
                    return None
                
                print(f"✅ Final size: {resized_img.size}")
                
                # Save debug image if requested
                if debug:
                    debug_path = f"debug_resized_{resize_mode}.png"
                    resized_img.save(debug_path)
                    print(f"   Debug image saved: {debug_path}")
                
                # Define E6 palette - more accurate colors
                e6_palette = [
                    (0x0, (0, 0, 0)),           # BLACK
                    (0x1, (255, 255, 255)),     # WHITE  
                    (0x2, (255, 255, 0)),       # YELLOW
                    (0x3, (255, 0, 0)),         # RED
                    (0x5, (0, 0, 255)),         # BLUE
                    (0x6, (0, 255, 0))          # GREEN
                ]
                
                # Apply improved color quantization
                quantized_img = self.quantize_to_e6_palette(resized_img, e6_palette, debug)
                
                # Convert to E6 format with verification
                raw_data = self.convert_to_e6_format(quantized_img, e6_palette)
                
                print(f"   Raw data size: {len(raw_data)} bytes")
                print(f"   Expected size: {EPD_BUFFER_SIZE} bytes")
                
                if len(raw_data) != EPD_BUFFER_SIZE:
                    print(f"❌ Size mismatch! Expected {EPD_BUFFER_SIZE}, got {len(raw_data)}")
                    return None
                
                # Debug: show first few bytes
                if debug:
                    print(f"   First 16 bytes: {' '.join(f'{b:02X}' for b in raw_data[:16])}")
                    print(f"   Last 16 bytes:  {' '.join(f'{b:02X}' for b in raw_data[-16:])}")
                
                return raw_data
                    
        except Exception as e:
            print(f"❌ Error processing image: {e}")
            import traceback
            traceback.print_exc()
            return None

    def quantize_to_e6_palette(self, img, palette, debug=False):
        """Convert image to E6 palette using improved quantization"""
        print("   Quantizing to E6 palette...")
        
        width, height = img.size
        pixels = img.load()
        
        # Create new image for quantized result
        quantized = Image.new('RGB', (width, height))
        quantized_pixels = quantized.load()
        
        # Color statistics for debugging
        color_counts = {color_code: 0 for color_code, _ in palette}
        
        # Process each pixel
        for y in range(height):
            for x in range(width):
                r, g, b = pixels[x, y]
                
                # Find closest color in E6 palette
                min_distance = float('inf')
                best_color = palette[0][1]  # Default to black
                best_code = palette[0][0]
                
                for color_code, rgb in palette:
                    # Use weighted distance for better color matching
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
        print("   Color distribution:")
        color_names = {0x0: "BLACK", 0x1: "WHITE", 0x2: "YELLOW", 0x3: "RED", 0x5: "BLUE", 0x6: "GREEN"}
        for code, count in color_counts.items():
            percentage = (count / total_pixels) * 100
            print(f"     {color_names.get(code, f'0x{code:X}')}: {count:6d} pixels ({percentage:5.1f}%)")
        
        # Save debug image
        if debug:
            debug_path = f"debug_quantized.png"
            quantized.save(debug_path)
            print(f"   Quantized debug image saved: {debug_path}")
        
        return quantized

    def convert_to_e6_format(self, img, palette):
        """Convert quantized image to E6 4-bit format with exact ESP32 matching"""
        print("   Converting to E6 4-bit format...")
        
        width, height = img.size
        pixels = img.load()
        
        # Verify dimensions
        if width != EPD_WIDTH or height != EPD_HEIGHT:
            raise ValueError(f"Image must be exactly {EPD_WIDTH}x{EPD_HEIGHT}, got {width}x{height}")
        
        raw_data = bytearray()
        
        # Create RGB to color code mapping
        rgb_to_code = {}
        for color_code, rgb in palette:
            rgb_to_code[rgb] = color_code
        
        # Process image row by row, exactly matching ESP32 expectations
        bytes_written = 0
        for y in range(height):
            for x in range(0, width, 2):  # Process 2 pixels at a time
                # Get first pixel
                r1, g1, b1 = pixels[x, y]
                color1 = rgb_to_code.get((r1, g1, b1), 0x0)  # Default to black if not found
                
                # Get second pixel (or white if at odd width edge)
                if x + 1 < width:
                    r2, g2, b2 = pixels[x + 1, y]
                    color2 = rgb_to_code.get((r2, g2, b2), 0x1)  # Default to white
                else:
                    color2 = 0x1  # WHITE for padding
                
                # Pack exactly as ESP32 expects: high nibble = first pixel, low nibble = second pixel
                byte_value = (color1 << 4) | color2
                raw_data.append(byte_value)
                bytes_written += 1
        
        print(f"   Packed {bytes_written} bytes from {width}x{height} pixels")
        
        # Verify exact buffer size
        expected_bytes = (EPD_WIDTH * EPD_HEIGHT) // 2
        if len(raw_data) != expected_bytes:
            raise ValueError(f"Buffer size mismatch: expected {expected_bytes}, got {len(raw_data)}")
        
        return bytes(raw_data)

    def create_test_pattern(self):
        """Create E6 test pattern exactly matching ESP32 implementation"""
        print("📊 Creating E6 test pattern...")
        
        raw_data = bytearray()
        
        # E6 color definitions - exact match with ESP32
        colors = [0x0, 0x1, 0x2, 0x3, 0x5, 0x6]  # BLACK, WHITE, YELLOW, RED, BLUE, GREEN
        
        for y in range(EPD_HEIGHT):
            # Determine which color band (6 bands of 80 pixels each)
            color_index = min(y // 80, 5)
            color = colors[color_index]
            
            for x in range(EPD_WIDTH // 2):  # 2 pixels per byte
                # Pack 2 pixels of the same color - exact ESP32 format
                byte_value = (color << 4) | color
                raw_data.append(byte_value)
        
        print(f"   Test pattern size: {len(raw_data)} bytes")
        return bytes(raw_data)
    
    def create_simple_test_image(self):
        """Create a simple test image to debug pixel mapping"""
        print("📊 Creating simple test image...")
        
        # Create image with clear patterns
        img = Image.new('RGB', (EPD_WIDTH, EPD_HEIGHT), 'white')
        pixels = img.load()
        
        # Create vertical stripes of each color
        stripe_width = EPD_WIDTH // 6
        colors = [(0, 0, 0), (255, 255, 255), (255, 255, 0), (255, 0, 0), (0, 0, 255), (0, 255, 0)]
        
        for x in range(EPD_WIDTH):
            color_index = min(x // stripe_width, 5)
            color = colors[color_index]
            for y in range(EPD_HEIGHT):
                pixels[x, y] = color
        
        # Add horizontal lines every 50 pixels for alignment checking
        for y in range(0, EPD_HEIGHT, 50):
            for x in range(EPD_WIDTH):
                pixels[x, y] = (0, 0, 0)  # Black lines
        
        return img
    
    def send_image_data(self, image_data):
        """Send raw image data to ESP32 using multipart form"""
        try:
            print(f"📡 Sending image data to ESP32...")
            print(f"   Endpoint: {self.base_url}{API_ENDPOINT}")
            print(f"   Data size: {len(image_data)} bytes")
            
            # Create multipart form with binary data
            files = {
                'file': ('image.bin', image_data, 'application/octet-stream')
            }
            
            start_time = time.time()
            
            # Send data
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
    
    def send_simple_test(self):
        """Send simple test image for debugging"""
        print("🧪 Creating and sending simple test image...")
        test_img = self.create_simple_test_image()
        
        # Save for inspection
        test_img.save("debug_simple_test.png")
        print("   Test image saved as debug_simple_test.png")
        
        # Define E6 palette
        e6_palette = [
            (0x0, (0, 0, 0)),           # BLACK
            (0x1, (255, 255, 255)),     # WHITE  
            (0x2, (255, 255, 0)),       # YELLOW
            (0x3, (255, 0, 0)),         # RED
            (0x5, (0, 0, 255)),         # BLUE
            (0x6, (0, 255, 0))          # GREEN
        ]
        
        # Quantize and convert
        quantized = self.quantize_to_e6_palette(test_img, e6_palette, debug=True)
        raw_data = self.convert_to_e6_format(quantized, e6_palette)
        
        return self.send_image_data(raw_data)

def main():
    parser = argparse.ArgumentParser(description='Send image to ESP32 E6 E-Paper display')
    parser.add_argument('--ip', default=DEFAULT_ESP32_IP, help='ESP32 IP address')
    parser.add_argument('--image', default=DEFAULT_IMAGE_PATH, help='Image file path')
    parser.add_argument('--test', action='store_true', help='Send test pattern instead of image')
    parser.add_argument('--simple-test', action='store_true', help='Send simple test image for debugging')
    parser.add_argument('--resize', choices=['fit', 'fill', 'stretch'], default='fit',
                       help='Resize mode: fit (maintain ratio, may have borders), fill (crop to fill), stretch (may distort)')
    parser.add_argument('--debug', action='store_true', help='Save debug images and show detailed output')
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("🖼️  ESP32 E6 E-Paper Image Sender")
    print("=" * 60)
    print(f"Display: 7.3\" E6 Spectra 6-color ({EPD_WIDTH}x{EPD_HEIGHT})")
    print(f"Buffer size: {EPD_BUFFER_SIZE} bytes")
    print()
    
    # Create sender instance
    sender = EPaperImageSender(args.ip)
    
    # Check connection
    if not sender.check_connection():
        print("\n💡 Troubleshooting tips:")
        print("   1. Make sure ESP32 is powered on and connected to WiFi")
        print("   2. Check that you're on the same network as the ESP32")
        print("   3. Verify the IP address is correct")
        print(f"   4. Try accessing http://{args.ip}/api/status in your web browser")
        sys.exit(1)
    
    success = False
    
    if args.test:
        # Send test pattern
        print(f"\n🧪 Sending E6 test pattern...")
        success = sender.send_test_pattern()
        
    elif args.simple_test:
        # Send simple test image
        print(f"\n🧪 Sending simple test image...")
        success = sender.send_simple_test()
        
    else:
        # Process and send image
        print(f"\n🖼️  Processing image: {args.image}")
        image_data = sender.convert_image_for_epaper(args.image, args.resize, args.debug)
        if image_data:
            success = sender.send_image_data(image_data)
        else:
            print(f"❌ Failed to process image: {args.image}")
            sys.exit(1)
    
    print("\n" + "=" * 60)
    if success:
        print("🎉 Image sent successfully!")
        print("   The E6 display should update shortly...")
        print("   Check the ESP32 serial output for details")
        if args.debug:
            print("   Debug images saved in current directory")
    else:
        print("❌ Failed to send image")
        print("   Check ESP32 serial output for error details")
    print("=" * 60)

if __name__ == "__main__":
    main()