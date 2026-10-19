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
const char* nodeID = "node1";

const char* mqttStatusTopic = "vehicle/status";


WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Detection parameters
#define DETECTION_THRESHOLD_MAX 2000 // Max detection distance in mm (2 m)
#define DETECTION_THRESHOLD_MIN 200  // Min detection distance in mm (ignore close noise)
#define MIN_DETECTION_TIME 50        // Min detection time in ms to validate a vehicle
#define DEBOUNCE_TIME 200            // Debounce time in ms to prevent double counting
#define MOVING_AVERAGE_SIZE 5        // Number of readings for moving average
#define CALIBRATION_DISTANCE 100     // Calibration target distance in mm (10 cm)
#define NO_TARGET_DISTANCE 3000      // Distance for no target (mm) 
#define DETECTION_TIMEOUT 1000       // Timeout for DETECTING state (ms)




// Ultrasonic sensor pins
#define TRIG_PIN  12 // Adjust pin as needed
#define ECHO_PIN  13  // Adjust pin as needed
#define ULTRASONIC_THRESHOLD 2000  // mm, same as VL53L0X max detection


// Sensor object
VL53L0X sensor;

// Global variables
unsigned long lastDetectionTime = 0;   // Time of last detection
unsigned long vehicleCount = 0;        // Vehicle counter
int offsetCalibration = 0;             // Software offset calibration in mm
float distanceBuffer[MOVING_AVERAGE_SIZE]; // Buffer for moving average
int bufferIndex = 0;
bool bufferFilled = false;

const int offsetCalibration = -26;  // Adjust this to your calculated offset

enum DetectionState {
  WAITING,
  DETECTING,
  COUNTED
};

DetectionState state = WAITING;
float lastAvgDistance = NO_TARGET_DISTANCE;


long readUltrasonicDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
  long distance = duration * 0.0343 / 2; // Convert to mm
  return distance;
}




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
      mqttClient.publish(mqttStatusTopic, "online", true); // Retained = true
    } else {
      Serial.println("Failed");
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

    // Ultrasonic pin modes
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
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


  // --- Read Ultrasonic ---
  long ultrasonicDistance = readUltrasonicDistance();


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
        if (
          avgDistance < DETECTION_THRESHOLD_MAX &&
          avgDistance > DETECTION_THRESHOLD_MIN &&
          ultrasonicDistance > 0 && ultrasonicDistance < ULTRASONIC_THRESHOLD &&
          lastAvgDistance >= DETECTION_THRESHOLD_MAX) {

            state = DETECTING;
            lastDetectionTime = currentTime;
            Serial.println("Object detected by BOTH sensors!");
            }
        break;

      case DETECTING:
        if (avgDistance >= DETECTION_THRESHOLD_MAX  || ultrasonicDistance >= ULTRASONIC_THRESHOLD) {
          // Object has left; count if minimum time is met
          if (currentTime - lastDetectionTime >= MIN_DETECTION_TIME) {
            vehicleCount++;
            Serial.print("Vehicle detected! Count: ");
            Serial.println(vehicleCount);

            // Format and publish MQTT message
            char message[128];
            snprintf(message, sizeof(message), "{\"node\":\"%s\",\"count\":%lu,\"distance\":%.1f,\"timestamp\":%lu}",
            nodeID, vehicleCount, avgDistance, millis());
            mqttClient.publish(mqttTopic, message);
            Serial.print("MQTT Sent: ");
            Serial.println(message); 
            
               
            state = COUNTED;
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
