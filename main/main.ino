#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPI.h>

// Pin definitions for ESP32 S3 Feather (adjust to your wiring)
#define EPD_CS    10  // D10 - Chip Select
#define EPD_DC    9   // D9  - Data/Command  
#define EPD_RST   6   // D6  - Reset
#define EPD_BUSY  5   // D5  - Busy

// Display specifications for Waveshare 7.3inch E-Paper (E) - 6 color
#define EPD_WIDTH   800
#define EPD_HEIGHT  480
// Buffer size (4 bits per pixel = 2 pixels/byte) - CORRECTED
#define EPD_BUFFER_SIZE ((EPD_WIDTH * EPD_HEIGHT) / 2)

// 6-color definitions (4 bits per pixel) - CORRECTED BASED ON CADRE PROJECT
// The Cadre project shows that index 4 is unused, so colors 4&5 map to 5&6
#define EPD_BLACK   0x00  // Index 0
#define EPD_WHITE   0x01  // Index 1
#define EPD_YELLOW  0x02  // Index 2
#define EPD_RED     0x03  // Index 3
// Index 4 is UNUSED - skip it
#define EPD_BLUE    0x05  // Index 5 (was 4 in display, but 4 is unused)
#define EPD_GREEN   0x06  // Index 6 (was 5 in display, but 4 is unused)

// WiFi Configuration
const char* WIFI_SSID     = "Jon6";
const char* WIFI_PASSWORD = "fv4!F48P8&tR";

WebServer server(80);
bool displayInitialized = false;

// === Upload State ===
uint8_t* imageBuffer = nullptr;
unsigned int uploadBytesReceived = 0;
bool uploadInProgress = false;

// === Hardware Interface Layer ===
void epd_digital_write(int pin, int value) { digitalWrite(pin, value); }
int  epd_digital_read(int pin)              { return digitalRead(pin); }
void epd_delay_ms(unsigned int ms)          { delay(ms); }

void epd_spi_transfer(unsigned char data) {
  epd_digital_write(EPD_CS, LOW);
  SPI.transfer(data);
  epd_digital_write(EPD_CS, HIGH);
}

void epd_send_command(unsigned char command) {
  epd_digital_write(EPD_DC, LOW);
  epd_spi_transfer(command);
}

void epd_send_data(unsigned char data) {
  epd_digital_write(EPD_DC, HIGH);
  epd_spi_transfer(data);
}

// === CADRE-STYLE BUSY HANDLING ===
void epd_busy_wait(float timeout_seconds) {
  unsigned long timeout_ms = (unsigned long)(timeout_seconds * 1000);
  
  // If BUSY is already high, just wait the full timeout (Cadre approach)
  if (epd_digital_read(EPD_BUSY) == HIGH) {
    Serial.printf("⏳ BUSY already high, waiting %.1fs...\n", timeout_seconds);
    delay(timeout_ms);
    return;
  }
  
  // Otherwise, poll until BUSY goes high or timeout
  Serial.printf("⏳ Waiting for BUSY (timeout: %.1fs)...", timeout_seconds);
  unsigned long start_time = millis();
  
  while (epd_digital_read(EPD_BUSY) == LOW) {
    delay(10);  // 10ms polling interval like Cadre
    if (millis() - start_time >= timeout_ms) {
      float elapsed = (millis() - start_time) / 1000.0;
      Serial.printf(" TIMEOUT after %.1fs (continuing)\n", elapsed);
      return;
    }
  }
  
  float elapsed = (millis() - start_time) / 1000.0;
  Serial.printf(" OK (%.1fs)\n", elapsed);
}

void epd_reset(void) {
  epd_digital_write(EPD_RST, HIGH);
  epd_delay_ms(20);
  epd_digital_write(EPD_RST, LOW);
  epd_delay_ms(2);
  epd_digital_write(EPD_RST, HIGH);
  epd_delay_ms(20);
}

