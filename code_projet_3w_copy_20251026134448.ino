#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <Adafruit_BME280.h>
#include <ChainableLED.h>

// === Pins Grove ===
#define LED_CLK 7
#define LED_DATA 8
#define NUM_LEDS 1

#define BUTTON_RED 2
#define BUTTON_GREEN 3
#define LIGHT_SENSOR A0
#define SD_CS 4

// === Objets capteurs ===
Adafruit_BME280 bme;
RTC_DS1307 rtc;
ChainableLED led(LED_CLK, LED_DATA, NUM_LEDS);

// === Structure mesure ===
struct Mesure {
  float temperature;
  float humidite;
  float pression;
  int luminosite;
  String timestamp;
} mesure;

// === Modes ===
enum Mode { STANDARD, CONFIG, ECO, MAINTENANCE };
Mode modeActuel = STANDARD;
Mode modePrecedent = STANDARD;

// === États et timers ===
unsigned long lastMeasure = 0;
unsigned long intervalStandard = 5000;
unsigned long intervalEco = 15000;
unsigned long inactivityTimer = 0;
bool sdReady = false;
bool bmeReady = false;

// === Appui long ===
unsigned long pressStartRed = 0;
unsigned long pressStartGreen = 0;
bool redPressed = false;
bool greenPressed = false;

// === Fonctions LED ===
void setLEDColor(float r, float g, float b) {
  led.setColorRGB(0, (int)r, (int)g, (int)b);
}

void updateLEDByMode() {
  switch (modeActuel) {
    case STANDARD: setLEDColor(0, 255, 0); break;      // Vert
    case CONFIG: setLEDColor(255, 255, 0); break;      // Jaune
    case ECO: setLEDColor(0, 0, 255); break;           // Bleu
    case MAINTENANCE: setLEDColor(255, 100, 0); break; // Orange
  }
}

// === Initialisation ===
void setup() {
  Serial.begin(9600);
  setLEDColor(0,0,0);

  pinMode(BUTTON_RED, INPUT_PULLUP);
  pinMode(BUTTON_GREEN, INPUT_PULLUP);

  if (digitalRead(BUTTON_RED) == LOW) modeActuel = CONFIG;
  else modeActuel = STANDARD;

  if (bme.begin(0x76)) { bmeReady = true; Serial.println("BME280 OK"); }
  else Serial.println("⚠️ BME280 non détecté");

  if (!rtc.begin()) Serial.println("⚠️ RTC non détectée");
  else Serial.println("RTC OK");

  if (SD.begin(SD_CS)) {
    sdReady = true;
    Serial.println("Carte SD initialisée");
    if (!SD.exists("meteo.csv")) {
      File f = SD.open("meteo.csv", FILE_WRITE);
      f.println("timestamp;temperature;humidite;pression;luminosite");
      f.close();
    }
  } else Serial.println("⚠️ Erreur SD");

  updateLEDByMode();
  inactivityTimer = millis();
}

// === Lecture des capteurs ===
void lireCapteurs() {
  mesure.temperature = bme.readTemperature();
  mesure.humidite = bme.readHumidity();
  mesure.pression = bme.readPressure() / 100.0;
  mesure.luminosite = analogRead(LIGHT_SENSOR);

  DateTime now = rtc.now();
  char buf[20];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  mesure.timestamp = String(buf);

  Serial.print("T="); Serial.print(mesure.temperature);
  Serial.print("C | H="); Serial.print(mesure.humidite);
  Serial.print("% | P="); Serial.print(mesure.pression);
  Serial.print("hPa | L="); Serial.println(mesure.luminosite);
}

// === Sauvegarde ===
void sauvegarderDonnees() {
  if (!sdReady || modeActuel == CONFIG || modeActuel == MAINTENANCE) return;
  File f = SD.open("meteo.csv", FILE_WRITE);
  if (f) {
    f.print(mesure.timestamp); f.print(';');
    f.print(mesure.temperature); f.print(';');
    f.print(mesure.humidite); f.print(';');
    f.print(mesure.pression); f.print(';');
    f.println(mesure.luminosite);
    f.close();
  }
}

// === Vérifie appuis boutons (y compris longs) ===
void checkButtons() {
  bool redState = digitalRead(BUTTON_RED) == LOW;
  bool greenState = digitalRead(BUTTON_GREEN) == LOW;

  unsigned long now = millis();

  // ---- bouton rouge ----
  if (redState && !redPressed) { redPressed = true; pressStartRed = now; }
  if (!redState && redPressed) { redPressed = false; }

  // Appui long rouge (5s)
  if (redPressed && (now - pressStartRed >= 5000)) {
    redPressed = false;
    if (modeActuel == STANDARD) {
      modePrecedent = modeActuel;
      modeActuel = MAINTENANCE;
    } else if (modeActuel == ECO) {
      modeActuel = STANDARD;
    } else if (modeActuel == MAINTENANCE) {
      modeActuel = modePrecedent;
    }
    updateLEDByMode();
    Serial.println("→ Bascule via bouton rouge");
  }

  // ---- bouton vert ----
  if (greenState && !greenPressed) { greenPressed = true; pressStartGreen = now; }
  if (!greenState && greenPressed) { greenPressed = false; }

  // Appui long vert (5s)
  if (greenPressed && (now - pressStartGreen >= 5000)) {
    greenPressed = false;
    if (modeActuel == STANDARD) {
      modeActuel = ECO;
      Serial.println("→ Bascule en mode ECO");
    }
    updateLEDByMode();
  }
}

// === Boucle principale ===
void loop() {
  checkButtons();
  updateLEDByMode();

  unsigned long now = millis();

  // === CONFIG : pas de mesures ===
  if (modeActuel == CONFIG) {
    if (now - inactivityTimer > 30UL * 60UL * 1000UL) {
      Serial.println("→ Retour auto mode standard (30min)");
      modeActuel = STANDARD;
      updateLEDByMode();
    }
    delay(500);
    return;
  }

  // === MAINTENANCE : données série ===
  if (modeActuel == MAINTENANCE) {
    lireCapteurs();
    delay(3000);
    return;
  }

  // === ECO ou STANDARD : mesures + sauvegarde ===
  unsigned long interval = (modeActuel == ECO) ? intervalEco : intervalStandard;
  if (now - lastMeasure >= interval) {
    lastMeasure = now;
    lireCapteurs();
    sauvegarderDonnees();
  }

  delay(50); rawdha 
}

