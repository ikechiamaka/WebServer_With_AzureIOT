#include <ESP8266WiFi.h>
#include "src/iotc/common/string_buffer.h"
#include "src/iotc/iotc.h"
#include "DHT.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#define DHTPIN D5

#define DHTTYPE DHT22
#define RelayPin D3

ESP8266WebServer server(80);


// Initialize the I2C LCD (20x4)
LiquidCrystal_I2C lcd(0x27, 20, 4);

// DS18B20 setup
#define ONE_WIRE_BUS D7  // Change to the pin connected to the DS18B20 sensor
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);


bool relayStatus = LOW;
bool autoControl = true;
const char* SCOPE_ID = "0ne009A5A96";
const char* DEVICE_ID = "SensorsData";
const char* DEVICE_KEY = "rX1RC202hHa3Z5FalWsY1LoqmTXwwykBneDrA/+nQtg=";

DHT dht(DHTPIN, DHTTYPE);

void on_event(IOTContext ctx, IOTCallbackInfo* callbackInfo);
#include "src/connection.h"

void on_event(IOTContext ctx, IOTCallbackInfo* callbackInfo) {
  // ConnectionStatus
  if (strcmp(callbackInfo->eventName, "ConnectionStatus") == 0) {
    LOG_VERBOSE("Is connected ? %s (%d)",
                callbackInfo->statusCode == IOTC_CONNECTION_OK ? "YES" : "NO",
                callbackInfo->statusCode);
    isConnected = callbackInfo->statusCode == IOTC_CONNECTION_OK;
    return;
  }

  // payload buffer doesn't have a null ending.
  // add null ending in another buffer before print
  AzureIOT::StringBuffer buffer;
  if (callbackInfo->payloadLength > 0) {
    buffer.initialize(callbackInfo->payload, callbackInfo->payloadLength);
  }

  LOG_VERBOSE("- [%s] event was received. Payload => %s\n",
              callbackInfo->eventName, buffer.getLength() ? *buffer : "EMPTY");

  if (strcmp(callbackInfo->eventName, "Command") == 0) {
    LOG_VERBOSE("- Command name was => %s\r\n", callbackInfo->tag);
  }
}

void setup() {

  // Initialize the LCD
  lcd.init();
  lcd.backlight();

  // Start the DS18B20 sensor
  DS18B20.begin();

  Serial.begin(115200);
  pinMode(RelayPin, OUTPUT);
    WiFiManager wifiManager;

  // Uncomment this line if you want to reset the WiFi settings when the device starts
  wifiManager.resetSettings();

  // Set the AP mode timeout (in seconds)
  wifiManager.setConfigPortalTimeout(180);

  // Set a custom AP name
  wifiManager.autoConnect("ESP8266_HOTSPOT");

  // Check if connected to WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi. Restarting...");
    delay(3000);
    ESP.reset();
    delay(5000);
  }
  // connect_wifi(WIFI_SSID, WIFI_PASSWORD);
  connect_client(SCOPE_ID, DEVICE_ID, DEVICE_KEY);

  Serial.println("");
  Serial.println("WiFi connected..!");
  Serial.print("Got IP: ");
  Serial.println(WiFi.localIP());
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi connected..!");
  lcd.setCursor(0, 1);
  lcd.print("Web Server IP:");
  lcd.setCursor(0, 2);
  lcd.print(WiFi.localIP());
  delay(4000);
  server.on("/", handle_OnConnect);
  server.on("/relayon", handle_relayon);
  server.on("/relayoff", handle_relayoff);

  server.on("/autocontrol", handle_autocontrol);
  server.onNotFound(handle_NotFound);

  server.begin();
  Serial.println("HTTP Server Started");

  if (context != NULL) {
    lastTick = 0;  // set timer in the past to enable first telemetry a.s.a.p
  }
  dht.begin();
}

