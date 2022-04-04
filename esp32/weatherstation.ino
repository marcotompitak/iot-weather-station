// Include AWS IoT Certificates
#include "aws_iot_certs.h"

// Set up sensor definitions
#include "DHT.h"
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Defining our AWS IoT Configuration
#define DEVICE_NAME "weatherstation"
#define AWS_IOT_ENDPOINT "<CONFIGURE>"
#define AWS_IOT_TOPIC "<CONFIGURE>"
#define AWS_MAX_RECONNECT_TRIES 10

// Connectivity setup
#include "WiFi.h"
#include "time.h"
#include <WiFiClientSecure.h>
#include <MQTTClient.h>                  // Joel Gaehwiler MQTT package
WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(512);
const char* ntpServer = "pool.ntp.org";
#define WIFI_NETWORK "<CONFIGURE>"
#define WIFI_PASS "<CONFIGURE>"
#define WIFI_TIMEOUT_MS 20000
unsigned long message_interval = 300000; // in milliseconds
unsigned long keepalive_interval = 1200; // in seconds; 1200 is the default and maximum for AWS IoT Core

// JSON setup
#include <ArduinoJson.h>

// Wifi connection wrapper
void connectToWifi() {
  WiFi.mode(WIFI_STA); // Station mode for existing network
  WiFi.begin(WIFI_NETWORK, WIFI_PASS); // Initiate connection
  unsigned long startAttemptTime = millis(); // Track start time for time-out
  Serial.print("Connecting to Wifi");

  // Check if connection is established within the time-out window
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
    Serial.print(".");
    delay(100); // Attempt limiter
  }
  Serial.println("");

  // Check the connection status
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to wifi");
    esp_deep_sleep_start();
  } else {
    Serial.println("Wifi connection established");
    Serial.println(WiFi.localIP());
  }
}

// AWS connection wrapper
void connectToAWSIoT() {

  // Load certificates
  net.setCACert(AWS_ROOT_CA_CERT);
  net.setCertificate(AWS_CLIENT_CERT);
  net.setPrivateKey(AWS_PRIVATE_KEY);

  client.setKeepAlive(keepalive_interval);

  // Attempt to connect
  Serial.print("Connecting to AWS IoT Core");
  client.begin(AWS_IOT_ENDPOINT, 8883, net);
  int retries = 0;
  while (!client.connect(DEVICE_NAME) && retries < AWS_MAX_RECONNECT_TRIES) {
    Serial.print(".");
    delay(100);
    retries++;
  }
  Serial.println("");

  // Check connection status
  if (!client.connected()) {
    Serial.println("Connection failed.");
    return;
  } else {
    Serial.println("Connected.");
  }
}

// Produce a timestamp
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("WARNING: Unable to get local time, timestamp set to 0");
    return (0);
  }
  time(&now);
  return now;
}

void sendSensorReading() {
  StaticJsonDocument<200> doc;

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println("Sensor read failed, aborting until next iteration");
    return;
  }

  float heat_index = dht.computeHeatIndex(t, h, false);

  unsigned long timestamp = getTime();

  doc["timestamp"] = timestamp;
  doc["temperature"] = t;
  doc["humidity"] = h;
  doc["heat_index"] = heat_index;

  Serial.print(timestamp);
  Serial.print(" : ");
  Serial.print("Sending T = ");
  Serial.print(t);
  Serial.print(" C; H = ");
  Serial.print(h);
  Serial.print(" %; heat_index = ");
  Serial.print(heat_index);
  Serial.println(" C.");

  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  client.publish(AWS_IOT_TOPIC, jsonBuffer);
}

void setup() {
  Serial.begin(115200);

  // Set up connection
  connectToWifi();
  delay(2000);
  connectToAWSIoT();

  // Connect to NTP server to get time info
  Serial.println("Getting time info from NTP");
  configTime(0, 0, ntpServer);
  Serial.print("Current timestamp is ");
  Serial.println(getTime());

  // Start sensor reader
  Serial.println("Initiating sensor reading");
  dht.begin();
}

void loop() {
  // Check wifi connection and reconnect if needed
  if ((WiFi.status() != WL_CONNECTED)) {
    WiFi.disconnect();
    Serial.println("Connection lost, attempting to reconnect");
    connectToWifi();
    delay(2000);
    connectToAWSIoT();
  }

  // Perform measurement and send to AWS
  sendSensorReading();

  delay(message_interval);
}
