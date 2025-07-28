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

// 6-color definitions (4 bits per pixel, as per Waveshare EPD7in3E spec)
#define EPD_BLACK   0x00  // 0000
#define EPD_WHITE   0x01  // 0001  
#define EPD_YELLOW  0x02  // 0010
#define EPD_RED     0x03  // 0011
#define EPD_BLUE    0x05  // 0101
#define EPD_GREEN   0x06  // 0110

// WiFi Configuration
const char* WIFI_SSID = "Jon6";
const char* WIFI_PASSWORD = "fv4!F48P8&tR";

WebServer server(80);
bool displayInitialized = false;

// === Hardware Interface Layer (based on Waveshare epdif.cpp) ===

void epd_digital_write(int pin, int value) {
  digitalWrite(pin, value);
}

int epd_digital_read(int pin) {
  return digitalRead(pin);
}

void epd_delay_ms(unsigned int delaytime) {
  delay(delaytime);
}

void epd_spi_transfer(unsigned char data) {
  epd_digital_write(EPD_CS, LOW);
  SPI.transfer(data);
  epd_digital_write(EPD_CS, HIGH);
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

// === E-Paper Driver Layer (based on Waveshare epd7in3e.cpp) ===

void epd_send_command(unsigned char command) {
  epd_digital_write(EPD_DC, LOW);
  epd_spi_transfer(command);
}

void epd_send_data(unsigned char data) {
  epd_digital_write(EPD_DC, HIGH);
  epd_spi_transfer(data);
}

void epd_read_busy_h(void) {
  Serial.print("e-Paper busy H\r\n");
  while(epd_digital_read(EPD_BUSY) == LOW) {      // LOW: busy, HIGH: idle
    epd_delay_ms(5);
  } 
  Serial.print("e-Paper busy release H\r\n");
}

void epd_read_busy_l(void) {
  Serial.print("e-Paper busy L\r\n");
  while(epd_digital_read(EPD_BUSY) == HIGH) {     // LOW: idle, HIGH: busy
    epd_delay_ms(5);
  }      
  Serial.print("e-Paper busy release L\r\n");
}

void epd_reset(void) {
  epd_digital_write(EPD_RST, HIGH);
  epd_delay_ms(20);    
  epd_digital_write(EPD_RST, LOW);                // module reset    
  epd_delay_ms(2);
  epd_digital_write(EPD_RST, HIGH);
  epd_delay_ms(20);     
}

void epd_turn_on_display(void) {
  epd_send_command(0x12); // DISPLAY_REFRESH
  epd_send_data(0x01);
  epd_read_busy_h();

  epd_send_command(0x02); // POWER_OFF
  epd_send_data(0X00);
  epd_read_busy_h();
}

int epd_init(void) {
  // Initialize hardware interface
  if (epd_if_init() != 0) {
    return -1;
  }
  
  epd_reset();
  epd_read_busy_h();
  epd_delay_ms(30);

  // Initialization sequence for EPD7in3E (6-color)
  epd_send_command(0xAA);
  epd_send_data(0x49);
  epd_send_data(0x55);
  epd_send_data(0x20);
  epd_send_data(0x08);
  epd_send_data(0x09);
  epd_send_data(0x18);

  epd_send_command(0x01);
  epd_send_data(0x3F);

  epd_send_command(0x00);
  epd_send_data(0x4F);
  epd_send_data(0x69);

  epd_send_command(0x05);
  epd_send_data(0x40);
  epd_send_data(0x1F);
  epd_send_data(0x1F);
  epd_send_data(0x2C);

  epd_send_command(0x08);
  epd_send_data(0x6F);
  epd_send_data(0x1F);
  epd_send_data(0x1F);
  epd_send_data(0x22);

  // First setting (from Waveshare 20211212 update)
  epd_send_command(0x06);
  epd_send_data(0x6F);
  epd_send_data(0x1F);
  epd_send_data(0x14);
  epd_send_data(0x14);

  epd_send_command(0x03);
  epd_send_data(0x00);
  epd_send_data(0x54);
  epd_send_data(0x00);
  epd_send_data(0x44);

  epd_send_command(0x60);
  epd_send_data(0x02);
  epd_send_data(0x00);
  
  // PLL must be set for version 2 IC
  epd_send_command(0x30);
  epd_send_data(0x08);

  epd_send_command(0x50);
  epd_send_data(0x3F);

  epd_send_command(0x61);
  epd_send_data(0x03);  // 800 pixels width (0x0320)
  epd_send_data(0x20);
  epd_send_data(0x01);  // 480 pixels height (0x01E0)
  epd_send_data(0xE0); 

  epd_send_command(0xE3);
  epd_send_data(0x2F);

  epd_send_command(0x84);
  epd_send_data(0x01);
  
  return 0;
}

void epd_clear(unsigned char color) {
  unsigned int Width = (EPD_WIDTH % 4 == 0) ? (EPD_WIDTH / 4) : (EPD_WIDTH / 4 + 1);
  unsigned int Height = EPD_HEIGHT;
  
  epd_send_command(0x04);
  epd_read_busy_h();

  epd_send_command(0x10);
  for (unsigned int j = 0; j < Height; j++) {
    for (unsigned int i = 0; i < Width; i++) {
      epd_send_data((color<<6) | (color<<4) | (color<<2) | color);
    }
  }

  epd_turn_on_display();
}

void epd_display(unsigned char *image) {
  unsigned int Width = (EPD_WIDTH % 4 == 0) ? (EPD_WIDTH / 4) : (EPD_WIDTH / 4 + 1);
  unsigned int Height = EPD_HEIGHT;
  
  epd_send_command(0x04);
  epd_read_busy_h();

  epd_send_command(0x10);
  for (unsigned int j = 0; j < Height; j++) {
    for (unsigned int i = 0; i < Width; i++) {
      epd_send_data(image[i + j * Width]);
    }
  }

  epd_turn_on_display();
}

void epd_sleep(void) {
  epd_send_command(0x02); // POWER_OFF
  epd_send_data(0X00);
  epd_send_command(0x07); // DEEP_SLEEP
  epd_send_data(0XA5);
}

// === Application Layer ===

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("ESP32 E-Paper 6-Color Frame");
  Serial.println("============================");
  Serial.printf("Display: Waveshare 7.3inch E-Paper (E) - %dx%d pixels, 6-color\n", EPD_WIDTH, EPD_HEIGHT);
  Serial.printf("Pin Config: CS=%d, DC=%d, RST=%d, BUSY=%d\n", EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
  
  // Initialize display
  Serial.println("\n--- Display Initialization ---");
  Serial.println("Initializing e-paper display...");
  if (epd_init() == 0) {
    displayInitialized = true;
    Serial.println("✅ Display initialized successfully");
    
    // Show test pattern
    showTestPattern();
  } else {
    Serial.println("❌ Display initialization failed");
    displayInitialized = false;
    return; // Don't continue if display failed
  }
  
  // Setup WiFi
  Serial.println("\n--- WiFi Connection ---");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.printf("✅ WiFi connected! IP Address: %s\n", WiFi.localIP().toString().c_str());
  
  // Setup HTTP server
  server.on("/api/display/frame", HTTP_POST, handleFrameUpload);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/clear", HTTP_GET, handleClear);
  server.on("/api/test", HTTP_GET, handleTest);
  server.begin();
  
  Serial.println("\n--- Server Started ---");
  Serial.println("✅ HTTP server running on port 80");
  Serial.println("Ready for 6-color images!");
  Serial.println("\nAvailable endpoints:");
  Serial.println("  GET  /api/status       - Get device status");
  Serial.println("  GET  /api/clear        - Clear display");
  Serial.println("  GET  /api/test         - Show test pattern");
  Serial.println("  POST /api/display/frame - Upload image data");
}

void loop() {
  server.handleClient();
  delay(1);
}

void showTestPattern() {
  if (!displayInitialized) {
    Serial.println("Cannot show test pattern - display not initialized");
    return;
  }
  
  Serial.println("Showing 6-color test pattern...");
  Serial.println("This will take ~12 seconds to refresh...");
  
  try {
    unsigned int Width = (EPD_WIDTH % 4 == 0) ? (EPD_WIDTH / 4) : (EPD_WIDTH / 4 + 1);
    
    epd_send_command(0x04);
    epd_read_busy_h();
    epd_send_command(0x10);
    
    // Create a test pattern with 6 color bands
    for (unsigned int y = 0; y < EPD_HEIGHT; y++) {
      for (unsigned int x = 0; x < Width; x++) {
        unsigned char pixelData;
        
        // Create horizontal color bands
        if (y < 80) {
          pixelData = (EPD_BLACK << 4) | EPD_BLACK;    // Black band
        } else if (y < 160) {
          pixelData = (EPD_WHITE << 4) | EPD_WHITE;    // White band
        } else if (y < 240) {
          pixelData = (EPD_YELLOW << 4) | EPD_YELLOW;  // Yellow band
        } else if (y < 320) {
          pixelData = (EPD_RED << 4) | EPD_RED;        // Red band
        } else if (y < 400) {
          pixelData = (EPD_BLUE << 4) | EPD_BLUE;      // Blue band
        } else {
          pixelData = (EPD_GREEN << 4) | EPD_GREEN;    // Green band
        }
        
        epd_send_data(pixelData);
      }
    }
    
    epd_turn_on_display();
    Serial.println("✅ 6-color test pattern displayed successfully!");
    Serial.println("CHECK YOUR E-PAPER DISPLAY NOW!");
    Serial.println("You should see 6 colored bands: Black, White, Yellow, Red, Blue, Green");
    
  } catch (...) {
    Serial.println("❌ Failed to show test pattern");
  }
}

void handleFrameUpload() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No image data received\"}");
    return;
  }
  
  String frameData = server.arg("plain");
  Serial.printf("Received frame data: %d bytes\n", frameData.length());
  
  // Calculate expected data size (4 bits per pixel, 2 pixels per byte)
  unsigned int expectedSize = (EPD_WIDTH * EPD_HEIGHT) / 2;
  
  if (frameData.length() != expectedSize) {
    Serial.printf("Warning: Expected %d bytes, got %d bytes\n", expectedSize, frameData.length());
  }
  
  if (displayFrame(frameData)) {
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Image displayed\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"Failed to display image\"}");
  }
}

