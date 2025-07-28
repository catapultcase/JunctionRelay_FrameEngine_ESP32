#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPI.h>

// Pin definitions
#define EPD_CS    10
#define EPD_DC    9
#define EPD_RST   6
#define EPD_BUSY  5

// Display specifications for E6
#define EPD_WIDTH   800
#define EPD_HEIGHT  480
#define EPD_BUFFER_SIZE 192000  // 4 bits per pixel (800 * 480 / 2)

// E6 6-color definitions (4-bit values for easier handling)
#define EPD_BLACK   0x0
#define EPD_WHITE   0x1
#define EPD_YELLOW  0x2
#define EPD_RED     0x3
#define EPD_BLUE    0x5
#define EPD_GREEN   0x6

// WiFi Configuration
const char* WIFI_SSID = "Jon6";
const char* WIFI_PASSWORD = "fv4!F48P8&tR";

WebServer server(80);
bool displayInitialized = false;

// Allocate buffer for image data
uint8_t* imageBuffer = nullptr;

// === Hardware Interface ===
void epd_digital_write(int pin, int value) { digitalWrite(pin, value); }
int epd_digital_read(int pin) { return digitalRead(pin); }
void epd_delay_ms(unsigned int ms) { delay(ms); }

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
  Serial.print("e-Paper busy... ");
  while(epd_digital_read(EPD_BUSY) == LOW) {
    epd_delay_ms(5);
  } 
  Serial.println("ready");
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
  epd_send_command(0x12);
  epd_send_data(0x01);
  epd_read_busy_h();
  epd_send_command(0x02);
  epd_send_data(0X00);
  epd_read_busy_h();
}

int epd_init(void) {
  pinMode(EPD_CS, OUTPUT);
  pinMode(EPD_RST, OUTPUT);
  pinMode(EPD_DC, OUTPUT);
  pinMode(EPD_BUSY, INPUT); 

  SPI.begin();
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  
  epd_reset();
  epd_read_busy_h();
  epd_delay_ms(30);

  // EPD7in3E E6 initialization sequence
  epd_send_command(0xAA);
  epd_send_data(0x49); epd_send_data(0x55); epd_send_data(0x20);
  epd_send_data(0x08); epd_send_data(0x09); epd_send_data(0x18);
  epd_send_command(0x01); epd_send_data(0x3F);
  epd_send_command(0x00); epd_send_data(0x4F); epd_send_data(0x69);
  epd_send_command(0x05); epd_send_data(0x40); epd_send_data(0x1F); epd_send_data(0x1F); epd_send_data(0x2C);
  epd_send_command(0x08); epd_send_data(0x6F); epd_send_data(0x1F); epd_send_data(0x1F); epd_send_data(0x22);
  epd_send_command(0x06); epd_send_data(0x6F); epd_send_data(0x1F); epd_send_data(0x14); epd_send_data(0x14);
  epd_send_command(0x03); epd_send_data(0x00); epd_send_data(0x54); epd_send_data(0x00); epd_send_data(0x44);
  epd_send_command(0x60); epd_send_data(0x02); epd_send_data(0x00);
  epd_send_command(0x30); epd_send_data(0x08);
  epd_send_command(0x50); epd_send_data(0x3F);
  epd_send_command(0x61); epd_send_data(0x03); epd_send_data(0x20); epd_send_data(0x01); epd_send_data(0xE0);
  epd_send_command(0xE3); epd_send_data(0x2F);
  epd_send_command(0x84); epd_send_data(0x01);
  
  return 0;
}

void epd_display_image(uint8_t* image_data, uint32_t data_size) {
  Serial.printf("Displaying image... %d bytes\n", data_size);
  
  epd_send_command(0x04);
  epd_read_busy_h();
  epd_send_command(0x10);
  
  // Send image data directly (already in correct 4-bit format)
  for (uint32_t i = 0; i < data_size; i++) {
    epd_send_data(image_data[i]);
  }
  
  epd_turn_on_display();
  Serial.println("✅ Image displayed");
}

void epd_show_test_pattern() {
  Serial.println("Showing E6 6-color test pattern...");
  
  epd_send_command(0x04);
  epd_read_busy_h();
  epd_send_command(0x10);
  
  // Create 6 color bands (80 pixels each) using 4-bit format
  uint8_t colors[6] = {EPD_BLACK, EPD_WHITE, EPD_YELLOW, EPD_RED, EPD_BLUE, EPD_GREEN};
  
  for (int y = 0; y < EPD_HEIGHT; y++) {
    // Determine which color band this row belongs to
    int color_index = min(y / 80, 5);
    uint8_t color = colors[color_index];
    
    for (int x = 0; x < EPD_WIDTH / 2; x++) {  // 2 pixels per byte
      // Pack 2 pixels of the same color into one byte
      uint8_t pixelData = (color << 4) | color;
      epd_send_data(pixelData);
    }
  }
  
  epd_turn_on_display();
  Serial.println("✅ Test pattern complete");
}

// Global variables for upload handling
int uploadBytesReceived = 0;
bool uploadInProgress = false;

