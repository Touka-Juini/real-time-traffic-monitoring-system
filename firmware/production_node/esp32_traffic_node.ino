#include <Wire.h>
#include <VL53L0X.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>

// WiFi & MQTT config
const char* wifiSSID = "Orange-6432-2.4GHz";
const char* wifiPassword = "hq3m6inREYi";
const char* mqttServer = "192.168.1.102";
const int mqttPort = 1883;
const char* mqttTopic = "vehicle/counter";
const char* nodeID = "node1";
const char* mqttStatusTopic = "vehicle/status";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// VL53L0X config
#define DETECTION_THRESHOLD_MAX 2000
#define DETECTION_THRESHOLD_MIN 150
#define DETECTION_VALID_TIME 15       // Minimum time in ms for VL53L0X
#define DETECTION_VALID_TIME_ULTRASONIC 50 // Minimum time in ms for ultrasonic
#define DETECTION_TIMEOUT 1000        // Timeout in ms to reset to WAITING
#define INVALID_DISTANCE 8190         // VL53L0X invalid distance value
#define INVALID_READINGS_THRESHOLD 3  // Require 3 consecutive invalid readings
#define COUNTED_TIMEOUT 5000          // 5 seconds
#define SHORT_VEHICLE_TIME 500        // Detection time threshold for short vehicles (ms)
#define LONG_VEHICLE_TIME 1500        // Detection time threshold for long vehicles (ms)
#define SHORT_VEHICLE_LENGTH 2000     // Length for short vehicles (mm, e.g., motorcycles)
#define MEDIUM_VEHICLE_LENGTH 4000    // Length for medium vehicles (mm, e.g., cars)
#define LONG_VEHICLE_LENGTH 10000     // Length for long vehicles (mm, e.g., trucks)
// Note: For side-of-road placement, calibrate SHORT_VEHICLE_TIME and LONG_VEHICLE_TIME based on sensor distance from road and expected vehicle speeds.

// Ultrasonic sensor pins (HC-SR04)
#define TRIG_PIN 5  // Pin for TRIG
#define ECHO_PIN 18 // Pin for ECHO

unsigned long lastCountedTime = 0;
int invalidReadingCount = 0;
unsigned long lastDetectionTime = 0;
unsigned long vehicleCount = 0;
unsigned long detectionStartTime = 0;  // Time when vehicle enters DETECTING state
float vehicleSpeed = 0.0;  // Speed in km/h
const char* vehicleType = "unknown";  // Vehicle type (short, medium, long)

// Variables for DETECTING state distance consistency check
static float lastDistances[3] = {0, 0, 0};
static int distanceIndex = 0;

enum DetectionState {
  WAITING,
  DETECTING,
  COUNTED
};

DetectionState state = WAITING;

VL53L0X sensor;

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
    Serial.print(F("MQTT: "));
    if (mqttClient.connect("ESP32_VL53L0X_node1", mqttStatusTopic, 0, true, "offline")) {
      Serial.println("Connected");
      mqttClient.publish(mqttStatusTopic, "online", true);
    } else {
      Serial.println("Failed");
    }
  }
}

