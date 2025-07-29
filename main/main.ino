#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// Pin definitions for ESP32 S3 Feather
#define EPD_CS    10  // D10 - Chip Select
#define EPD_DC    9   // D9  - Data/Command  
#define EPD_RST   6   // D6  - Reset
#define EPD_BUSY  5   // D5  - Busy

// Display specifications
#define EPD_WIDTH   800
#define EPD_HEIGHT  480
#define EPD_BUFFER_SIZE ((EPD_WIDTH * EPD_HEIGHT) / 2)

// 6-color definitions
#define EPD_BLACK   0x00
#define EPD_WHITE   0x01
#define EPD_YELLOW  0x02
#define EPD_RED     0x03
#define EPD_BLUE    0x05
#define EPD_GREEN   0x06

// WiFi Configuration
const char* WIFI_SSID     = "Jon6";
const char* WIFI_PASSWORD = "fv4!F48P8&tR";

// === MEMORY-EFFICIENT APPROACH ===
// Use PSRAM for large buffers and shared memory between tasks
uint8_t* sharedImageBuffer = nullptr;  // Single shared buffer
SemaphoreHandle_t bufferMutex;
TaskHandle_t httpTaskHandle;
TaskHandle_t displayTaskHandle;
QueueHandle_t displayQueue;
SemaphoreHandle_t displayMutex;

// Display command types
enum DisplayCommand {
  CMD_SHOW_SHARED_IMAGE,  // Use shared buffer
  CMD_SHOW_TEST_PATTERN,
  CMD_CLEAR_DISPLAY,
  CMD_SLEEP_DISPLAY
};

struct DisplayMessage {
  DisplayCommand command;
  uint8_t clearColor;
  bool newImageReady;
};

// === SHARED STATE (PROTECTED BY MUTEX) ===
struct DisplayState {
  bool initialized;
  bool busy;
  String lastOperation;
  unsigned long lastUpdateTime;
  String status;
} displayState;

WebServer server(80);
unsigned int uploadBytesReceived = 0;
bool uploadInProgress = false;
bool newImagePending = false;

// === HARDWARE INTERFACE (DISPLAY CORE ONLY) ===
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

