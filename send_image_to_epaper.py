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
    
    def apply_floyd_steinberg_dithering(self, img, palette):
        """Apply Floyd-Steinberg dithering to improve color quality"""
        print("   Applying Floyd-Steinberg dithering...")
        
        # Convert to RGB and get pixel array
        img = img.convert('RGB')
        width, height = img.size
        pixels = list(img.getdata())
        
        # Convert to 2D array for easier access
        pixel_array = []
        for y in range(height):
            row = []
            for x in range(width):
                row.append(list(pixels[y * width + x]))
            pixel_array.append(row)
        
        # Apply Floyd-Steinberg dithering
        for y in range(height):
            for x in range(width):
                old_pixel = pixel_array[y][x]
                new_pixel = self.find_closest_color(old_pixel, palette)
                pixel_array[y][x] = new_pixel
                
                # Calculate error
                error = [old_pixel[i] - new_pixel[i] for i in range(3)]
                
                # Distribute error to neighboring pixels
                if x + 1 < width:
                    for i in range(3):
                        pixel_array[y][x + 1][i] = min(255, max(0, pixel_array[y][x + 1][i] + error[i] * 7/16))
                
                if y + 1 < height:
                    if x > 0:
                        for i in range(3):
                            pixel_array[y + 1][x - 1][i] = min(255, max(0, pixel_array[y + 1][x - 1][i] + error[i] * 3/16))
                    
                    for i in range(3):
                        pixel_array[y + 1][x][i] = min(255, max(0, pixel_array[y + 1][x][i] + error[i] * 5/16))
                    
                    if x + 1 < width:
                        for i in range(3):
                            pixel_array[y + 1][x + 1][i] = min(255, max(0, pixel_array[y + 1][x + 1][i] + error[i] * 1/16))
        
        # Convert back to PIL Image
        dithered_pixels = []
        for y in range(height):
            for x in range(width):
                dithered_pixels.append(tuple(int(c) for c in pixel_array[y][x]))
        
        dithered_img = Image.new('RGB', (width, height))
        dithered_img.putdata(dithered_pixels)
        return dithered_img
    
    def find_closest_color(self, pixel, palette):
        """Find the closest color in the E6 palette"""
        min_distance = float('inf')
        closest_color = palette[0][1]  # Default to black
        
        for color_code, rgb in palette:
            distance = sum((pixel[i] - rgb[i]) ** 2 for i in range(3)) ** 0.5
            if distance < min_distance:
                min_distance = distance
                closest_color = rgb
        
        return closest_color
    
    def convert_image_for_epaper(self, image_path):
        """Convert image to E6 e-paper format with dithering"""
        try:
            print(f"🖼️  Processing image: {image_path}")
            
            if not os.path.exists(image_path):
                print(f"❌ Image file not found: {image_path}")
                return None
            
            # Open and process image
            with Image.open(image_path) as img:
                print(f"   Original size: {img.size}")
                print(f"   Original mode: {img.mode}")
                
                # Convert to RGB if needed
                if img.mode != 'RGB':
                    img = img.convert('RGB')
                
                # Resize to fit display while maintaining aspect ratio
                img.thumbnail((EPD_WIDTH, EPD_HEIGHT), Image.Resampling.LANCZOS)
                
                # Create new image with white background and center the resized image
                new_img = Image.new('RGB', (EPD_WIDTH, EPD_HEIGHT), 'white')
                
                # Calculate position to center the image
                x = (EPD_WIDTH - img.width) // 2
                y = (EPD_HEIGHT - img.height) // 2
                new_img.paste(img, (x, y))
                
                print(f"   Resized to: {new_img.size}")
                
                # Define E6 palette
                e6_palette = [
                    (0x0, (0, 0, 0)),           # BLACK
                    (0x1, (255, 255, 255)),     # WHITE  
                    (0x2, (255, 255, 0)),       # YELLOW
                    (0x3, (255, 0, 0)),         # RED
                    (0x5, (0, 0, 255)),         # BLUE
                    (0x6, (0, 255, 0))          # GREEN
                ]
                
                # Apply Floyd-Steinberg dithering
                dithered_img = self.apply_floyd_steinberg_dithering(new_img, e6_palette)
                
                # Convert to E6 format
                raw_data = self.convert_to_e6_format(dithered_img, e6_palette)
                
                print(f"   Raw data size: {len(raw_data)} bytes")
                print(f"   Expected size: {EPD_BUFFER_SIZE} bytes")
                
                if len(raw_data) != EPD_BUFFER_SIZE:
                    print(f"❌ Size mismatch! Expected {EPD_BUFFER_SIZE}, got {len(raw_data)}")
                    return None
                
                return raw_data
                    
        except Exception as e:
            print(f"❌ Error processing image: {e}")
            return None

    def convert_to_e6_format(self, img, palette):
        """Convert RGB image to E6 4-bit format (2 pixels per byte)"""
        width, height = img.size
        pixels = img.load()
        
        raw_data = bytearray()
        
        # Create color mapping for quick lookup
        color_map = {}
        for color_code, rgb in palette:
            color_map[rgb] = color_code
        
        def rgb_to_e6_color(r, g, b):
            """Convert RGB to nearest E6 color"""
            target_pixel = (r, g, b)
            min_distance = float('inf')
            best_color = 0x0  # Default to black
            
            for color_code, rgb in palette:
                distance = sum((target_pixel[i] - rgb[i]) ** 2 for i in range(3)) ** 0.5
                if distance < min_distance:
                    min_distance = distance
                    best_color = color_code
            
            return best_color
        
        # Process image row by row, 2 pixels per byte (4 bits each)
        for y in range(height):
            for x in range(0, width, 2):
                # Get first pixel
                r1, g1, b1 = pixels[x, y]
                color1 = rgb_to_e6_color(r1, g1, b1)
                
                # Get second pixel (or use white if at edge)
                if x + 1 < width:
                    r2, g2, b2 = pixels[x + 1, y]
                    color2 = rgb_to_e6_color(r2, g2, b2)
                else:
                    color2 = 0x1  # WHITE
                
                # Pack 2 pixels into one byte: first pixel in upper 4 bits, second in lower 4 bits
                byte_value = (color1 << 4) | color2
                raw_data.append(byte_value)
        
        return bytes(raw_data)

    def create_test_pattern(self):
        """Create E6 test pattern matching ESP32"""
        print("📊 Creating E6 test pattern...")
        
        raw_data = bytearray()
        
        # E6 color definitions
        colors = [0x0, 0x1, 0x2, 0x3, 0x5, 0x6]  # BLACK, WHITE, YELLOW, RED, BLUE, GREEN
        
        for y in range(EPD_HEIGHT):
            # Determine which color band this row belongs to (6 bands of 80 pixels each)
            color_index = min(y // 80, 5)
            color = colors[color_index]
            
            for x in range(EPD_WIDTH // 2):  # 2 pixels per byte
                # Pack 2 pixels of the same color into one byte
                byte_value = (color << 4) | color
                raw_data.append(byte_value)
        
        print(f"   Test pattern size: {len(raw_data)} bytes")
        return bytes(raw_data)
    
    def send_image_data(self, image_data):
        """Send raw image data to ESP32 using multipart form"""
        try:
            print(f"📡 Sending image data to ESP32...")
            print(f"   Endpoint: {self.base_url}{API_ENDPOINT}")
            print(f"   Data size: {len(image_data)} bytes")
            
            # Create a simple multipart form with the binary data
            # We'll send the data as a file upload to trigger the ESP32's upload handler
            files = {
                'file': ('image.bin', image_data, 'application/octet-stream')
            }
            
            start_time = time.time()
            
            # Send as multipart/form-data which ESP32 can handle with upload handler
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
        
    else:
        # Process and send image
        print(f"\n🖼️  Processing image: {args.image}")
        image_data = sender.convert_image_for_epaper(args.image)
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
    else:
        print("❌ Failed to send image")
        print("   Check ESP32 serial output for error details")
    print("=" * 60)

if __name__ == "__main__":
    main()