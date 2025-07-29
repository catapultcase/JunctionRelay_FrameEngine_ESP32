# ESP32 E-Paper 6-Color Display System

A dual-core ESP32 system for driving 7.3" Waveshare E-Paper (E) displays with 6-color support (Black, White, Yellow, Red, Blue, Green). Features a responsive HTTP API and Python client with advanced image processing including Floyd-Steinberg dithering.

## Features

- 🎨 **6-Color E-Paper Support** - Black, White, Yellow, Red, Blue, Green
- 🚀 **Dual-Core Architecture** - HTTP server on Core 0, Display on Core 1
- 🌐 **RESTful API** - Upload images, control display, check status
- 🖼️ **Advanced Image Processing** - Multiple resize modes and dithering options
- 📱 **Non-Blocking Operations** - Upload images while previous ones are rendering
- 🔧 **Memory Optimized** - Uses PSRAM when available, efficient buffer management

## Hardware Requirements

- ESP32-S3 Feather (or compatible)
- Waveshare 7.3" E-Paper (E) display - 6 color
- SPI connections:
  - CS: Pin 10
  - DC: Pin 9  
  - RST: Pin 6
  - BUSY: Pin 5

## Quick Start

### 1. Flash the ESP32

Upload the ESP32 code with your WiFi credentials:

```cpp
const char* WIFI_SSID     = "YourWiFiName";
const char* WIFI_PASSWORD = "YourWiFiPassword";
```

### 2. Find Your ESP32's IP

Check the serial monitor for the WiFi connection message:
```
✅ WiFi connected: 192.168.1.100
✅ HTTP server running on port 80
```

### 3. Test the Connection

```bash
# Check if the system is running
curl http://192.168.1.100/api/status

# Show test pattern
curl http://192.168.1.100/api/test

# Clear display
curl http://192.168.1.100/api/clear
```

### 4. Upload Images with Python

```bash
# Basic image upload
python send_image_to_epaper.py --ip 192.168.1.100 --image photo.jpg

# With fast dithering (recommended)
python send_image_to_epaper.py --ip 192.168.1.100 --image photo.jpg --dither fast

# Fill entire display (crop if needed)
python send_image_to_epaper.py --ip 192.168.1.100 --image photo.jpg --resize fill --dither fast
```

## API Endpoints

### GET /api/status

Returns system status and information.

**Response:**
```json
{
  "status": "running",
  "service": "epaper_6color_frame_dual_core",
  "display_initialized": true,
  "display_busy": false,
  "last_operation": "Image display",
  "last_update_time": 1234567890,
  "resolution": "800x480",
  "buffer_size": 192000,
  "ip_address": "192.168.1.100",
  "free_heap": 56940,
  "largest_free_block": 45000,
  "http_core": 0,
  "display_core": 1,
  "shared_buffer": true
}
```

### POST /api/display/frame

Upload and display an image. Accepts raw 4-bit E-Paper format data.

**Content-Type:** `multipart/form-data`
**File Field:** `file`
**Expected Size:** 192,000 bytes (800×480 pixels, 4 bits per pixel)

**Response (Success):**
```json
{
  "status": "success",
  "message": "Image queued (immediate)",
  "queue_position": 1,
  "estimated_wait_seconds": 0,
  "bytes_received": 192000
}
```

**Response (Display Busy):**
```json
{
  "status": "success",
  "message": "Image queued (replacing previous)",
  "queue_position": 1,
  "estimated_wait_seconds": 45
}
```

### GET /api/test

Display a test pattern showing all 6 colors in horizontal bands.

**Response:**
```json
{
  "status": "success",
  "message": "Test pattern queued"
}
```

### GET /api/clear

Clear the display to white.

**Response:**
```json
{
  "status": "success",
  "message": "Clear queued"
}
```

## Python Client Usage

### Basic Commands

```bash
# Upload image with default settings
python send_image_to_epaper.py --image photo.jpg

# Specify ESP32 IP address
python send_image_to_epaper.py --ip 192.168.1.100 --image photo.jpg

# Send test pattern
python send_image_to_epaper.py --test

# Save debug images
python send_image_to_epaper.py --image photo.jpg --debug
```

### Resize Modes

Control how images are fitted to the 800×480 display:

```bash
# Fit (default) - maintain aspect ratio, add borders if needed
python send_image_to_epaper.py --image photo.jpg --resize fit

# Fill - crop to fill entire display
python send_image_to_epaper.py --image photo.jpg --resize fill

# Stretch - distort to exact dimensions
python send_image_to_epaper.py --image photo.jpg --resize stretch
```

**Resize Mode Comparison:**
- **Fit**: Best for preserving image composition, may have white borders
- **Fill**: Best for full-screen display, may crop parts of the image  
- **Stretch**: Exact fit but may distort proportions

### Dithering Options

Improve image quality with different dithering algorithms:

```bash
# No dithering (fastest, hard color boundaries)
python send_image_to_epaper.py --image photo.jpg --dither none

# Fast dithering (recommended - PIL optimized)
python send_image_to_epaper.py --image photo.jpg --dither fast

# Quality dithering (slower, custom Floyd-Steinberg)
python send_image_to_epaper.py --image photo.jpg --dither quality
```