// === CADRE-STYLE BUSY HANDLING (DISPLAY CORE ONLY) ===
void epd_busy_wait(float timeout_seconds) {
  unsigned long timeout_ms = (unsigned long)(timeout_seconds * 1000);
  
  if (epd_digital_read(EPD_BUSY) == HIGH) {
    Serial.printf("[DISPLAY] BUSY already high, waiting %.1fs...\n", timeout_seconds);
    delay(timeout_ms);
    return;
  }
  
  Serial.printf("[DISPLAY] Waiting for BUSY (timeout: %.1fs)...", timeout_seconds);
  unsigned long start_time = millis();
  
  while (epd_digital_read(EPD_BUSY) == LOW) {
    delay(10);
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

void epd_send_init_sequence() {
  // Command Header
  epd_send_command(0xAA);
  epd_send_data(0x49); epd_send_data(0x55); epd_send_data(0x20);
  epd_send_data(0x08); epd_send_data(0x09); epd_send_data(0x18);
  
  // Power Setting
  epd_send_command(0x01);
  epd_send_data(0x3F); epd_send_data(0x00); epd_send_data(0x32);
  epd_send_data(0x2A); epd_send_data(0x0E); epd_send_data(0x2A);
  
  // Panel Setting
  epd_send_command(0x00);
  epd_send_data(0x5F); epd_send_data(0x69);
  
  // Additional settings...
  epd_send_command(0x03);
  epd_send_data(0x00); epd_send_data(0x54); epd_send_data(0x00); epd_send_data(0x44);
  
  epd_send_command(0x05);
  epd_send_data(0x40); epd_send_data(0x1F); epd_send_data(0x1F); epd_send_data(0x2C);
  
  epd_send_command(0x06);
  epd_send_data(0x6F); epd_send_data(0x1F); epd_send_data(0x16); epd_send_data(0x25);
  
  epd_send_command(0x08);
  epd_send_data(0x6F); epd_send_data(0x1F); epd_send_data(0x1F); epd_send_data(0x22);
  
  epd_send_command(0x30); epd_send_data(0x02); epd_send_data(0x00);
  epd_send_command(0x41); epd_send_data(0x00);
  epd_send_command(0x50); epd_send_data(0x3F);
  epd_send_command(0x60); epd_send_data(0x02); epd_send_data(0x00);
  
  epd_send_command(0x61);
  epd_send_data(0x03); epd_send_data(0x20); epd_send_data(0x01); epd_send_data(0xE0);
  
  epd_send_command(0x82); epd_send_data(0x1E);
  epd_send_command(0x84); epd_send_data(0x01);
  epd_send_command(0x86); epd_send_data(0x00);
  epd_send_command(0xE0); epd_send_data(0x01);
  epd_send_command(0xE3); epd_send_data(0x2F);
  epd_send_command(0xE6); epd_send_data(0x00);
}

int epd_init(void) {
  if (epd_if_init() != 0) return -1;
  epd_reset();
  epd_busy_wait(0.1);
  epd_delay_ms(30);
  epd_send_init_sequence();
  return 0;
}

void epd_turn_on_display(void) {
  Serial.println("[DISPLAY] Starting display update sequence...");
  
  epd_send_command(0x04); // Power ON  
  epd_busy_wait(1.0);     // Increased from 0.4s to 1.0s
  epd_delay_ms(500);      // Extra power stabilization
  
  epd_send_command(0x12); // DISPLAY_REFRESH
  epd_send_data(0x01);
  epd_busy_wait(60.0);    // Increased from 45s to 60s
  
  epd_delay_ms(1000);     // Delay before power off
  epd_send_command(0x02); // POWER_OFF
  epd_send_data(0x00);
  epd_busy_wait(1.0);     // Increased from 0.4s to 1.0s
  epd_delay_ms(500);      // Final stabilization
  
  Serial.println("[DISPLAY] Display update complete");
}

uint8_t epd_correct_color(uint8_t color) {
  if (color > 5) color = 5;
  if (color >= 4) color += 1;
  return color;
}

void epd_display_shared_image() {
  if (!sharedImageBuffer) {
    Serial.println("[DISPLAY] ERROR: No shared image buffer");
    return;
  }
  
  // Lock the shared buffer
  if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    Serial.println("[DISPLAY] ERROR: Could not lock buffer");
    return;
  }
  
  unsigned int W = (EPD_WIDTH + 1) / 2;
  unsigned int H = EPD_HEIGHT;
  
  Serial.println("[DISPLAY] Sending shared image data...");
  epd_send_command(0x10);
  
  for (unsigned int j = 0; j < H; j++) {
    for (unsigned int i = 0; i < W; i++) {
      uint8_t byte_val = sharedImageBuffer[i + j * W];
      uint8_t pixel1 = epd_correct_color((byte_val >> 4) & 0x0F);
      uint8_t pixel2 = epd_correct_color(byte_val & 0x0F);
      epd_send_data((pixel1 << 4) | pixel2);
    }
  }
  
  // Release the buffer lock
  xSemaphoreGive(bufferMutex);
  
  epd_turn_on_display();
}

void epd_clear(unsigned char color) {
  unsigned int W = (EPD_WIDTH + 1) / 2;
  unsigned int H = EPD_HEIGHT;
  
  Serial.printf("[DISPLAY] Clearing to color 0x%02X...\n", color);
  epd_send_command(0x10);
  
  for (unsigned int j = 0; j < H; j++) {
    for (unsigned int i = 0; i < W; i++) {
      epd_send_data((color<<6)|(color<<4)|(color<<2)|color);
    }
  }
  
  epd_turn_on_display();
}

void epd_show_test_pattern() {
  unsigned int W = (EPD_WIDTH + 1) / 2;
  unsigned int H = EPD_HEIGHT;
  
  Serial.println("[DISPLAY] Generating test pattern...");
  epd_send_command(0x10);
  
  for (unsigned int y = 0; y < H; y++) {
    uint8_t band = y / 80;
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

// === DISPLAY TASK (CORE 1) ===
void displayTask(void* parameter) {
  Serial.println("[DISPLAY] Display task starting on core " + String(xPortGetCoreID()));
  
  // Initialize display hardware
  if (epd_init() == 0) {
    xSemaphoreTake(displayMutex, portMAX_DELAY);
    displayState.initialized = true;
    displayState.status = "Display initialized";
    xSemaphoreGive(displayMutex);
    
    Serial.println("[DISPLAY] Hardware initialized successfully");
    
    // Show initial test pattern
    epd_show_test_pattern();
    
    xSemaphoreTake(displayMutex, portMAX_DELAY);
    displayState.lastOperation = "Test pattern";
    displayState.lastUpdateTime = millis();
    displayState.busy = false;
    xSemaphoreGive(displayMutex);
  } else {
    Serial.println("[DISPLAY] Hardware initialization failed");
    xSemaphoreTake(displayMutex, portMAX_DELAY);
    displayState.status = "Display init failed";
    xSemaphoreGive(displayMutex);
  }
  
  DisplayMessage msg;
  
  // Main display loop - process commands from queue
  while (true) {
    if (xQueueReceive(displayQueue, &msg, portMAX_DELAY)) {
      xSemaphoreTake(displayMutex, portMAX_DELAY);
      displayState.busy = true;
      xSemaphoreGive(displayMutex);
      
      switch (msg.command) {
        case CMD_SHOW_SHARED_IMAGE:
          Serial.println("[DISPLAY] Processing shared image command...");
          epd_display_shared_image();
          xSemaphoreTake(displayMutex, portMAX_DELAY);
          displayState.lastOperation = "Image display";
          xSemaphoreGive(displayMutex);
          break;
          
        case CMD_SHOW_TEST_PATTERN:
          Serial.println("[DISPLAY] Processing test pattern command...");
          epd_show_test_pattern();
          xSemaphoreTake(displayMutex, portMAX_DELAY);
          displayState.lastOperation = "Test pattern";
          xSemaphoreGive(displayMutex);
          break;
          
        case CMD_CLEAR_DISPLAY:
          Serial.printf("[DISPLAY] Processing clear command (color: 0x%02X)...\n", msg.clearColor);
          epd_clear(msg.clearColor);
          xSemaphoreTake(displayMutex, portMAX_DELAY);
          displayState.lastOperation = "Clear display";
          xSemaphoreGive(displayMutex);
          break;
          
        case CMD_SLEEP_DISPLAY:
          Serial.println("[DISPLAY] Processing sleep command...");
          epd_send_command(0x02); epd_send_data(0x00);
          epd_send_command(0x07); epd_send_data(0xA5);
          xSemaphoreTake(displayMutex, portMAX_DELAY);
          displayState.lastOperation = "Sleep";
          xSemaphoreGive(displayMutex);
          break;
      }
      
      xSemaphoreTake(displayMutex, portMAX_DELAY);
      displayState.busy = false;
      displayState.lastUpdateTime = millis();
      xSemaphoreGive(displayMutex);
    }
  }
}

// === HTTP HANDLERS (CORE 0) ===
void handleFrameUploadBody() {
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[HTTP] Upload started, filename: %s\n", upload.filename.c_str());
    uploadInProgress = true;
    uploadBytesReceived = 0;
    
    // Make sure we have the shared buffer
    if (!sharedImageBuffer) {
      Serial.println("[HTTP] ERROR: No shared buffer allocated");
      uploadInProgress = false;
      return;
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadBytesReceived + upload.currentSize <= EPD_BUFFER_SIZE) {
      // Lock buffer and write directly to shared memory
      if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(sharedImageBuffer + uploadBytesReceived, upload.buf, upload.currentSize);
        uploadBytesReceived += upload.currentSize;
        xSemaphoreGive(bufferMutex);
      } else {
        Serial.println("[HTTP] WARNING: Could not lock buffer for write");
      }
    } else {
      Serial.printf("[HTTP] ERROR: Upload too large: %d + %d > %d\n", 
                   uploadBytesReceived, upload.currentSize, EPD_BUFFER_SIZE);
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    uploadInProgress = false;
    Serial.printf("[HTTP] Upload complete: %d bytes\n", uploadBytesReceived);
  }
  else if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadInProgress = false;
    uploadBytesReceived = 0;
    Serial.println("[HTTP] Upload aborted");
  }
}

void handleFrameUpload() {
  if (uploadInProgress) {
    server.send(500, "application/json", "{\"error\":\"Upload in progress\"}");
    return;
  }
  
  if (uploadBytesReceived != EPD_BUFFER_SIZE) {
    Serial.printf("[HTTP] ERROR: Incomplete upload: %d != %d\n", uploadBytesReceived, EPD_BUFFER_SIZE);
    uploadBytesReceived = 0;
    server.send(400, "application/json", "{\"error\":\"Upload incomplete\"}");
    return;
  }
  
  xSemaphoreTake(displayMutex, portMAX_DELAY);
  bool displayBusy = displayState.busy;
  xSemaphoreGive(displayMutex);
  
  DisplayMessage msg = {CMD_SHOW_SHARED_IMAGE, 0, true};
  
  if (displayBusy) {
    // Display is busy - replace any queued operations
    xQueueReset(displayQueue);
    Serial.println("[HTTP] Display busy - replacing queued operations with new image");
  }
  
  if (xQueueSend(displayQueue, &msg, 0) == pdTRUE) {
    int queuePosition = uxQueueMessagesWaiting(displayQueue);
    int estimatedWait = displayBusy ? 45 : 0;
    
    Serial.printf("[HTTP] Image queued at position %d (%d bytes)\n", queuePosition, uploadBytesReceived);
    
    DynamicJsonDocument response(200);
    response["status"] = "success";
    response["message"] = displayBusy ? "Image queued (replacing previous)" : "Image queued (immediate)";
    response["queue_position"] = queuePosition;
    response["estimated_wait_seconds"] = estimatedWait;
    response["bytes_received"] = uploadBytesReceived;
    
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(200, "application/json", jsonResponse);
  } else {
    server.send(500, "application/json", "{\"error\":\"Display queue error\"}");
  }
  
  uploadBytesReceived = 0;
}

void handleStatus() {
  xSemaphoreTake(displayMutex, portMAX_DELAY);
  bool initialized = displayState.initialized;
  bool busy = displayState.busy;
  String lastOp = displayState.lastOperation;
  unsigned long lastUpdate = displayState.lastUpdateTime;
  String status = displayState.status;
  xSemaphoreGive(displayMutex);
  
  DynamicJsonDocument doc(600);
  doc["status"] = "running";
  doc["service"] = "epaper_6color_frame_dual_core";
  doc["display_initialized"] = initialized;
  doc["display_busy"] = busy;
  doc["last_operation"] = lastOp;
  doc["last_update_time"] = lastUpdate;
  doc["resolution"] = String(EPD_WIDTH) + "x" + String(EPD_HEIGHT);
  doc["buffer_size"] = EPD_BUFFER_SIZE;
  doc["ip_address"] = WiFi.localIP().toString();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["largest_free_block"] = ESP.getMaxAllocHeap();
  doc["http_core"] = 0;
  doc["display_core"] = 1;
  doc["shared_buffer"] = sharedImageBuffer != nullptr;
  
  String resp;
  serializeJson(doc, resp);
  server.send(200, "application/json", resp);
}

void handleClear() {
  xSemaphoreTake(displayMutex, portMAX_DELAY);
  bool displayReady = displayState.initialized && !displayState.busy;
  xSemaphoreGive(displayMutex);
  
  if (!displayReady) {
    server.send(500, "application/json", "{\"error\":\"Display not ready or busy\"}");
    return;
  }
  
  DisplayMessage msg = {CMD_CLEAR_DISPLAY, EPD_WHITE, false};
  if (xQueueSend(displayQueue, &msg, 0) == pdTRUE) {
    Serial.println("[HTTP] Clear command queued");
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Clear queued\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"Display queue full\"}");
  }
}

