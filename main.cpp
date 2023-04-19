#include <DHT.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

//ESP8266 Serial Number
String espSeriesNumber = "ESP8266 MULTIPLE Record";

// WiFi Settings
const char * ssid = "WIFI_SSID";
const char * pass = "WIFI_PASS";

// Host Settings
const char * server = "example-host.com";

// Set DHT22 Input Pin
#define DHTPIN 0

// RTC
#define RTCMEMORYSTART 2
#define COLLECT 17
#define SEND 66
#define RECORDS 15
#define SLEEPTIME 6e6

struct rtcManagementStruct {
  int magicNumber;
  int valueCounter;
};

// Temp & Hum Storage
struct rtcValues {
  float t;
  float h;
};

rtcManagementStruct rtcManagement;
rtcValues readSensor;

int buckets;

DHT dht(DHTPIN, DHT22);
WiFiClient client;

void setup() {
  Serial.begin(115200);
  delay(1000);
  dht.begin();

  rst_info *rsti;
  rsti = ESP.getResetInfoPtr();

  switch(rsti->reason) {
    case 5:
      Serial.println(" from RTC-RESET (ResetInfo.reason = 5)");
      break;
    case 6:
      Serial.println(" from POWER-UP (ResetInfo.reason = 6)");
      rtcManagement.magicNumber = COLLECT;
      rtcManagement.valueCounter = 0;
      break;
  }

  buckets = (sizeof(rtcValues) / 4);
  if (buckets == 0) buckets = 1;
  Serial.print("Buckets ");
  Serial.println(buckets);

  ESP.rtcUserMemoryRead(0, (uint32_t*) &rtcManagement, sizeof(rtcManagement));
  Serial.print("Magic Number ");
  Serial.println(rtcManagement.magicNumber);
  Serial.print("Value Counter ");
  Serial.println(rtcManagement.valueCounter);

  if (rtcManagement.magicNumber != COLLECT && rtcManagement.magicNumber != SEND ) {
    rtcManagement.magicNumber = COLLECT;
    rtcManagement.valueCounter = 0;
    ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcManagement, sizeof(rtcManagement));
    Serial.println("Initial values set");
    ESP.deepSleep(10, WAKE_RF_DISABLED);
  }
  if (rtcManagement.magicNumber == COLLECT) {   
    // Read sensor and store
    if (rtcManagement.valueCounter <  RECORDS) {
      Serial.println("SENSOR READS");
      readSensor.h = dht.readHumidity();
      readSensor.t =  dht.readTemperature();

      int rtcPos = RTCMEMORYSTART + rtcManagement.valueCounter * buckets;
      ESP.rtcUserMemoryWrite(rtcPos, (uint32_t*) &readSensor, sizeof(readSensor));

      Serial.print("Position: ");
      Serial.print(rtcPos);
      Serial.print(", Temp: ");
      Serial.print(readSensor.t);
      Serial.print(", Hum: ");
      Serial.print(readSensor.h);
      rtcManagement.valueCounter++;
      Serial.print(", ValCounter: ");
      Serial.println(rtcManagement.valueCounter);
      ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcManagement, sizeof(rtcManagement));

      Serial.println("before sleep W/O RF");
      ESP.deepSleep(SLEEPTIME, WAKE_NO_RFCAL);
    }
    else {    
      // Set initial values
      rtcManagement.magicNumber = SEND;
      rtcManagement.valueCounter = 0;
      ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcManagement, sizeof(rtcManagement));
      Serial.println("before sleep w RF");
      ESP.deepSleep(10, WAKE_RFCAL);
    }
  }
  else {  
    // Send to Cloud
    WiFi.mode(WIFI_STA);
    Serial.print("Connecting to: ");
    Serial.println(ssid);
    WiFi.begin(ssid, pass);

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("START SENDING VALUES");

    const size_t capacity = JSON_ARRAY_SIZE(10) + JSON_OBJECT_SIZE(1) + 10*JSON_OBJECT_SIZE(3);
    DynamicJsonDocument requestBody(capacity);
    JsonArray records = requestBody.createNestedArray("records");

    for (int i = 0; i < RECORDS; i++) {
      int rtcPos = RTCMEMORYSTART + i * buckets;
      ESP.rtcUserMemoryRead(rtcPos, (uint32_t*) &readSensor, sizeof(readSensor));

      Serial.print("Send Counter: ");
      Serial.println(i);
      Serial.print(", Position: ");
      Serial.println(rtcPos);
      Serial.print(", Temp: ");
      Serial.println(readSensor.t);
      Serial.print(", Hum: ");
      Serial.println(readSensor.h);
      Serial.print(", espSN: ");
      Serial.println(espSeriesNumber);

      JsonObject record = records.createNestedObject();
      record["temp"]= readSensor.t;
      record["hum"] = readSensor.h;
      record["espSN"] = espSeriesNumber;
    }
    Serial.println("REQUEST BODY: ");
    serializeJson(requestBody, Serial);

    if (client.connect(server, 80)) {
      client.println("POST /api/example-endpoint HTTP/1.1");
      client.println("Host: example-host.com");
      client.println("Connection: close");
      client.print("Content-Length: ");
      client.println(measureJson(requestBody));
      client.println("Content-Type: application/json");
      client.println();
      
      serializeJson(requestBody, client);
      Serial.println("Data send");
    }
    
    // Print response
    while (client.available()) {
      String line = client.readStringUntil('\r');
      Serial.print(line);
    }
    client.stop();

    rtcManagement.magicNumber = COLLECT;
    rtcManagement.valueCounter = 0;

    ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcManagement, sizeof(rtcManagement));
    ESP.deepSleep(SLEEPTIME, WAKE_NO_RFCAL);
  }
}

void loop() {
}