bool displayFrame(String frameData) {
  if (!displayInitialized) {
    Serial.println("Cannot display frame - display not initialized");
    return false;
  }
  
  Serial.println("Displaying received frame...");
  Serial.println("This will take ~12 seconds to refresh...");
  
  try {
    epd_send_command(0x04);
    epd_read_busy_h();
    epd_send_command(0x10);
    
    // Send the frame data directly to display
    for (int i = 0; i < frameData.length(); i++) {
      epd_send_data((unsigned char)frameData[i]);
    }
    
    epd_turn_on_display();
    Serial.println("✅ Frame displayed successfully");
    return true;
    
  } catch (...) {
    Serial.println("❌ Failed to display frame");
    return false;
  }
}

void handleStatus() {
  DynamicJsonDocument doc(500);
  doc["status"] = "running";
  doc["service"] = "epaper_6color_frame";
  doc["display_initialized"] = displayInitialized;
  doc["display_type"] = "Waveshare 7.3inch E-Paper (E) 6-color";
  doc["resolution"] = String(EPD_WIDTH) + "x" + String(EPD_HEIGHT);
  doc["colors"] = 6;
  doc["ip_address"] = WiFi.localIP().toString();
  doc["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime_ms"] = millis();
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleClear() {
  if (!displayInitialized) {
    server.send(500, "application/json", "{\"error\":\"Display not initialized\"}");
    return;
  }
  
  Serial.println("Clearing display to white...");
  epd_clear(EPD_WHITE);
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Display cleared\"}");
}

void handleTest() {
  if (!displayInitialized) {
    server.send(500, "application/json", "{\"error\":\"Display not initialized\"}");
    return;
  }
  
  Serial.println("Showing test pattern via web request...");
  showTestPattern();
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Test pattern displayed\"}");
}