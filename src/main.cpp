#include <secrets.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

void setup() {
    Serial.begin(115200);
    delay(1000);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.println("WiFi connected!");
    Serial.println(WiFi.localIP());

    ArduinoOTA.begin();
}

void loop() {
    ArduinoOTA.handle();
}