// Function to read distance from ultrasonic sensor (HC-SR04)
float readUltrasonicDistance() {
  const int NUM_READINGS = 3; // Nombre de lectures consécutives
  float distances[NUM_READINGS];
  int validReadings = 0;

  for (int i = 0; i < NUM_READINGS; i++) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 30000); // Timeout after 30ms (~5m)
    if (duration == 0) {
      distances[i] = INVALID_DISTANCE;
      Serial.println("Ultrasonic: No echo received");
    } else {
      distances[i] = (duration * 0.343) / 2; // Distance in mm
      Serial.print("Ultrasonic: Raw duration = ");
      Serial.print(duration);
      Serial.print(" us, Distance = ");
      Serial.print(distances[i]);
      Serial.println(" mm");
    }

    if (distances[i] >= DETECTION_THRESHOLD_MIN && distances[i] <= DETECTION_THRESHOLD_MAX) {
      validReadings++;
    }
    delay(10); // Petite pause entre les lectures
  }

  // Vérifier si au moins 2 des 3 lectures sont valides
  if (validReadings >= 2) {
    // Retourner la moyenne des lectures valides
    float sum = 0;
    int count = 0;
    for (int i = 0; i < NUM_READINGS; i++) {
      if (distances[i] >= DETECTION_THRESHOLD_MIN && distances[i] <= DETECTION_THRESHOLD_MAX) {
        sum += distances[i];
        count++;
      }
    }
    float averageDistance = sum / count;
    Serial.print("Ultrasonic: Valid average distance = ");
    Serial.print(averageDistance);
    Serial.println(" mm");
    return averageDistance;
  } else {
    Serial.println("Ultrasonic: Insufficient valid readings");
    return INVALID_DISTANCE;
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);

  // Initialize ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  connectWiFi();

  Wire.begin();
  Wire.setClock(400000);  // Fast I2C for better performance

  // Initialize VL53L0X sensor
  sensor.setTimeout(500);
  if (!sensor.init()) {
    Serial.println("Failed to boot VL53L0X");
    while (1);
  }

  // Configure for long-range mode
  sensor.setSignalRateLimit(0.25); // Lower signal rate for increased range
  sensor.setMeasurementTimingBudget(10000);
  sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 18);
  sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 14);

  // Start continuous ranging
  sensor.startContinuous(20);
  lastCountedTime = millis();
  connectMQTT();

  Serial.println("Sensor ready");
}

