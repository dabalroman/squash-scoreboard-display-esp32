#include <secrets.h>
#include <version.h>

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiServer.h>
#include <Update.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <deque>

const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

WebServer OTAServer(80);
WiFiServer telnetServer(23);
WiFiClient telnetClient;

// OLED SSD1106
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET (-1)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// 433MHz Receiver Pins
constexpr uint8_t RX_PIN_D0 = 8;
constexpr uint8_t RX_PIN_D1 = 10;
constexpr uint8_t RX_PIN_D2 = 13;
constexpr uint8_t RX_PIN_D3 = 14;

// Neopixel
#define LED_PIN 18
#define NUM_LEDS 86
Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

uint32_t colors[] = { pixels.Color(5,1,0), pixels.Color(3,3,0), pixels.Color(0,1,5) };
uint8_t colorIndex = 0;
unsigned long lastLedUpdate = 0;

std::deque<String> logBuffer;
const size_t MAX_LOGS = 20;

void telnetPrintLn(const char* format, ...) {
  char buf[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  if (telnetClient && telnetClient.connected()) {
    telnetClient.println(buf);
  } else {
    if (logBuffer.size() >= MAX_LOGS) {
      logBuffer.pop_front();
    }
    logBuffer.push_back(String(buf));
  }
}

void flushLogBuffer() {
  while (!logBuffer.empty()) {
    telnetClient.println(logBuffer.front());
    logBuffer.pop_front();
  }
}

void setup() {
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  OTAServer.on("/update", HTTP_POST,
    []() {
      // OTA - onUploadEnd
      OTAServer.sendHeader("Connection", "close");
      OTAServer.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
      ESP.restart();
    },
    []() {  // OTA - onUpload
      HTTPUpload& upload = OTAServer.upload();
      if (upload.status == UPLOAD_FILE_START) {
        if (telnetClient && telnetClient.connected()) {
          flushLogBuffer();
          telnetPrintLn(">>>>   OTA update started   <<<<");
          telnetClient.stop();
          telnetServer.close();
        }

        Update.begin(UPDATE_SIZE_UNKNOWN);
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        Update.write(upload.buf, upload.currentSize);
      } else if (upload.status == UPLOAD_FILE_END) {
        Update.end(true);
      }
    }
  );

  telnetServer.begin();
  telnetServer.setNoDelay(true);

  telnetPrintLn("ESP-S2 ready. FW version: %s, %s %s\n", FW_VERSION, __DATE__, __TIME__);
  telnetPrintLn("Connected to %s\n", ssid);
  telnetPrintLn("IP: %s\n", WiFi.localIP().toString().c_str());

  OTAServer.begin();

  Wire.begin(33, 34);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    telnetPrintLn("SSD1106 allocation failed!");
  }
  display.setRotation(2);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(WiFi.localIP().toString().c_str());
  display.display();

  pinMode(RX_PIN_D0, INPUT);
  pinMode(RX_PIN_D1, INPUT);
  pinMode(RX_PIN_D2, INPUT);
  pinMode(RX_PIN_D3, INPUT);

  pixels.begin();
  pixels.show();
}

void loop() {
  OTAServer.handleClient();

  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      telnetClient = telnetServer.available();
      flushLogBuffer();
    } else {
      WiFiClient newClient = telnetServer.available();
      newClient.stop();
    }
  }

  if (digitalRead(RX_PIN_D0)) telnetPrintLn("D0 pressed");
  if (digitalRead(RX_PIN_D1)) telnetPrintLn("D1 pressed");
  if (digitalRead(RX_PIN_D2)) telnetPrintLn("D2 pressed");
  if (digitalRead(RX_PIN_D3)) telnetPrintLn("D3 pressed");

  if (millis() - lastLedUpdate > 1000) {
    pixels.fill(colors[colorIndex]);
    pixels.show();
    colorIndex = (colorIndex + 1) % 3;
    lastLedUpdate = millis();
  }
}
