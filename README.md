\# ESP32 E6 E-Paper Image Sender - Usage Commands



\## Basic Usage



\### Send Image with Default Settings

```bash

python send\_image\_to\_epaper.py --image photo.jpg

```

\*Uses fill mode (crops to fill entire display) with default IP address\*



\### Send Image to Specific IP Address

```bash

python send\_image\_to\_epaper.py --ip 192.168.1.100 --image photo.jpg

```



\### Send Test Pattern

```bash

python send\_image\_to\_epaper.py --test

```

\*Displays 6-color test bands to verify display functionality\*



\## Image Resize Modes



\### Fill Mode (Default - Recommended)

```bash

python send\_image\_to\_epaper.py --image photo.jpg --resize fill

```

\*Crops image to fill entire 800x480 display, no white borders\*



\### Fit Mode (Maintain Aspect Ratio)

```bash

python send\_image\_to\_epaper.py --image photo.jpg --resize fit

```

\*Maintains original aspect ratio, adds white borders if needed\*



\### Stretch Mode (Force Exact Fit)

```bash

python send\_image\_to\_epaper.py --image photo.jpg --resize stretch

```

\*Stretches image to exact 800x480 dimensions, may distort image\*



\## Complete Command Examples



\### Send Image with All Options

```bash

python send\_image\_to\_epaper.py --ip 10.168.1.166 --image ~/Pictures/sunset.png --resize fill

```



\### Send Different Image Types

```bash

\# JPEG image

python send\_image\_to\_epaper.py --image photo.jpg



\# PNG image  

python send\_image\_to\_epaper.py --image screenshot.png



\# Other supported formats (BMP, GIF, TIFF, etc.)

python send\_image\_to\_epaper.py --image image.bmp

```



\### Quick Commands for Testing



\#### Test ESP32 Connection

```bash

python send\_image\_to\_epaper.py --test --ip 192.168.1.100

```



\#### Send Image with Verbose Output

```bash

python send\_image\_to\_epaper.py --image photo.jpg --resize fill

```

\*Already includes verbose output by default\*



\## Command Line Arguments Reference



| Argument | Default | Description |

|----------|---------|-------------|

| `--ip` | `10.168.1.166` | ESP32 IP address |

| `--image` | `photo.png` | Path to image file |

| `--resize` | `fill` | Resize mode: `fill`, `fit`, or `stretch` |

| `--test` | `false` | Send test pattern instead of image |



\## ESP32 API Endpoints



The script automatically uses these endpoints:



\- \*\*Image Upload:\*\* `http://ESP32\_IP/api/display/frame` (POST)

\- \*\*Status Check:\*\* `http://ESP32\_IP/api/status` (GET)

\- \*\*Test Pattern:\*\* Uses same upload endpoint with generated test data

\- \*\*Clear Display:\*\* `http://ESP32\_IP/api/clear` (GET) - \*Available via ESP32 web interface\*



\## Troubleshooting Commands



\### Check ESP32 Status

```bash

curl http://10.168.1.166/api/status

```



\### Clear Display (via browser or curl)

```bash

curl http://10.168.1.166/api/clear

```



\### Send Test Pattern to Verify Connection

```bash

python send\_image\_to\_epaper.py --test --ip YOUR\_ESP32\_IP

```



\## File Support



Supports all image formats that PIL/Pillow can read:

\- \*\*Common:\*\* JPG, JPEG, PNG, BMP, GIF, TIFF

\- \*\*Others:\*\* WebP, TGA, ICO, and more



\## Performance Notes



\- \*\*Image Processing:\*\* ~1-3 seconds (depends on image size and complexity)

\- \*\*Upload Time:\*\* ~2-5 seconds for 192KB data

\- \*\*Display Update:\*\* ~10-15 seconds (e-paper refresh time)

\- \*\*Total Time:\*\* ~15-25 seconds from command to display update