void handleTest() {
  xSemaphoreTake(displayMutex, portMAX_DELAY);
  bool displayReady = displayState.initialized && !displayState.busy;
  xSemaphoreGive(displayMutex);
  
  if (!displayReady) {
    server.send(500, "application/json", "{\"error\":\"Display not ready or busy\"}");
    return;
  }
  
  DisplayMessage msg = {CMD_SHOW_TEST_PATTERN, 0, false};
  if (xQueueSend(displayQueue, &msg, 0) == pdTRUE) {
    Serial.println("[HTTP] Test pattern queued");
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Test pattern queued\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"Display queue full\"}");
  }
}

// === HTTP TASK (CORE 0) ===
void httpTask(void* parameter) {
  Serial.println("[HTTP] HTTP task starting on core " + String(xPortGetCoreID()));
  
  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.printf("[HTTP] WiFi connected: %s\n", WiFi.localIP().toString().c_str());
  
  // Setup HTTP routes
  server.on("/api/display/frame", HTTP_POST, handleFrameUpload, handleFrameUploadBody);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/clear", HTTP_GET, handleClear);
  server.on("/api/test", HTTP_GET, handleTest);
  
  server.begin();
  Serial.println("[HTTP] HTTP server running on port 80");
  
  // HTTP server loop - always responsive!
  while (true) {
    server.handleClient();
    delay(1);
  }
}

