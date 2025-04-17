#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>

// ---- CONFIG ----
#define BUTTON_1 8
#define BUTTON_2 10
#define BUTTON_3 13
#define BUTTON_4 14

#define LED_PIN 18
#define NUM_LEDS 1

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define I2C_SDA 33
#define I2C_SCL 34

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

const char* ssid = "ESP32-TestAP";
const char* password = "12345678";

// ---- STATE ----
volatile bool triggered[4] = {false, false, false, false};

// ---- INTERRUPTS ----
void IRAM_ATTR handleBtn1() { triggered[0] = true; }
void IRAM_ATTR handleBtn2() { triggered[1] = true; }
void IRAM_ATTR handleBtn3() { triggered[2] = true; }
void IRAM_ATTR handleBtn4() { triggered[3] = true; }

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Setup GPIO inputs with pullups
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(BUTTON_3, INPUT_PULLUP);
  pinMode(BUTTON_4, INPUT_PULLUP);

  attachInterrupt(BUTTON_1, handleBtn1, FALLING);
  attachInterrupt(BUTTON_2, handleBtn2, FALLING);
  attachInterrupt(BUTTON_3, handleBtn3, FALLING);
  attachInterrupt(BUTTON_4, handleBtn4, FALLING);

  // Setup NeoPixel
  strip.begin();
  strip.setBrightness(20);
  strip.show(); // all off

  // Setup I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Setup OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 not found");
  } else {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("Display OK");
    display.display();
  }

  Serial.println("Setup done");
}

void loop() {
  for (int i = 0; i < 4; i++) {
    if (triggered[i]) {
      Serial.printf("Button %d pressed\n", i + 1);
      triggered[i] = false;

      // LED color feedback
      strip.setPixelColor(0, strip.Color(50 * i, 255 - 50 * i, 100));
      strip.show();

      // Display update
      display.clearDisplay();
      display.setCursor(0, 0);
      display.printf("BTN %d", i + 1);
      display.display();
    }
  }
}