int epd_if_init(void) {
  pinMode(EPD_CS, OUTPUT);
  pinMode(EPD_RST, OUTPUT);
  pinMode(EPD_DC, OUTPUT);
  pinMode(EPD_BUSY, INPUT);
  SPI.begin();
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  return 0;
}

// === CORRECTED E-Paper Driver Layer ===
// Based on GDEP073E01 driver from Cadre project - much more comprehensive!
void epd_send_init_sequence() {
  // Command Header - wake up display
  epd_send_command(0xAA);
  epd_send_data(0x49);
  epd_send_data(0x55);
  epd_send_data(0x20);
  epd_send_data(0x08);
  epd_send_data(0x09);
  epd_send_data(0x18);
  
  // Power Setting - critical for stable operation
  epd_send_command(0x01);
  epd_send_data(0x3F);
  epd_send_data(0x00);
  epd_send_data(0x32);
  epd_send_data(0x2A);
  epd_send_data(0x0E);
  epd_send_data(0x2A);
  
  // Panel Setting - CRITICAL for 6-color mode
  epd_send_command(0x00);
  epd_send_data(0x5F);  // 6-color mode with proper settings
  epd_send_data(0x69);
  
  // Power OFF Setting
  epd_send_command(0x03);
  epd_send_data(0x00);
  epd_send_data(0x54);
  epd_send_data(0x00);
  epd_send_data(0x44);
  
  // Booster Setting 1
  epd_send_command(0x05);
  epd_send_data(0x40);
  epd_send_data(0x1F);
  epd_send_data(0x1F);
  epd_send_data(0x2C);
  
  // Booster Setting 2  
  epd_send_command(0x06);
  epd_send_data(0x6F);
  epd_send_data(0x1F);
  epd_send_data(0x16);
  epd_send_data(0x25);
  
  // Booster Setting 3
  epd_send_command(0x08);
  epd_send_data(0x6F);
  epd_send_data(0x1F);
  epd_send_data(0x1F);
  epd_send_data(0x22);
  
  // PLL Setting
  epd_send_command(0x30);
  epd_send_data(0x02);
  epd_send_data(0x00);
  
  // Temperature Sensor Enable
  epd_send_command(0x41);
  epd_send_data(0x00);
  
  // VCOM Setting - important for contrast
  epd_send_command(0x50);
  epd_send_data(0x3F);
  
  // CDI Setting - CRITICAL for color accuracy
  epd_send_command(0x60);
  epd_send_data(0x02);
  epd_send_data(0x00);
  
  // Resolution Setting - MUST MATCH YOUR DISPLAY
  epd_send_command(0x61);
  epd_send_data(0x03);  // 800 pixels width (high byte)
  epd_send_data(0x20);  // 800 pixels width (low byte)  
  epd_send_data(0x01);  // 480 pixels height (high byte)
  epd_send_data(0xE0);  // 480 pixels height (low byte)
  
  // Additional display settings
  epd_send_command(0x82);
  epd_send_data(0x1E);
  
  epd_send_command(0x84);
  epd_send_data(0x01);
  
  epd_send_command(0x86);
  epd_send_data(0x00);
  
  epd_send_command(0xE0);
  epd_send_data(0x01);
  
  epd_send_command(0xE3);
  epd_send_data(0x2F);
  
  epd_send_command(0xE6);
  epd_send_data(0x00);
}

int epd_init(void) {
  if (epd_if_init() != 0) return -1;
  epd_reset();
  epd_busy_wait(0.1);  // Short wait after reset
  epd_delay_ms(30);
  epd_send_init_sequence();
  return 0;
}

