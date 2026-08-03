#define RELAY1 23
#define RELAY2 22
#define RELAY3 21
const int tdsPin = 34;

#include <WiFi.h>
#include <Wire.h>
#include <Firebase.h>
#include <ArduinoJson.h>
#include "secrets.h"

Firebase fb(FIREBASE_URL);

unsigned long previousMillisSensor = 0;
const long intervalSensor = 1000;

unsigned long previousMillisFirebase = 0;
const long intervalFirebase = 3000;

unsigned long previousMillisWifiCheck = 0;
const long intervalWifiCheck = 5000;

// TDS 
float voltase = 0;
float nilaiTDS = 0;
const float VOLTAGE_FLOATING_THRESHOLD = 1.15;
float koefisienKompensasi = 1.0;

const int HYSTERESIS_PPM = 20;

bool statusPompaNutrisiA = false;
bool statusPompaNutrisiB = false;
bool statusPompaAir = false;
int paramTDSMin = 0;
int paramTDSMax = 0;
String mode = "OTOMATIS";

// KONFIGURASI RELAY 
const bool RELAY_ACTIVE_LOW = false;

void setRelay(int pin, bool nyala) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(pin, nyala ? LOW : HIGH);
  } else {
    digitalWrite(pin, nyala ? HIGH : LOW);
  }
}

bool notifSentTDS = false;

float bacaTDS();
void kontrolPompa();
void syncFirebase();
void cekWifi();

void setup() {
  Serial.begin(115200);
  pinMode(tdsPin, INPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);

  setRelay(RELAY1, false);
  setRelay(RELAY2, false);
  setRelay(RELAY3, false);
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillisWifiCheck >= intervalWifiCheck) {
    previousMillisWifiCheck = currentMillis;
    cekWifi();
  }

  if (currentMillis - previousMillisSensor >= intervalSensor) {
    previousMillisSensor = currentMillis;
    nilaiTDS = bacaTDS();

    Serial.print("Voltase: ");
    Serial.print(voltase, 3);
    Serial.print("V | Nilai TDS: ");
    Serial.print(nilaiTDS);
    Serial.println(" PPM");
  }

  if (currentMillis - previousMillisFirebase >= intervalFirebase) {
    previousMillisFirebase = currentMillis;
    syncFirebase();
  }
}

float bacaTDS() {
  const int SAMPLE_COUNT = 20;
  long total = 0;
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    total += analogRead(tdsPin);
    delay(2);
  }

  int analogValue = total / SAMPLE_COUNT;
  voltase = analogValue * (3.3 / 4095.0);

  if (voltase < VOLTAGE_FLOATING_THRESHOLD) {
    return 0.0;
  }

  float voltaseKompensasi = voltase / koefisienKompensasi;
  float tds = (
    133.42 * pow(voltaseKompensasi, 3)
    - 255.86 * pow(voltaseKompensasi, 2)
    + 857.39 * voltaseKompensasi
  ) * 0.5;

  if (tds < 0) tds = 0;
  return tds;
}

void cekWifi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi terputus, mencoba reconnect...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// RELAY1 = pompaNutrisiA, RELAY2 = pompaNutrisiB, RELAY3 = pompaAir
void kontrolPompa() {
  if (mode == "MANUAL") {

    setRelay(RELAY1, statusPompaNutrisiA);
    setRelay(RELAY2, statusPompaNutrisiB);
    setRelay(RELAY3, statusPompaAir);

    Serial.print("Manual -> NutrisiA:");
    Serial.print(statusPompaNutrisiA ? "ON" : "OFF");
    Serial.print(" NutrisiB:");
    Serial.print(statusPompaNutrisiB ? "ON" : "OFF");
    Serial.print(" Air:");
    Serial.println(statusPompaAir ? "ON" : "OFF");

  } else {
    if (nilaiTDS < paramTDSMin) {
      setRelay(RELAY1, true);
      setRelay(RELAY2, true);
      setRelay(RELAY3, true);
      Serial.println("Otomatis: Nutrisi + Air ON - TDS Rendah");
    }
    else if (nilaiTDS >= paramTDSMax + HYSTERESIS_PPM) {
      setRelay(RELAY1, false);
      setRelay(RELAY2, false);
      setRelay(RELAY3, false);
      Serial.println("Otomatis: Semua OFF - TDS Tinggi");
    }
    else {
      Serial.println("Otomatis: Stabil (dalam range hysteresis)");
    }
  }
}

void syncFirebase() {
  String jsonAktuator;
  int codeAktuator = fb.getJson("/SmartIndoorFarming/Aktuator", jsonAktuator);

  String jsonParameter;
  int codeParameter = fb.getJson("/SmartIndoorFarming/Parameter", jsonParameter);

  if (codeAktuator != 200 || codeParameter != 200) {
    Serial.print("Gagal ambil data Firebase. codeAktuator=");
    Serial.print(codeAktuator);
    Serial.print(" codeParameter=");
    Serial.println(codeParameter);
    return;
  }

  JsonDocument docAktuator;
  DeserializationError errA = deserializeJson(docAktuator, jsonAktuator);
  if (errA) {
    Serial.print("Gagal parse JSON Aktuator: ");
    Serial.println(errA.c_str());
    return;
  }
  mode = docAktuator["modePompa"] | mode;
  statusPompaNutrisiA = docAktuator["pompaNutrisiA"] | statusPompaNutrisiA;
  statusPompaNutrisiB = docAktuator["pompaNutrisiB"] | statusPompaNutrisiB;
  statusPompaAir = docAktuator["pompaAir"] | statusPompaAir;

  JsonDocument docParameter;
  DeserializationError errP = deserializeJson(docParameter, jsonParameter);
  if (errP) {
    Serial.print("Gagal parse JSON Parameter: ");
    Serial.println(errP.c_str());
    return;
  }
  paramTDSMin = docParameter["tdsMin"] | paramTDSMin;
  paramTDSMax = docParameter["tdsMax"] | paramTDSMax;

  Serial.println("Mode: " + mode);

  kontrolPompa();

  bool kondisiBahayaTDS = false;
  String pesanTDS = "";

  if (nilaiTDS < paramTDSMin) {
    kondisiBahayaTDS = true;
    pesanTDS = "TDS rendah! Nutrisi kurang.";
  }
  else if (nilaiTDS >= paramTDSMax) {
    kondisiBahayaTDS = true;
    pesanTDS = "TDS terlalu tinggi! Nutrisi berlebih.";
  }

  if (kondisiBahayaTDS && !notifSentTDS) {
    fb.setString("/SmartIndoorFarming/Notifikasi/pesanTDS", pesanTDS);
    fb.setBool("/SmartIndoorFarming/Notifikasi/statusTDS", true);
    Serial.println("NOTIF: " + pesanTDS);
    notifSentTDS = true;
  }

  if (!kondisiBahayaTDS && notifSentTDS) {
    fb.setBool("/SmartIndoorFarming/Notifikasi/statusTDS", false);
    Serial.println("TDS kembali normal");
    notifSentTDS = false;
  }
}
