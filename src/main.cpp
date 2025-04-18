#include <secrets.h>
#include <version.h>

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

WebServer server(80);

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  Serial.printf("ESP-S2 ready. Connected to %s\n", ssid);
  Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("FW version: %s, %s %s\n", FW_VERSION, __DATE__, __TIME__);


  server.on("/update", HTTP_POST,
    []() {
      // OTA - onUploadEnd
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
      ESP.restart();
    },
    []() {  // OTA - onUpload
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Update.begin(UPDATE_SIZE_UNKNOWN);
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        Update.write(upload.buf, upload.currentSize);
      } else if (upload.status == UPLOAD_FILE_END) {
        Update.end(true);
      }
    }
  );

  server.begin();
}

void loop() {
  server.handleClient();
}