**Dithering Comparison:**
- **None**: Fastest processing, hard color transitions, potential banding
- **Fast**: Good balance of speed and quality, uses PIL's optimized dithering
- **Quality**: Best image quality, slower processing, custom implementation

### Complete Examples

```bash
# Portrait photo with quality dithering
python send_image_to_epaper.py --image portrait.jpg --resize fill --dither quality --debug

# Landscape photo, fast processing
python send_image_to_epaper.py --image landscape.jpg --resize fit --dither fast

# Test different settings quickly
python send_image_to_epaper.py --image test.jpg --resize stretch --dither none
```

## Color Palette

The display supports 6 colors with specific RGB values:

| Color Index | Color Name | RGB Value | Usage |
|-------------|------------|-----------|--------|
| 0x0 | Black | (0, 0, 0) | Text, outlines |
| 0x1 | White | (255, 255, 255) | Background, highlights |
| 0x2 | Yellow | (255, 255, 0) | Accents, warnings |
| 0x3 | Red | (255, 0, 0) | Important elements |
| 0x5 | Blue | (0, 0, 255) | Links, information |
| 0x6 | Green | (0, 255, 0) | Success, nature |

> **Note:** Color index 4 is unused in the hardware and automatically mapped to index 5 (Blue).

## Performance Notes

### Display Refresh Timing

- **Image Upload**: ~1-2 seconds for 192KB
- **Data Transmission**: ~1-2 seconds to display
- **E-Paper Refresh**: 10-45 seconds (hardware limitation)
- **HTTP Response**: Immediate (non-blocking)

### Memory Usage

- **Shared Buffer**: 192KB (uses PSRAM when available)
- **Processing**: Temporary allocations during upload
- **Free Heap**: ~50-60KB available during operation

### Concurrent Operations

- ✅ Upload new image while previous is displaying
- ✅ Check status during display refresh
- ✅ Queue multiple operations
- ⚠️ New images replace queued ones (last-writer-wins)

## Troubleshooting

### Connection Issues

```bash
# Test basic connectivity
ping 192.168.1.100

# Check if HTTP server is responding
curl http://192.168.1.100/api/status

# Verify from browser
open http://192.168.1.100/api/status
```

### Common Error Messages

**"Memory allocation failed"**
- ESP32 out of memory
- Try restarting the ESP32
- Check free heap in `/api/status`

**"Display not ready or busy"**
- Display is currently refreshing (wait 45 seconds)
- Check `display_busy` field in `/api/status`

**"Upload incomplete"**
- Image processing failed in Python client
- Check image file exists and is readable
- Try with `--debug` flag for more information

**"Size mismatch"**
- Processed image is not exactly 192,000 bytes
- Internal processing error, try different resize mode

### Debug Mode

Enable debug mode for detailed information and saved intermediate images:

```bash
python send_image_to_epaper.py --image photo.jpg --debug --dither fast
```

**Debug outputs:**
- `debug_resized_[mode].png` - Resized image
- `debug_quantized.png` - Color-reduced image (no dither)
- `debug_pil_dithered.png` - PIL dithered result
- `debug_custom_dithered.png` - Custom dithered result
- Console output with timing and color statistics

### Serial Monitor

Monitor ESP32 serial output for detailed operation logs:

```
[HTTP] Image queued at position 1 (192000 bytes)
[DISPLAY] Processing shared image command...
[DISPLAY] Sending shared image data...
[DISPLAY] Data transmission took 1234 ms
[DISPLAY] Starting display update sequence...
[DISPLAY] Waiting for BUSY (timeout: 45.0s)... OK (23.4s)
[DISPLAY] Display update complete
[DISPLAY] Command completed in 25678 ms
```

## Advanced Usage

### Custom API Integration

Use the HTTP API directly from any programming language:

```python
import requests

# Upload custom binary data
with open('custom_image.bin', 'rb') as f:
    files = {'file': ('image.bin', f.read(), 'application/octet-stream')}
    response = requests.post('http://192.168.1.100/api/display/frame', files=files)
    print(response.json())
```

```javascript
// JavaScript/Node.js example
const FormData = require('form-data');
const fs = require('fs');

const form = new FormData();
form.append('file', fs.createReadStream('image.bin'));

fetch('http://192.168.1.100/api/display/frame', {
    method: 'POST',
    body: form
}).then(response => response.json())
  .then(data => console.log(data));
```

### Batch Processing

Process multiple images quickly:

```bash
#!/bin/bash
for image in *.jpg; do
    echo "Processing $image..."
    python send_image_to_epaper.py --image "$image" --dither fast --resize fill
    sleep 60  # Wait for display to finish
done
```

## Contributing

This project is based on the [Cadre](https://github.com/DDoS/Cadre) open-source project (AGPL v3 license) for e-paper display drivers and color handling.

## License

This project incorporates techniques and approaches from the Cadre project. Please respect the AGPL v3 license terms if distributing or modifying this code.