#include <Wire.h>
#include <VL53L0X.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>

const char* wifiSSID = "Orange-6432-2.4GHz";
const char* wifiPassword = "hq3m6inREYi";
const char* mqttServer = "192.168.1.102";
const int mqttPort = 1883;
const char* mqttTopic = "vehicle/counter";
const char* nodeID = "node2";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Detection parameters
#define DETECTION_THRESHOLD_MAX 2000 // Max detection distance in mm (2 m)
#define DETECTION_THRESHOLD_MIN 200  // Min detection distance in mm (ignore close noise)
#define MIN_DETECTION_TIME 50        // Min detection time in ms to validate a vehicle
#define DEBOUNCE_TIME 200            // Debounce time in ms to prevent double counting
#define MOVING_AVERAGE_SIZE 8        // Number of readings for moving average
#define NO_TARGET_DISTANCE 3000      // Distance for no target (mm)
#define DETECTION_TIMEOUT 1000       // Timeout for DETECTING state (ms)

// Sensor object
VL53L0X sensor;

// Global variables
unsigned long lastDetectionTime = 0;   // Time of last detection
unsigned long vehicleCount = 0;        // Vehicle counter
float distanceBuffer[MOVING_AVERAGE_SIZE]; // Buffer for moving average
int bufferIndex = 0;
bool bufferFilled = false;

// Hardcoded offset after manual calibration (in mm)
const int offsetCalibration = -26;

enum DetectionState {
  WAITING,
  DETECTING,
  COUNTED
};

DetectionState state = WAITING;
float lastAvgDistance = NO_TARGET_DISTANCE;

void connectWiFi() {
  Serial.print(F("Wi-Fi: "));
  WiFi.begin(wifiSSID, wifiPassword);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? F(" Connected") : F(" Failed"));
}

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    mqttClient.setServer(mqttServer, mqttPort);
    //mqttClient.setWill("vehicle/status/node2", "offline", true, 1);  // LWT setup
    Serial.print(F("MQTT: "));
    if (mqttClient.connect("ESP32_VL53L0X_node2")) {
      Serial.println(F("Connected"));
      mqttClient.publish("vehicle/status/node2", "online", true);   // Publish online status
    } else {
      Serial.println(F("Failed"));
    }
  }
}

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(100); // Ensure Serial is ready

  connectWiFi();

  // Initialize I2C
  Wire.begin();
  Wire.setClock(400000); // I2C at 400 kHz (fast mode)

  // Initialize sensor
  sensor.setTimeout(500);
  if (!sensor.init()) {
    Serial.println("Error: Failed to initialize VL53L0X");
    while (1);
  }

  // Configure for high-speed mode (supports 40 km/h)
  sensor.setSignalRateLimit(0.25);
  sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 12);
  sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 8);
  sensor.setMeasurementTimingBudget(20000); // 20 ms for high-speed mode

  connectMQTT();


  // Initialize distance buffer
  for (int i = 0; i < MOVING_AVERAGE_SIZE; i++) {
    distanceBuffer[i] = NO_TARGET_DISTANCE;
  }

  // Start continuous ranging
  sensor.startContinuous(20); // 20 ms interval
  Serial.println("Sensor ready");
}

void loop() {
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) connectMQTT();
  mqttClient.loop();
   
  // Read distance
  uint16_t rawDistance = sensor.readRangeContinuousMillimeters();
  if (sensor.timeoutOccurred()) {
    Serial.println("Error: Read timeout");
    return;
  }

  // Apply hardcoded offset to raw distance
  int16_t distance = rawDistance + offsetCalibration;  // Using the hardcoded offset
  if (distance < 0) distance = 0;
  if (rawDistance > 8000) distance = NO_TARGET_DISTANCE; // No target detected

  // Update moving average
  if (distance > DETECTION_THRESHOLD_MIN && distance < 4000) {
    distanceBuffer[bufferIndex] = distance;
  } else {
    distanceBuffer[bufferIndex] = NO_TARGET_DISTANCE; // Use no-target distance
  }
  bufferIndex = (bufferIndex + 1) % MOVING_AVERAGE_SIZE;
  if (bufferIndex == 0) bufferFilled = true;

  // Calculate moving average
  float avgDistance = 0;
  for (int i = 0; i < MOVING_AVERAGE_SIZE; i++) {
    avgDistance += distanceBuffer[i];
  }
  avgDistance /= MOVING_AVERAGE_SIZE;

  // Debug output
  Serial.print("Raw: ");
  Serial.print(rawDistance);
  Serial.print(" mm, Calibrated: ");
  Serial.print(distance);
  Serial.print(" mm, Avg: ");
  Serial.print(avgDistance);
  Serial.print(" mm, State: ");
  switch (state) {
    case WAITING: Serial.print("WAITING"); break;
    case DETECTING: Serial.print("DETECTING"); break;
    case COUNTED: Serial.print("COUNTED"); break;
  }
  Serial.print(", Count: ");
  Serial.println(vehicleCount);

  // State machine
  if (bufferFilled) {
    unsigned long currentTime = millis();

    switch (state) {
      case WAITING:
        // Only enter DETECTING if no object is present (or previous object has left)
        if (avgDistance < DETECTION_THRESHOLD_MAX && avgDistance > DETECTION_THRESHOLD_MIN &&
            lastAvgDistance >= DETECTION_THRESHOLD_MAX) {
          state = DETECTING;
          lastDetectionTime = currentTime;
          Serial.println("Object detected!");
        }
        break;

      case DETECTING:
        if (avgDistance >= DETECTION_THRESHOLD_MAX) {
          // Object has left; count if minimum time is met
          if (currentTime - lastDetectionTime >= MIN_DETECTION_TIME) {
            vehicleCount++;
            Serial.print("Vehicle detected! Count: ");
            Serial.println(vehicleCount);
            state = COUNTED;
            // Publish MQTT message
            if (mqttClient.connected()) {
              char message[100];
              snprintf(message, sizeof(message), 
                "{\"node\":\"%s\",\"count\":%lu,\"distance\":%.1f,\"timestamp\":%lu}",
                nodeID, vehicleCount, lastAvgDistance, currentTime);
              mqttClient.publish(mqttTopic, message);
              Serial.println("MQTT message published");
            } else {
              Serial.println("MQTT not connected, message not published");
            }
          } else {
            state = WAITING;
          }
        } else if (currentTime - lastDetectionTime > DETECTION_TIMEOUT) {
          // Timeout for stationary object; reset to WAITING without counting
          Serial.println("Stationary object detected, resetting to WAITING");
          state = WAITING;
        }
        break;

      case COUNTED:
        if (currentTime - lastDetectionTime >= DEBOUNCE_TIME) {
          state = WAITING;
        }
        break;
    }

    // Update last average distance for next iteration
    lastAvgDistance = avgDistance;
  }
}