// === CADRE-STYLE DISPLAY UPDATE SEQUENCE ===
void epd_turn_on_display(void) {
  Serial.println("📺 Starting display update sequence...");
  
  epd_send_command(0x04); // Power ON  
  epd_busy_wait(0.4);     // Short wait for power on (Cadre: 0.4s)
  
  epd_send_command(0x12); // DISPLAY_REFRESH
  epd_send_data(0x01);
  epd_busy_wait(45.0);    // Long wait for display refresh (Cadre: 45.0s)

  epd_send_command(0x02); // POWER_OFF
  epd_send_data(0x00);
  epd_busy_wait(0.4);     // Short wait for power off (Cadre: 0.4s)
  
  Serial.println("✅ Display update sequence complete");
}

// Clear display to a single color
void epd_clear(unsigned char color) {
  unsigned int W = (EPD_WIDTH + 1) / 2;  // CORRECTED BUFFER WIDTH
  unsigned int H = EPD_HEIGHT;
  
  epd_send_command(0x10);  // Data Start Transmission
  for (unsigned int j = 0; j < H; j++) {
    for (unsigned int i = 0; i < W; i++) {
      epd_send_data((color<<6)|(color<<4)|(color<<2)|color);
    }
  }
  epd_turn_on_display();
}

// CRITICAL: Color correction function based on Cadre project findings
uint8_t epd_correct_color(uint8_t color) {
  // Clamp color to valid range (0-5)
  if (color > 5) color = 5;
  
  // Skip unused index 4: colors 4&5 become 5&6
  if (color >= 4) color += 1;
  
  return color;
}

// Display raw buffer (4-bit pixels) - WITH COLOR CORRECTION
void epd_display(unsigned char *image) {
  unsigned int W = (EPD_WIDTH + 1) / 2;  // CORRECTED BUFFER WIDTH
  unsigned int H = EPD_HEIGHT;
  
  Serial.println("📤 Sending image data to display...");
  epd_send_command(0x10);  // Data Start Transmission
  for (unsigned int j = 0; j < H; j++) {
    for (unsigned int i = 0; i < W; i++) {
      // Extract and correct both pixels in the byte
      uint8_t byte_val = image[i + j * W];
      uint8_t pixel1 = epd_correct_color((byte_val >> 4) & 0x0F);
      uint8_t pixel2 = epd_correct_color(byte_val & 0x0F);
      epd_send_data((pixel1 << 4) | pixel2);
    }
  }
  Serial.println("✅ Image data sent, starting display update...");
  epd_turn_on_display();
}

void epd_sleep(void) {
  epd_send_command(0x02);  // Power OFF
  epd_send_data(0x00);
  epd_send_command(0x07);  // Deep Sleep
  epd_send_data(0xA5);
}

// CORRECTED test pattern - should show distinct color bands
void showTestPattern() {
  if (!displayInitialized) return;
  
  unsigned int W = (EPD_WIDTH + 1) / 2;  // CORRECTED BUFFER WIDTH
  unsigned int H = EPD_HEIGHT;
  
  Serial.println("🎨 Generating test pattern...");
  epd_send_command(0x10);  // Data Start Transmission
  
  for (unsigned int y = 0; y < H; y++) {
    uint8_t band = y / 80;  // 6 bands of 80 pixels each
    uint8_t pixel = 0;
    
    switch (band) {
      case 0: pixel = (EPD_BLACK<<4)|EPD_BLACK; break;     // Black band
      case 1: pixel = (EPD_WHITE<<4)|EPD_WHITE; break;     // White band  
      case 2: pixel = (EPD_YELLOW<<4)|EPD_YELLOW; break;   // Yellow band
      case 3: pixel = (EPD_RED<<4)|EPD_RED; break;         // Red band
      case 4: pixel = (EPD_BLUE<<4)|EPD_BLUE; break;       // Blue band
      default: pixel = (EPD_GREEN<<4)|EPD_GREEN; break;    // Green band
    }
    
    for (unsigned int x = 0; x < W; x++) {
      epd_send_data(pixel);
    }
  }
  Serial.println("✅ Test pattern data sent, starting display update...");
  epd_turn_on_display();
}

