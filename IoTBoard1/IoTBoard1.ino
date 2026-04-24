#include <WiFi.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <Firebase.h>

// wifi
#define WIFI_SSID "UPH@Event"
#define WIFI_PASSWORD "transformationalleadership"

// firebase 
#define FIREBASE_URL "https://smartindoorfarming-default-rtdb.firebaseio.com/"
Firebase fb(FIREBASE_URL);

String mode;
bool growLight;
int durasiJam;

// growlight
#define RELAY_PIN 4

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

// gy302 bh1750
BH1750 lightMeter;

// jalankan sesuatu setiap 10 detik
unsigned long lastSendTime = 0;
const unsigned long interval = 10000;

unsigned long startMillis = 0;
bool autoStarted = false;

// notif
bool notifSent = false;

void setup() {
  Serial.begin(115200);

  // setup wifi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  // pin
  pinMode(trigAir, OUTPUT);
  pinMode(echoAir, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // LOW -> hidup

  // i2c gy302 bh1750
  Wire.begin(22,23);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  dht.begin();
}

void loop() {
  // sesnor dht11
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

  // sensor intensitas cahaya
  float lux = lightMeter.readLightLevel(); 
  if (lux < 0) lux = 0;

  // sensor ultrasonic
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

  // sensor nutrisi
  float valA = analogRead(pinLevelA);
  float valB = analogRead(pinLevelB);

  // serial print
  Serial.print("Cahaya: "); 
  Serial.print(lux); 
  Serial.println(" lux"); 

  Serial.print("Jarak Air: "); 
  Serial.print(jarak); 
  Serial.println("cm"); 
  
  Serial.print("Nutrisi A: "); 
  Serial.println(valA); 

  Serial.print("Nutrisi B: "); 
  Serial.println(valB); 

  // ambil data firebase
  fb.getString("/SmartIndoorFarming/Aktuator/mode", mode);
  fb.getBool("/SmartIndoorFarming/Aktuator/growLight", growLight);
  fb.getInt("/SmartIndoorFarming/Parameter/durasi", durasiJam);

  // manual dan auto
  if (mode == "MANUAL") {

    if (growLight) {
      digitalWrite(RELAY_PIN, LOW);
      Serial.println("Growlight MANUAL: NYALA");
    } else {
      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("Growlight MANUAL: MATI");
    }
    autoStarted = false; // reset AUTO
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

    // kalau mau test, ganti ke 60000UL. kalau biasa 3600000UL
    unsigned long durasiNyala = durasiJam * 60000UL;
    unsigned long durasiMati  = (24 - durasiJam) * 60000UL;

    unsigned long elapsed = now - startMillis;

    if (elapsed < durasiNyala) {
      digitalWrite(RELAY_PIN, LOW);
      Serial.println("AUTO: NYALA");
    } 
    else if (elapsed < (durasiNyala + durasiMati)) {
      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("AUTO: MATI");
    } 
    else {
      startMillis = now; // reset siklus 24 jam
      Serial.println("AUTO: RESET SIKLUS");
    }
  }

  int suhuMin, suhuMax, kelembapanMax, kelembapanMin, cahayaMax, cahayaMin;

  fb.getInt("/SmartIndoorFarming/Parameter/suhuMin", suhuMin);
  fb.getInt("/SmartIndoorFarming/Parameter/suhuMax", suhuMax);
  fb.getInt("/SmartIndoorFarming/Parameter/kelembapanMin", kelembapanMin);
  fb.getInt("/SmartIndoorFarming/Parameter/kelembapanMax", kelembapanMax);
  fb.getInt("/SmartIndoorFarming/Parameter/cahayaMin", cahayaMin);
  fb.getInt("/SmartIndoorFarming/Parameter/cahayaMax", cahayaMax);

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

  if (valA < 1000 || valB < 1000) {
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

  // RESET kalau sudah normal
  if (!kondisiBahaya && notifSent) {
    fb.setBool("/SmartIndoorFarming/Notifikasi/status", false);

    Serial.println("Kondisi kembali normal");

    notifSent = false;
  }
  
  // kirim data (tiap 10 detik)
  if (millis() - lastSendTime > interval) {
  lastSendTime = millis();

    // kirim data ke database
    fb.setFloat("/SmartIndoorFarming/Sensor/suhu", suhu);
    fb.setFloat("/SmartIndoorFarming/Sensor/kelembapan", kelembapan);
    fb.setFloat("/SmartIndoorFarming/Sensor/intensitasCahaya", lux);
    fb.setFloat("/SmartIndoorFarming/Sensor/tinggiAir", jarak);
    fb.setFloat("/SmartIndoorFarming/Sensor/tinggiNutrisiA", valA);
    fb.setFloat("/SmartIndoorFarming/Sensor/tinggiNutrisiB", valB);

    Serial.println("Data terkirim!");
  }
  delay(1000);
}