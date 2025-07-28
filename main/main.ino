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
// Buffer size (4 bits per pixel = 2 pixels/byte)
#define EPD_BUFFER_SIZE ((EPD_WIDTH * EPD_HEIGHT) / 2)

// 6-color definitions (4 bits per pixel)
#define EPD_BLACK   0x00
#define EPD_WHITE   0x01
#define EPD_YELLOW  0x02
#define EPD_RED     0x03
#define EPD_BLUE    0x05
#define EPD_GREEN   0x06

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

void epd_read_busy_h(void) {
  while (epd_digital_read(EPD_BUSY) == LOW) {
    epd_delay_ms(5);
  }
}

void epd_read_busy_l(void) {
  while (epd_digital_read(EPD_BUSY) == HIGH) {
    epd_delay_ms(5);
  }
}

void epd_reset(void) {
  epd_digital_write(EPD_RST, HIGH);
  epd_delay_ms(20);
  epd_digital_write(EPD_RST, LOW);
  epd_delay_ms(2);
  epd_digital_write(EPD_RST, HIGH);
  epd_delay_ms(20);
}

void epd_turn_on_display(void) {
  epd_send_command(0x12); // DISPLAY_REFRESH
  epd_send_data(0x01);
  epd_read_busy_h();

  epd_send_command(0x02); // POWER_OFF
  epd_send_data(0x00);
  epd_read_busy_h();
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

// === E-Paper Driver Layer ===
void epd_send_init_sequence() {
  epd_send_command(0xAA);
  epd_send_data(0x49); epd_send_data(0x55); epd_send_data(0x20);
  epd_send_data(0x08); epd_send_data(0x09); epd_send_data(0x18);
  epd_send_command(0x01); epd_send_data(0x3F);
  epd_send_command(0x00); epd_send_data(0x4F); epd_send_data(0x69);
  epd_send_command(0x05); epd_send_data(0x40); epd_send_data(0x1F);
    epd_send_data(0x1F); epd_send_data(0x2C);
  epd_send_command(0x08); epd_send_data(0x6F); epd_send_data(0x1F);
    epd_send_data(0x1F); epd_send_data(0x22);
  epd_send_command(0x06); epd_send_data(0x6F); epd_send_data(0x1F);
    epd_send_data(0x14); epd_send_data(0x14);
  epd_send_command(0x03); epd_send_data(0x00); epd_send_data(0x54);
    epd_send_data(0x00); epd_send_data(0x44);
  epd_send_command(0x60); epd_send_data(0x02); epd_send_data(0x00);
  epd_send_command(0x30); epd_send_data(0x08);
  epd_send_command(0x50); epd_send_data(0x3F);
  epd_send_command(0x61); epd_send_data(0x03); epd_send_data(0x20);
    epd_send_data(0x01); epd_send_data(0xE0);
  epd_send_command(0xE3); epd_send_data(0x2F);
  epd_send_command(0x84); epd_send_data(0x01);
}

int epd_init(void) {
  if (epd_if_init() != 0) return -1;
  epd_reset();
  epd_read_busy_h();
  epd_delay_ms(30);
  epd_send_init_sequence();
  return 0;
}

// Clear display to a single color
void epd_clear(unsigned char color) {
  unsigned int W = (EPD_WIDTH % 4 == 0 ? EPD_WIDTH/4 : EPD_WIDTH/4+1);
  unsigned int H = EPD_HEIGHT;
  epd_send_command(0x04);
  epd_read_busy_h();
  epd_send_command(0x10);
  for (unsigned int j = 0; j < H; j++)
    for (unsigned int i = 0; i < W; i++)
      epd_send_data((color<<6)|(color<<4)|(color<<2)|color);
  epd_turn_on_display();
}

// Display raw buffer (4-bit pixels)
void epd_display(unsigned char *image) {
  unsigned int W = (EPD_WIDTH % 4 == 0 ? EPD_WIDTH/4 : EPD_WIDTH/4+1);
  unsigned int H = EPD_HEIGHT;
  epd_send_command(0x04);
  epd_read_busy_h();
  epd_send_command(0x10);
  for (unsigned int j = 0; j < H; j++)
    for (unsigned int i = 0; i < W; i++)
      epd_send_data(image[i + j * W]);
  epd_turn_on_display();
}

void epd_sleep(void) {
  epd_send_command(0x02);
  epd_send_data(0x00);
  epd_send_command(0x07);
  epd_send_data(0xA5);
}

// Original test pattern
void showTestPattern() {
  if (!displayInitialized) return;
  unsigned int W = (EPD_WIDTH % 4 == 0 ? EPD_WIDTH/4 : EPD_WIDTH/4+1);
  unsigned int H = EPD_HEIGHT;
  epd_send_command(0x04);
  epd_read_busy_h();
  epd_send_command(0x10);
  for (unsigned int y = 0; y < H; y++) {
    uint8_t band = y/80;
    uint8_t pixel = 0;
    switch (band) {
      case 0: pixel = (EPD_BLACK<<4)|EPD_BLACK; break;
      case 1: pixel = (EPD_WHITE<<4)|EPD_WHITE; break;
      case 2: pixel = (EPD_YELLOW<<4)|EPD_YELLOW; break;
      case 3: pixel = (EPD_RED<<4)|EPD_RED; break;
      case 4: pixel = (EPD_BLUE<<4)|EPD_BLUE; break;
      default: pixel = (EPD_GREEN<<4)|EPD_GREEN; break;
    }
    for (unsigned int x = 0; x < W; x++) {
      epd_send_data(pixel);
    }
  }
  epd_turn_on_display();
}

// === HTTP Upload Handlers from Updated Version ===
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
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Image received\"}");
  // Render from raw buffer
  epd_display(imageBuffer);
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
  epd_clear(EPD_WHITE);
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Display cleared\"}");
}

void handleTest() {
  if (!displayInitialized) {
    server.send(500, "application/json", "{\"error\":\"Display not ready\"}");
  } else {
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
    showTestPattern();
  } else {
    Serial.println("❌ Display init failed");
    return;
  }

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
