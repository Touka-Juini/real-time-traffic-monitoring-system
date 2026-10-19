#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define DISTANCE_THRESHOLD 10  // Changement de distance minimum (en mm)
#define MAX_VALID_DISTANCE 2000 // 2m
#define MIN_VALID_DISTANCE 100  // 10cm
#define NUM_SAMPLES 5
#define RESET_THRESHOLD 20 // Nombre max d'erreurs avant reset du capteur

Adafruit_VL53L0X lox;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

const char* mqttServer = "*********";
const char* wifiSSID = "**********";
const char* wifiPassword = "*********";
const int mqttPort = 1883;

long baselineDistance = -1; 
bool vehicleDetected = false;
int vehicle_id = 0;
int vehicle_count = 0;
unsigned long last_detection_time = 0;
int errorCount = 0; // Compteur d'erreurs du capteur

void setup() {
    Serial.begin(115200);
    setup_wifi();
    setup_mqtt();

    Wire.begin();
    Wire.setClock(400000);

    if (!lox.begin()) {
        Serial.println("Échec de l'initialisation du VL53L0X!");
        while (1);
    }

    // Initialisation de la distance de base
    while (baselineDistance == -1) {
        baselineDistance = get_distance_vl53();
        Serial.print("Lecture de base : ");
        Serial.println(baselineDistance);
        delay(500);
    }

    lox.startRangeContinuous();
    lox.setMeasurementTimingBudgetMicroSeconds(20000);

    Serial.println("Capteur VL53L0X prêt!");
}

void loop() {
    if (lox.isRangeComplete()) {
        long currentDistance = get_distance_vl53();

        // Vérifie si une valeur aberrante est détectée
        if (currentDistance == -1) {
            errorCount++;
            Serial.println("Valeur aberrante ignorée!");
            if (errorCount >= RESET_THRESHOLD) {
                reset_sensor(); // Réinitialisation après trop d'erreurs
            }
            return;
        }

        errorCount = 0; // Réinitialiser le compteur d'erreurs si une lecture valide est obtenue

        // Si la distance est dans les limites acceptables (entre 10 cm et 2 m)
        if (currentDistance >= MIN_VALID_DISTANCE && currentDistance <= MAX_VALID_DISTANCE) {
            // Vérifie si un véhicule est détecté
            if (!vehicleDetected) {
                long distanceChange = abs(currentDistance - baselineDistance);
                
                // Si un changement de distance est détecté, on envoie les données
                if (distanceChange > DISTANCE_THRESHOLD) {
                    vehicle_id++;
                    vehicle_count++;
                    String timestamp = get_timestamp();
                    String jsonData = create_json_data(vehicle_id, vehicle_count, timestamp, currentDistance);
                    send_mqtt_message("node2/vehicle", jsonData);

                    // Mémoriser la dernière détection
                    last_detection_time = millis();
                    vehicleDetected = true;
                    Serial.println("Véhicule détecté!");
                }
            }
            // Si le véhicule est détecté, attendre que la distance change pour retourner en attente
            else {
                long distanceChange = abs(currentDistance - baselineDistance);
                if (distanceChange < DISTANCE_THRESHOLD) {
                    vehicleDetected = false;
                    Serial.println("Véhicule parti, réinitialisation...");
                }
            }
        }
    }

    if (!mqttClient.connected()) {
        reconnect_mqtt();
    }
    mqttClient.loop(); // Assurez-vous que la boucle MQTT est exécutée sans blocage
    delay(100); // Temps de pause pour éviter de surcharger le processeur
}

void setup_wifi() {
    Serial.print("Connexion au WiFi...");
    WiFi.begin(wifiSSID, wifiPassword);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connecté!");
    Serial.print("Adresse IP: ");
    Serial.println(WiFi.localIP());
}

void setup_mqtt() {
    mqttClient.setServer(mqttServer, mqttPort);
    reconnect_mqtt();
}

void reconnect_mqtt() {
    while (!mqttClient.connected()) {
        Serial.print("Connexion MQTT...");
        if (mqttClient.connect("ESP32Client")) {
            Serial.println("connecté");
        } else {
            Serial.println("Échec, nouvelle tentative...");
            delay(5000);
        }
    }
}

long get_distance_vl53() {
    long sum = 0;
    int validReadings = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        VL53L0X_RangingMeasurementData_t measure;
        lox.rangingTest(&measure, false);
        
        // Ignorer les valeurs aberrantes
        if (measure.RangeStatus != 4 && measure.RangeMilliMeter > 0 && measure.RangeMilliMeter <= MAX_VALID_DISTANCE) {
            sum += measure.RangeMilliMeter;
            validReadings++;
        }
        delay(5); // Pause entre les mesures
    }

    return (validReadings > 0) ? (sum / validReadings) : -1;
}

void reset_sensor() {
    Serial.println("Réinitialisation du capteur VL53L0X...");
    errorCount = 0;
    lox.stopRangeContinuous();
    delay(100);
    if (!lox.begin()) {
        Serial.println("Échec du redémarrage du VL53L0X!");
    } else {
        lox.startRangeContinuous();
        Serial.println("Capteur réinitialisé avec succès.");
    }
}

String get_timestamp() {
    unsigned long currentMillis = millis();
    unsigned long seconds = currentMillis / 1000;
    unsigned long minutes = seconds / 60;
    seconds = seconds % 60;
    unsigned long hours = minutes / 60;
    minutes = minutes % 60;
    
    char timestamp[20];
    sprintf(timestamp, "%02lu:%02lu:%02lu.%03lu", hours, minutes, seconds, currentMillis % 1000);
    return String(timestamp);
}

String create_json_data(int vehicle_id, int vehicle_count, String timestamp, long distance) {
    String jsonData = "{";
    jsonData += "\"vehicle_id\":" + String(vehicle_id) + ","; 
    jsonData += "\"vehicle_count\":" + String(vehicle_count) + ","; 
    jsonData += "\"timestamp\":\"" + timestamp + "\","; 
    jsonData += "\"distance\":" + String(distance);
    jsonData += "}";

    return jsonData;
}

void send_mqtt_message(const char* topic, String message) {
    Serial.print("Publication sur : ");
    Serial.println(topic);
    Serial.print("Message : ");
    Serial.println(message);
    
    bool success = mqttClient.publish(topic, message.c_str(), true);
    if (success) {
        Serial.println("Message envoyé!");
    } else {
        Serial.println("Échec de l'envoi!");
    }
}