// === HTTP Upload Handlers ===
void handleFrameUploadBody() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadInProgress = true;
    uploadBytesReceived = 0;
    if (!imageBuffer) {
      imageBuffer = (uint8_t*)malloc(EPD_BUFFER_SIZE);
      if (!imageBuffer) {
        uploadInProgress = false;
        return;
      }
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadBytesReceived + upload.currentSize <= EPD_BUFFER_SIZE) {
      memcpy(imageBuffer + uploadBytesReceived, upload.buf, upload.currentSize);
      uploadBytesReceived += upload.currentSize;
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    uploadInProgress = false;
  }
  else if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadInProgress = false;
    uploadBytesReceived = 0;
  }
}

void handleFrameUpload() {
  if (!displayInitialized) {
    server.send(500, "application/json", "{\"error\":\"Display not ready\"}");
    return;
  }
  if (uploadInProgress) {
    server.send(500, "application/json", "{\"error\":\"Upload in progress\"}");
    return;
  }
  if (uploadBytesReceived != EPD_BUFFER_SIZE) {
    uploadBytesReceived = 0;
    server.send(400, "application/json", "{\"error\":\"Upload incomplete\"}");
    return;
  }
  
  Serial.println("📤 Image upload complete!");
  Serial.printf("📊 Received %d bytes (expected %d)\n", uploadBytesReceived, EPD_BUFFER_SIZE);
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Image received\"}");
  
  // Render from raw buffer
  Serial.println("🖼️  Starting image display...");
  epd_display(imageBuffer);
  Serial.println("✅ Image display complete!");
  uploadBytesReceived = 0;
}

// === Other HTTP Handlers ===
void handleStatus() {
  DynamicJsonDocument doc(500);
  doc["status"]              = "running";
  doc["service"]             = "epaper_6color_frame";
  doc["display_initialized"] = displayInitialized;
  doc["resolution"]          = String(EPD_WIDTH) + "x" + String(EPD_HEIGHT);
  doc["buffer_size"]         = EPD_BUFFER_SIZE;
  doc["ip_address"]          = WiFi.localIP().toString();
  doc["free_heap"]           = ESP.getFreeHeap();
  String resp;
  serializeJson(doc, resp);
  server.send(200, "application/json", resp);
}

void handleClear() {
  if (!displayInitialized) {
    server.send(500, "application/json", "{\"error\":\"Display not initialized\"}");
    return;
  }
  Serial.println("🧹 Clearing display to white...");
  epd_clear(EPD_WHITE);
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Display cleared\"}");
}

void handleTest() {
  if (!displayInitialized) {
    server.send(500, "application/json", "{\"error\":\"Display not ready\"}");
  } else {
    Serial.println("🧪 Showing test pattern...");
    showTestPattern();
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Test pattern displayed\"}");
  }
}

// === Application Layer ===
void setup() {
  Serial.begin(115200);
  delay(2000);

  // Initialize display
  if (epd_init() == 0) {
    displayInitialized = true;
    Serial.println("✅ Display initialized");
    Serial.println("🎨 Showing test pattern...");
    showTestPattern();
    Serial.println("✅ Test pattern complete");
  } else {
    Serial.println("❌ Display init failed");
    return;
  }

  Serial.println("🌐 Starting WiFi connection...");
  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.printf("✅ WiFi connected: %s\n", WiFi.localIP().toString().c_str());

  // Setup HTTP routes
  server.on("/api/display/frame", HTTP_POST, handleFrameUpload, handleFrameUploadBody);
  server.on("/api/status",        HTTP_GET,  handleStatus);
  server.on("/api/clear",         HTTP_GET,  handleClear);
  server.on("/api/test",          HTTP_GET,  handleTest);

  server.begin();
  Serial.println("✅ HTTP server running on port 80");
}

void loop() {
  server.handleClient();
  delay(1);
}