void loop() {
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) connectMQTT();
  mqttClient.loop();

  // Read distance from VL53L0X
  uint16_t rawDistance = sensor.readRangeContinuousMillimeters();
  float distance;
  bool usingUltrasonic = false;

  // Check if VL53L0X reading is invalid or out of range
  if (sensor.timeoutOccurred() || rawDistance == INVALID_DISTANCE || rawDistance > DETECTION_THRESHOLD_MAX) {
    Serial.println("VL53L0X: Timeout or out of range, switching to ultrasonic sensor");
    distance = readUltrasonicDistance();
    usingUltrasonic = true;
    if (distance == INVALID_DISTANCE || distance > DETECTION_THRESHOLD_MAX) {
      Serial.println("Ultrasonic: Invalid reading or out of range");
      state = WAITING;
      return;
    }
  } else {
    distance = rawDistance; // Use VL53L0X reading
  }

  unsigned long currentTime = millis();
  uint16_t currentDetectionTime = usingUltrasonic ? DETECTION_VALID_TIME_ULTRASONIC : DETECTION_VALID_TIME;

  // Debug output
  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.print(" mm, Sensor: ");
  Serial.print(usingUltrasonic ? "Ultrasonic" : "VL53L0X");
  Serial.print(", State: ");
  switch (state) {
    case WAITING: Serial.print("WAITING"); break;
    case DETECTING: Serial.print("DETECTING"); break;
    case COUNTED: Serial.print("COUNTED"); break;
  }
  Serial.print(", Count: ");
  Serial.println(vehicleCount);

  switch (state) {
    case WAITING:
      // Check for object within valid range
      if (distance >= DETECTION_THRESHOLD_MIN && distance <= DETECTION_THRESHOLD_MAX) {
        state = DETECTING;
        lastDetectionTime = currentTime;
        detectionStartTime = currentTime;  // Record entry time
        Serial.println("Object detected, switching to DETECTING...");
      }
      break;

    case DETECTING: {
      // Block to limit scope of distancesConsistent
      bool distancesConsistent = true;

      // Stocker la nouvelle distance
      lastDistances[distanceIndex % 3] = distance;
      distanceIndex++;

      // Vérifier si les distances sont cohérentes (écart < 100 mm)
      if (distanceIndex >= 3) {
        for (int i = 1; i < 3; i++) {
          if (abs(lastDistances[i] - lastDistances[i-1]) > 100) {
            distancesConsistent = false;
            break;
          }
        }
      }

      // Validate distance and duration
      if (distance >= DETECTION_THRESHOLD_MIN && distance <= DETECTION_THRESHOLD_MAX && distancesConsistent) {
        if (currentTime - lastDetectionTime >= currentDetectionTime) {
          // Valid detection, count vehicle
          vehicleCount++;
          Serial.print("Vehicle detected! Count: ");
          Serial.println(vehicleCount);

          // Send MQTT message
          char message[128];
          snprintf(message, sizeof(message), "{\"node\":\"%s\",\"count\":%lu,\"distance\":%.1f,\"speed\":%.1f,\"type\":\"%s\",\"timestamp\":%lu}",
                   nodeID, vehicleCount, distance, vehicleSpeed, vehicleType, millis());
          mqttClient.publish(mqttTopic, message);
          Serial.print("MQTT sent: ");
          Serial.println(message);

          state = COUNTED;
          distanceIndex = 0; // Réinitialiser l'index
        }
      } else if (currentTime - lastDetectionTime > DETECTION_TIMEOUT || distance == INVALID_DISTANCE) {
        // Timeout or invalid distance, reset to WAITING
        Serial.println("Detection timeout or invalid distance, back to WAITING");
        state = WAITING;
        distanceIndex = 0;
      }
      break;
    }

    case COUNTED:
      if (distance > DETECTION_THRESHOLD_MAX || distance == INVALID_DISTANCE) {
        invalidReadingCount++;
        if (invalidReadingCount >= INVALID_READINGS_THRESHOLD) {
          Serial.println("Object left range, back to WAITING");
          // Calculate vehicle type and speed
          unsigned long timeTaken = currentTime - detectionStartTime;  // Time from DETECTING to WAITING
          float vehicleLength = MEDIUM_VEHICLE_LENGTH;  // Default to medium
          if (timeTaken > 0) {  // Avoid division by zero
            if (timeTaken < SHORT_VEHICLE_TIME) {
              vehicleType = "short";
              vehicleLength = SHORT_VEHICLE_LENGTH;
            } else if (timeTaken < LONG_VEHICLE_TIME) {
              vehicleType = "medium";
              vehicleLength = MEDIUM_VEHICLE_LENGTH;
            } else {
              vehicleType = "long";
              vehicleLength = LONG_VEHICLE_LENGTH;
            }
            float speed_ms = (float)vehicleLength / timeTaken;  // Speed in m/s
            vehicleSpeed = speed_ms * 3.6;  // Convert to km/h
            Serial.print("Vehicle type: ");
            Serial.print(vehicleType);
            Serial.print(", Length: ");
            Serial.print(vehicleLength);
            Serial.print(" mm, Speed: ");
            Serial.print(vehicleSpeed);
            Serial.println(" km/h");
          } else {
            vehicleSpeed = 0.0;
            vehicleType = "unknown";
            Serial.println("Error: Time taken is zero, cannot calculate speed");
          }
          state = WAITING;
          invalidReadingCount = 0;
        }
      } else {
        invalidReadingCount = 0;
        lastCountedTime = currentTime;  // Reset timeout on valid reading
      }
      if (currentTime - lastCountedTime > COUNTED_TIMEOUT) {
        Serial.println("COUNTED timeout, back to WAITING");
        // Calculate vehicle type and speed
        unsigned long timeTaken = currentTime - detectionStartTime;  // Time from DETECTING to WAITING
        float vehicleLength = MEDIUM_VEHICLE_LENGTH;  // Default to medium
        if (timeTaken > 0) {  // Avoid division by zero
          if (timeTaken < SHORT_VEHICLE_TIME) {
            vehicleType = "short";
            vehicleLength = SHORT_VEHICLE_LENGTH;
          } else if (timeTaken < LONG_VEHICLE_TIME) {
            vehicleType = "medium";
            vehicleLength = MEDIUM_VEHICLE_LENGTH;
          } else {
            vehicleType = "long";
            vehicleLength = LONG_VEHICLE_LENGTH;
          }
          float speed_ms = (float)vehicleLength / timeTaken;  // Speed in m/s
          vehicleSpeed = speed_ms * 3.6;  // Convert to km/h
          Serial.print("Vehicle type: ");
          Serial.print(vehicleType);
          Serial.print(", Length: ");
          Serial.print(vehicleLength);
          Serial.print(" mm, Speed: ");
          Serial.print(vehicleSpeed);
          Serial.println(" km/h");
        } else {
          vehicleSpeed = 0.0;
          vehicleType = "unknown";
          Serial.println("Error: Time taken is zero, cannot calculate speed");
        }
        state = WAITING;
        invalidReadingCount = 0;
      }
      break;
  }
}
