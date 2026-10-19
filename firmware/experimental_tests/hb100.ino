#define HB100_PIN 23  // Connexion directe à la sortie IF du HB100

volatile unsigned long pulseCount = 0;

void IRAM_ATTR countPulse() {
    pulseCount++;
}

void setup() {
    Serial.begin(115200);
    pinMode(HB100_PIN, INPUT);
    attachInterrupt(HB100_PIN, countPulse, RISING);
}

void loop() {
    delay(2000);  // Attente plus longue pour capter des pulsations
    Serial.print("Fréquence détectée : ");
    Serial.print(pulseCount);
    Serial.println(" Hz");
    pulseCount = 0;  // Réinitialisation
}
