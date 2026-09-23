#include <WiFi.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <Firebase.h>
#include <ArduinoJson.h>
#include "secrets.h"

Firebase fb(FIREBASE_URL);

String mode;
bool growLight;
int durasiJam;

// growlight
#define RELAY_PIN 4

const bool GROWLIGHT_ACTIVE_LOW = false;

void setGrowLight(bool nyala) {
  if (GROWLIGHT_ACTIVE_LOW) {
    digitalWrite(RELAY_PIN, nyala ? LOW : HIGH);
  } else {
    digitalWrite(RELAY_PIN, nyala ? HIGH : LOW);
  }
}

// dht11
#define DHTPIN 27
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ultrasonic
const int trigAir = 19;
const int echoAir = 18;

// waterlevel
const int pinLevelA = 34;
const int pinLevelB = 35;

const int THRESHOLD_KERING_A = 120;
const int THRESHOLD_KERING_B = 470;

const float RAW_KERING_A = 417.0;
const float RAW_PENUH_A = 2676.85;
const float RAW_KERING_B = 417.0;
const float RAW_PENUH_B = 5360.33;
const float TINGGI_MAKS_CM = 4.0;

const int AMBANG_NUTRISI_RENDAH_A = 434;
const int AMBANG_NUTRISI_RENDAH_B = 693;

BH1750 lightMeter;

unsigned long lastSendTime = 0;
const unsigned long interval = 10000;

unsigned long previousMillisFirebase = 0;
const unsigned long intervalFirebase = 2000;

unsigned long startMillis = 0;
bool autoStarted = false;

bool notifSent = false;

int suhuMin = 0, suhuMax = 100, kelembapanMin = 0, kelembapanMax = 100, cahayaMin = 0, cahayaMax = 100000;

float bacaLevelSensor(int pin);
float konversiCm(float raw, float rawKering, float rawPenuh, float tinggiMaks);

void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  pinMode(trigAir, OUTPUT);
  pinMode(echoAir, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  setGrowLight(false);

  Wire.begin(22,23);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  dht.begin();
}

