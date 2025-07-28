#!/usr/bin/env python3
"""
ESP32 E-Paper Image Sender
Sends a PNG image to the ESP32 e-paper display via HTTP API
"""

import os
import sys
import requests
import json
from PIL import Image
import io
import time
import argparse

# Default configuration
DEFAULT_ESP32_IP = "10.168.1.166"  # Change this to your ESP32's IP address
DEFAULT_IMAGE_PATH = "photo.png"
API_ENDPOINT = "/api/display/frame"
STATUS_ENDPOINT = "/api/status"
TIMEOUT = 30  # seconds

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
                print(f"   Uptime: {status_data.get('uptime_formatted', 'Unknown')}")
                print(f"   Hardware: {'Available' if status_data.get('hardware_available') else 'Simulation Mode'}")
                return True
            else:
                print(f"❌ ESP32 responded with status {response.status_code}")
                return False
        except requests.RequestException as e:
            print(f"❌ Failed to connect to ESP32: {e}")
            return False
    
    def convert_image_for_epaper(self, image_path, target_width=800, target_height=480):
        """Convert image to format suitable for e-paper display"""
        try:
            print(f"🖼️  Processing image: {image_path}")
            
            # Check if file exists
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
                img.thumbnail((target_width, target_height), Image.Resampling.LANCZOS)
                
                # Create new image with white background and center the resized image
                new_img = Image.new('RGB', (target_width, target_height), 'white')
                
                # Calculate position to center the image
                x = (target_width - img.width) // 2
                y = (target_height - img.height) // 2
                
                new_img.paste(img, (x, y))
                
                # Convert to grayscale
                gray_img = new_img.convert('L')
                
                # Apply dithering for better e-paper display
                bw_img = gray_img.point(lambda x: 0 if x < 128 else 255, '1')
                
                print(f"   Processed size: {bw_img.size}")
                print(f"   Final mode: {bw_img.mode}")
                
                # Convert to bytes
                img_buffer = io.BytesIO()
                bw_img.save(img_buffer, format='PNG')
                img_bytes = img_buffer.getvalue()
                
                print(f"   Image data size: {len(img_bytes)} bytes")
                return img_bytes
                
        except Exception as e:
            print(f"❌ Error processing image: {e}")
            return None
    
    def send_image_data(self, image_data):
        """Send image data to ESP32"""
        try:
            print(f"📡 Sending image data to ESP32...")
            print(f"   Endpoint: {self.base_url}{API_ENDPOINT}")
            print(f"   Data size: {len(image_data)} bytes")
            
            headers = {
                'Content-Type': 'application/octet-stream',
                'User-Agent': 'EPaper-ImageSender/1.0'
            }
            
            # Send the data
            start_time = time.time()
            response = requests.post(
                f"{self.base_url}{API_ENDPOINT}",
                data=image_data,
                headers=headers,
                timeout=TIMEOUT
            )
            
            elapsed_time = time.time() - start_time
            
            if response.status_code == 200:
                response_data = response.json()
                print(f"✅ Image sent successfully!")
                print(f"   Response: {response_data.get('message', 'Success')}")
                print(f"   Frame number: {response_data.get('frame_number', 'Unknown')}")
                print(f"   Upload time: {elapsed_time:.2f} seconds")
                return True
            else:
                print(f"❌ Failed to send image. Status: {response.status_code}")
                try:
                    error_data = response.json()
                    print(f"   Error: {error_data.get('error', 'Unknown error')}")
                except:
                    print(f"   Response: {response.text}")
                return False
                
        except requests.Timeout:
            print(f"❌ Request timed out after {TIMEOUT} seconds")
            return False
        except requests.RequestException as e:
            print(f"❌ Network error: {e}")
            return False
    
    def send_raw_bitmap(self, image_path):
        """Send raw bitmap data (for testing)"""
        try:
            # Create a simple test pattern
            width, height = 800, 480
            bitmap_size = (width * height) // 8  # 1 bit per pixel
            
            # Create a simple pattern
            bitmap_data = bytearray(bitmap_size)
            
            # Fill with a test pattern (alternating stripes)
            for y in range(height):
                for x in range(0, width, 8):
                    byte_index = (y * width + x) // 8
                    if byte_index < len(bitmap_data):
                        # Create alternating pattern
                        if (y // 20) % 2 == 0:
                            bitmap_data[byte_index] = 0xAA  # Alternating bits
                        else:
                            bitmap_data[byte_index] = 0x55  # Inverted alternating bits
            
            print(f"📊 Sending raw bitmap test pattern...")
            print(f"   Size: {width}x{height} pixels")
            print(f"   Data: {len(bitmap_data)} bytes")
            
            return self.send_image_data(bytes(bitmap_data))
            
        except Exception as e:
            print(f"❌ Error creating test pattern: {e}")
            return False

def find_esp32_ip():
    """Try to find ESP32 on local network"""
    import socket
    
    print("🔍 Attempting to find ESP32 on local network...")
    
    # Get local network base
    hostname = socket.gethostname()
    local_ip = socket.gethostbyname(hostname)
    network_base = '.'.join(local_ip.split('.')[:-1]) + '.'
    
    print(f"   Scanning network: {network_base}1-254")
    
    # Common ESP32 IPs to try first
    common_ips = [
        f"{network_base}100",
        f"{network_base}101",
        f"{network_base}200",
        f"{network_base}150"
    ]
    
    for ip in common_ips:
        try:
            response = requests.get(f"http://{ip}/api/status", timeout=2)
            if response.status_code == 200:
                data = response.json()
                if 'epaper' in data.get('service', '').lower():
                    print(f"✅ Found ESP32 E-Paper display at: {ip}")
                    return ip
        except:
            continue
    
    print("❌ Could not automatically find ESP32")
    return None

def main():
    parser = argparse.ArgumentParser(description='Send image to ESP32 E-Paper display')
    parser.add_argument('--ip', default=None, help='ESP32 IP address')
    parser.add_argument('--image', default=DEFAULT_IMAGE_PATH, help='Image file path')
    parser.add_argument('--test', action='store_true', help='Send test pattern instead of image')
    parser.add_argument('--find', action='store_true', help='Try to find ESP32 automatically')
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("🖼️  ESP32 E-Paper Image Sender")
    print("=" * 60)
    
    # Determine ESP32 IP
    esp32_ip = args.ip
    
    if args.find or not esp32_ip:
        found_ip = find_esp32_ip()
        if found_ip:
            esp32_ip = found_ip
        elif not esp32_ip:
            esp32_ip = input(f"Enter ESP32 IP address (default: {DEFAULT_ESP32_IP}): ").strip()
            if not esp32_ip:
                esp32_ip = DEFAULT_ESP32_IP
    
    # Create sender instance
    sender = EPaperImageSender(esp32_ip)
    
    # Check connection
    if not sender.check_connection():
        print("\n💡 Troubleshooting tips:")
        print("   1. Make sure ESP32 is powered on and connected to WiFi")
        print("   2. Check that you're on the same network as the ESP32")
        print("   3. Verify the IP address is correct")
        print("   4. Try accessing http://{esp32_ip} in your web browser")
        sys.exit(1)
    
    success = False
    
    if args.test:
        # Send test pattern
        print(f"\n🧪 Sending test pattern...")
        success = sender.send_raw_bitmap("")
        
    else:
        # Process and send image
        image_data = sender.convert_image_for_epaper(args.image)
        if image_data:
            success = sender.send_image_data(image_data)
        else:
            print(f"❌ Failed to process image: {args.image}")
            sys.exit(1)
    
    print("\n" + "=" * 60)
    if success:
        print("🎉 Image sent successfully!")
        print("   The display should update shortly...")
        print("   Check the ESP32 serial output for details")
    else:
        print("❌ Failed to send image")
        print("   Check ESP32 serial output for error details")
    print("=" * 60)

if __name__ == "__main__":
    main()