// === MAIN SETUP ===
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("🚀 Starting Memory-Efficient Dual-Core E-Paper System");
  Serial.printf("💾 Total heap: %d bytes\n", ESP.getHeapSize());
  Serial.printf("💾 Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("💾 Largest free block: %d bytes\n", ESP.getMaxAllocHeap());
  
  // Try to allocate shared buffer in PSRAM first, then regular heap
  sharedImageBuffer = (uint8_t*)ps_malloc(EPD_BUFFER_SIZE);
  if (sharedImageBuffer) {
    Serial.printf("✅ Allocated %d bytes in PSRAM for shared buffer\n", EPD_BUFFER_SIZE);
  } else {
    sharedImageBuffer = (uint8_t*)malloc(EPD_BUFFER_SIZE);
    if (sharedImageBuffer) {
      Serial.printf("✅ Allocated %d bytes in heap for shared buffer\n", EPD_BUFFER_SIZE);
    } else {
      Serial.println("❌ Failed to allocate shared image buffer");
      return;
    }
  }
  
  Serial.printf("💾 Free heap after allocation: %d bytes\n", ESP.getFreeHeap());
  
  // Create synchronization primitives
  displayQueue = xQueueCreate(3, sizeof(DisplayMessage));
  displayMutex = xSemaphoreCreateMutex();
  bufferMutex = xSemaphoreCreateMutex();
  
  if (!displayQueue || !displayMutex || !bufferMutex) {
    Serial.println("❌ Failed to create synchronization primitives");
    return;
  }
  
  // Initialize shared state
  displayState.initialized = false;
  displayState.busy = false;
  displayState.lastOperation = "None";
  displayState.lastUpdateTime = 0;
  displayState.status = "Starting...";
  
  // Create tasks on specific cores
  BaseType_t httpResult = xTaskCreatePinnedToCore(
    httpTask,           // Function
    "HTTP_Task",        // Name
    8192,              // Stack size
    NULL,              // Parameters
    1,                 // Priority
    &httpTaskHandle,   // Handle
    0                  // Core 0 (Protocol core)
  );
  
  BaseType_t displayResult = xTaskCreatePinnedToCore(
    displayTask,        // Function
    "Display_Task",     // Name
    8192,              // Stack size
    NULL,              // Parameters
    2,                 // Higher priority
    &displayTaskHandle, // Handle
    1                  // Core 1 (Application core)
  );
  
  if (httpResult == pdPASS && displayResult == pdPASS) {
    Serial.println("✅ Tasks created - HTTP on Core 0, Display on Core 1");
  } else {
    Serial.println("❌ Failed to create tasks");
  }
}

void loop() {
  // Main loop can be empty - tasks handle everything!
  // Print memory stats periodically for debugging
  static unsigned long lastMemCheck = 0;
  if (millis() - lastMemCheck > 10000) {  // Every 10 seconds
    Serial.printf("[MAIN] Free heap: %d bytes, Largest block: %d bytes\n", 
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    lastMemCheck = millis();
  }
  delay(1000);
}