void loop() {
  float suhu = dht.readTemperature();
  float kelembapan = dht.readHumidity();

  if (isnan(suhu) || isnan(kelembapan)) { 
    Serial.println("Gagal membaca DHT11"); 
  } else { 
    Serial.print("Suhu: "); 
    Serial.print(suhu); 
    Serial.print(" °C | Kelembapan: "); 
    Serial.print(kelembapan); 
    Serial.println(" %"); 
  }
  
  float lux = lightMeter.readLightLevel(); 
  if (lux < 0) lux = 0;

  long durasi; 
  float jarak;

  digitalWrite(trigAir, LOW);
  delayMicroseconds(2);
  digitalWrite(trigAir, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigAir, LOW);

  durasi = pulseIn(echoAir, HIGH, 30000);
  if (durasi == 0) {
    jarak = 0;
  } else {
    jarak = durasi * 0.034 / 2;
  }

  float valA = bacaLevelSensor(pinLevelA);
  float valB = bacaLevelSensor(pinLevelB);

  float tinggiA_cm = konversiCm(valA, RAW_KERING_A, RAW_PENUH_A, TINGGI_MAKS_CM);
  float tinggiB_cm = konversiCm(valB, RAW_KERING_B, RAW_PENUH_B, TINGGI_MAKS_CM);

  Serial.print("Cahaya: "); 
  Serial.print(lux); 
  Serial.println(" lux"); 

  Serial.print("Jarak Air: "); 
  Serial.print(jarak); 
  Serial.println("cm"); 
  
  Serial.print("Nutrisi A (raw): "); 
  Serial.print(valA); 
  Serial.print(" | Nutrisi A (cm): "); 
  Serial.println(tinggiA_cm); 

  Serial.print("Nutrisi B (raw): "); 
  Serial.print(valB); 
  Serial.print(" | Nutrisi B (cm): "); 
  Serial.println(tinggiB_cm); 

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillisFirebase >= intervalFirebase) {
    previousMillisFirebase = currentMillis;

    String jsonAktuator;
    int codeAktuator = fb.getJson("/SmartIndoorFarming/Aktuator", jsonAktuator);

    String jsonParameter;
    int codeParameter = fb.getJson("/SmartIndoorFarming/Parameter", jsonParameter);

    if (codeAktuator == 200) {
      JsonDocument docAktuator;
      DeserializationError errA = deserializeJson(docAktuator, jsonAktuator);
      if (!errA) {
        mode = docAktuator["modeGrowLight"] | mode;
        growLight = docAktuator["growLight"] | growLight;
      } else {
        Serial.print("Gagal parse JSON Aktuator: ");
        Serial.println(errA.c_str());
      }
    } else {
      Serial.print("Gagal ambil Aktuator dari Firebase, code=");
      Serial.println(codeAktuator);
    }

    if (codeParameter == 200) {
      JsonDocument docParameter;
      DeserializationError errP = deserializeJson(docParameter, jsonParameter);
      if (!errP) {
        durasiJam = docParameter["durasi"] | durasiJam;
        suhuMin = docParameter["suhuMin"] | suhuMin;
        suhuMax = docParameter["suhuMax"] | suhuMax;
        kelembapanMin = docParameter["kelembapanMin"] | kelembapanMin;
        kelembapanMax = docParameter["kelembapanMax"] | kelembapanMax;
        cahayaMin = docParameter["cahayaMin"] | cahayaMin;
        cahayaMax = docParameter["cahayaMax"] | cahayaMax;
      } else {
        Serial.print("Gagal parse JSON Parameter: ");
        Serial.println(errP.c_str());
      }
    } else {
      Serial.print("Gagal ambil Parameter dari Firebase, code=");
      Serial.println(codeParameter);
    }
  }

  if (mode == "MANUAL") {

    if (growLight) {
      setGrowLight(true);
      Serial.println("Growlight MANUAL: NYALA");
    } else {
      setGrowLight(false);
      Serial.println("Growlight MANUAL: MATI");
    }
    autoStarted = false;
  } 
  else if (mode == "AUTO") {
    if (!autoStarted) {
      startMillis = millis();
      autoStarted = true;
      Serial.println("AUTO mulai");
    }

    if (durasiJam <= 0) durasiJam = 12;
    if (durasiJam > 24) durasiJam = 24;

    unsigned long now  = millis();
    unsigned long durasiNyala = durasiJam * 3600000UL;
    unsigned long durasiMati  = (24 - durasiJam) * 3600000UL;
    unsigned long elapsed = now - startMillis;

    if (elapsed < durasiNyala) {
      setGrowLight(true);
      Serial.println("AUTO: NYALA");
    } 
    else if (elapsed < (durasiNyala + durasiMati)) {
      setGrowLight(false);
      Serial.println("AUTO: MATI");
    } 
    else {
      startMillis = now;
      Serial.println("AUTO: RESET SIKLUS");
    }
  }

  bool kondisiBahaya = false;
  String pesan = "";

  if (suhu < suhuMin || suhu > suhuMax) {
    kondisiBahaya = true;
    pesan += "Suhu di luar batas normal!\n";
  }

  if (kelembapan < kelembapanMin || kelembapan > kelembapanMax) {
    kondisiBahaya = true;
    pesan += "Kelembapan di luar batas normal!\n";
  }

  if (lux < cahayaMin) {
    kondisiBahaya = true;
    pesan += "Intensitas cahaya kurang!\n";
  }

  if (jarak > 20) {
    kondisiBahaya = true;
    pesan += "Air hampir habis!\n";
  }

  if (valA < AMBANG_NUTRISI_RENDAH_A || valB < AMBANG_NUTRISI_RENDAH_B) {
    kondisiBahaya = true;
    pesan += "Persentasi nutrisi rendah!\n";
  }

  if (kondisiBahaya && !notifSent) {
    String finalNotif = "Peringatan!\n\n";
    finalNotif += pesan;
    finalNotif += "\nSegera cek kondisi tanaman.";

    fb.setString("/SmartIndoorFarming/Notifikasi/pesan", finalNotif);
    fb.setBool("/SmartIndoorFarming/Notifikasi/status", true);

    Serial.println("Notif Dikirim");
    Serial.println(finalNotif);
    notifSent = true;
  }

  if (!kondisiBahaya && notifSent) {
    fb.setBool("/SmartIndoorFarming/Notifikasi/status", false);
    Serial.println("Kondisi kembali normal");
    notifSent = false;
  }
  
  if (millis() - lastSendTime > interval) {
    lastSendTime = millis();

    fb.setFloat("/SmartIndoorFarming/Sensor/suhu", suhu);
    fb.setFloat("/SmartIndoorFarming/Sensor/kelembapan", kelembapan);
    fb.setFloat("/SmartIndoorFarming/Sensor/intensitasCahaya", lux);
    fb.setFloat("/SmartIndoorFarming/Sensor/tinggiAir", jarak);
    fb.setFloat("/SmartIndoorFarming/Sensor/tinggiNutrisiA", tinggiA_cm);
    fb.setFloat("/SmartIndoorFarming/Sensor/tinggiNutrisiB", tinggiB_cm);

    Serial.println("Data terkirim!");
  }
  delay(1000);
}

float bacaLevelSensor(int pin) {
  return analogRead(pin);
}

float konversiCm(float raw, float rawKering, float rawPenuh, float tinggiMaks) {
  float cm = (raw - rawKering) / (rawPenuh - rawKering) * tinggiMaks;

  if (cm < 0.0) cm = 0.0;
  if (cm > tinggiMaks) cm = tinggiMaks;

  return cm;
}