// === HTTP Handlers ===
void handleFrameUpload() {
  Serial.println("=== Image Upload - Main Handler ===");
  
  if (!displayInitialized) {
    Serial.println("❌ Display not ready");
    server.send(500, "application/json", "{\"error\":\"Display not ready\"}");
    return;
  }
  
  // Check if upload was successful
  if (uploadInProgress) {
    Serial.println("❌ Upload still in progress");
    server.send(500, "application/json", "{\"error\":\"Upload in progress\"}");
    return;
  }
  
  if (uploadBytesReceived != EPD_BUFFER_SIZE) {
    Serial.printf("❌ Upload failed. Expected %d bytes, got %d\n", EPD_BUFFER_SIZE, uploadBytesReceived);
    server.send(400, "application/json", "{\"error\":\"Upload failed or incomplete\"}");
    uploadBytesReceived = 0; // Reset for next attempt
    return;
  }
  
  // Send success response first
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Image received successfully\"}");
  
  Serial.println("✅ Data received successfully!");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // Display the image
  Serial.println("Starting display update...");
  epd_display_image(imageBuffer, EPD_BUFFER_SIZE);
  
  // Reset for next upload
  uploadBytesReceived = 0;
}

void handleFrameUploadBody() {
  Serial.println("=== Upload Body Handler ===");
  
  // Get the uploaded data
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("Upload started");
    uploadInProgress = true;
    uploadBytesReceived = 0;
    
    Serial.printf("Free heap before: %d bytes\n", ESP.getFreeHeap());
    
    // Allocate buffer if not already allocated
    if (imageBuffer == nullptr) {
      imageBuffer = (uint8_t*)malloc(EPD_BUFFER_SIZE);
      if (imageBuffer == nullptr) {
        Serial.println("❌ Failed to allocate memory");
        uploadInProgress = false;
        return;
      }
      Serial.printf("Buffer allocated: %d bytes\n", EPD_BUFFER_SIZE);
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    // Write data to buffer
    if (uploadBytesReceived + upload.currentSize <= EPD_BUFFER_SIZE) {
      memcpy(imageBuffer + uploadBytesReceived, upload.buf, upload.currentSize);
      uploadBytesReceived += upload.currentSize;
      
      // Progress update every 20KB
      if (uploadBytesReceived % 20480 == 0 || uploadBytesReceived >= EPD_BUFFER_SIZE) {
        Serial.printf("Progress: %d/%d bytes (%.1f%%)\n", 
                     uploadBytesReceived, EPD_BUFFER_SIZE, 
                     (float)uploadBytesReceived * 100 / EPD_BUFFER_SIZE);
      }
    } else {
      Serial.printf("⚠️ Buffer overflow! Received %d, buffer size %d\n", 
                   uploadBytesReceived + upload.currentSize, EPD_BUFFER_SIZE);
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    uploadInProgress = false;
    Serial.printf("Upload finished: %d bytes total\n", uploadBytesReceived);
    Serial.printf("Time taken: %lu ms\n", millis());
  }
  else if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadInProgress = false;
    uploadBytesReceived = 0;
    Serial.println("❌ Upload aborted");
  }
}

void handleStatus() {
  DynamicJsonDocument doc(400);
  doc["status"] = "running";
  doc["service"] = "epaper_6color_frame";
  doc["display_initialized"] = displayInitialized;
  doc["display_type"] = "Waveshare 7.3inch E6 Spectra 6-color";
  doc["resolution"] = "800x480";
  doc["colors"] = 6;
  doc["buffer_size"] = EPD_BUFFER_SIZE;
  doc["ip_address"] = WiFi.localIP().toString();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["buffer_allocated"] = (imageBuffer != nullptr);
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleTest() {
  if (displayInitialized) {
    epd_show_test_pattern();
    server.send(200, "application/json", "{\"status\":\"success\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"Display not ready\"}");
  }
}

void handleClear() {
  if (displayInitialized) {
    Serial.println("Clearing display...");
    
    epd_send_command(0x04);
    epd_read_busy_h();
    epd_send_command(0x10);
    
    // Fill with white (4-bit format)
    for (int i = 0; i < EPD_BUFFER_SIZE; i++) {
      epd_send_data((EPD_WHITE << 4) | EPD_WHITE);
    }
    
    epd_turn_on_display();
    server.send(200, "application/json", "{\"status\":\"success\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"Display not ready\"}");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("ESP32 E6 Spectra 6-Color E-Paper Frame");
  Serial.println("=====================================");
  Serial.printf("Buffer size: %d bytes\n", EPD_BUFFER_SIZE);
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // Initialize display
  if (epd_init() == 0) {
    displayInitialized = true;
    Serial.println("✅ Display ready");
    epd_show_test_pattern();
  } else {
    Serial.println("❌ Display failed");
  }
  
  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n✅ WiFi: %s\n", WiFi.localIP().toString().c_str());
  
  // Start server with upload handler
  server.on("/api/display/frame", HTTP_POST, handleFrameUpload, handleFrameUploadBody);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/test", HTTP_GET, handleTest);
  server.on("/api/clear", HTTP_GET, handleClear);
  server.begin();
  
  Serial.println("✅ Server ready");
  Serial.printf("Free heap after setup: %d bytes\n", ESP.getFreeHeap());
}

void loop() {
  server.handleClient();
  delay(1);
}