void loop() {
  server.handleClient();
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  // Check if readings are valid and turn on the relay if the temperature is above 32°C
  if (autoControl && !isnan(t) && !isnan(h) && t > 32) {
    relayStatus = HIGH;
  } else if (autoControl) {
    relayStatus = LOW;
  }

  if (relayStatus) {
    digitalWrite(RelayPin, HIGH);
  } else {
    digitalWrite(RelayPin, LOW);
  }
  // Read temperature from the DS18B20 sensor
  DS18B20.requestTemperatures();
  float tempDS18B20 = DS18B20.getTempCByIndex(0);

  // Get the date and time from the DS3231 RTC
  //DateTime now = rtc.now();

  // Update the LCD
  updateLCD(t, h, relayStatus, tempDS18B20);

  delay(1000);  // Update the LCD every second

  if (isConnected) {

    unsigned long ms = millis();
    if (ms - lastTick > 10000) {  // send telemetry every 10 seconds
      char msg[64] = { 0 };
      int pos = 0, errorCode = 0;

      lastTick = ms;
      if (loopId++ % 2 == 0) {  // send telemetry
        pos = snprintf(msg, sizeof(msg) - 1, "{\"Temperature\": %f}",
                       t);
        errorCode = iotc_send_telemetry(context, msg, pos);

        pos = snprintf(msg, sizeof(msg) - 1, "{\"Humidity\":%f}",
                       h);
        errorCode = iotc_send_telemetry(context, msg, pos);

        pos = snprintf(msg, sizeof(msg) - 1, "{\"DS18B20Temperature\":%f}",
                       tempDS18B20);
        errorCode = iotc_send_telemetry(context, msg, pos);
      } else {  // send property
      }

      msg[pos] = 0;

      if (errorCode != 0) {
        LOG_ERROR("Sending message has failed with error code %d", errorCode);
      }
    }

    iotc_do_work(context);  // do background work for iotc
  } else {
    iotc_free_context(context);
    context = NULL;
    connect_client(SCOPE_ID, DEVICE_ID, DEVICE_KEY);
  }
}

void handle_relayon() {
  relayStatus = HIGH;
  autoControl = false;  // Disable automatic control
  Serial.println("Relay: ON");
  server.send(200, "text/html", updateWebpage(relayStatus));
}

void handle_relayoff() {
  relayStatus = LOW;
  autoControl = false;  // Disable automatic control
  Serial.println("Relay: OFF");
  server.send(200, "text/html", updateWebpage(relayStatus));
}

void handle_OnConnect() {
  relayStatus = LOW;
  Serial.println("Relay: OFF");
  server.send(200, "text/html", updateWebpage(relayStatus));
}
void handle_autocontrol() {
  autoControl = true;  // Enable automatic control
  Serial.println("Automatic control enabled");
  server.send(200, "text/html", updateWebpage(relayStatus));
}


void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}



String updateWebpage(uint8_t relayStatus) {
  // Read temperature and humidity from the DHT22 sensor
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  // Check if readings are valid
  if (isnan(t) || isnan(h)) {
    Serial.println("Failed to read from DHT sensor!");
    return "Failed to read from DHT sensor!";
  }
  // Read temperature from the DS18B20 sensor
  DS18B20.requestTemperatures();
  float tempDS18B20 = DS18B20.getTempCByIndex(0);


  String ptr = "<!DOCTYPE html> <html>\n";

  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>LED Control</title>\n";
  ptr += "<style>html {font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr += ".button {display: block;width: 80px;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr += ".button-on {background-color: #3498db;}\n";
  ptr += ".button-on:active {background-color: #3498db;}\n";
  ptr += ".button-off {background-color: #34495e;}\n";
  ptr += ".button-off:active {background-color: #2c3e50;}\n";
  ptr += "p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "<h1>Web Server</h1>\n";
  // Add temperature and humidity values to the webpage
  ptr += "<h3>Temperature: " + String(t) + " °C</h3>\n";
  ptr += "<h3>Humidity: " + String(h) + " %</h3>\n";
  ptr += "<h3>Temperature (DS18B20): " + String(tempDS18B20) + " °C</h3>\n";
  if (relayStatus) {
    ptr += "<p>Relay: ON</p><a class=\"button button-off\" href=\"/relayoff\">OFF</a>\n";
  } else {
    ptr += "<p>Relay: OFF</p><a class=\"button button-on\" href=\"/relayon\">ON</a>\n";
  }

  // Add automatic control button
  ptr += "<p><a class=\"button\" href=\"/autocontrol\">Auto Control</a></p>\n";

  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
}

void updateLCD(float dhtTemp, float dhtHumidity, uint8_t relayStatus, float ds18b20Temp) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DHT T: ");
  lcd.print(dhtTemp);
  lcd.print(" C");
  lcd.setCursor(0, 1);
  lcd.print("DHT H: ");
  lcd.print(dhtHumidity);
  lcd.print(" %");
  lcd.setCursor(0, 2);
  lcd.print("DS18 T: ");
  lcd.print(ds18b20Temp);
  lcd.print(" C");
  lcd.setCursor(0, 3);
  lcd.print("Relay: ");
  lcd.print(relayStatus ? "ON " : "OFF");
  lcd.print(" ");
}