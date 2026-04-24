#define RELAY1 23
#define RELAY2 22
#define RELAY3 21
const int tdsPin = 34;      // Kabel kuning sensor TDS dicolok ke pin D32

float voltase = 0;          // Variabel untuk menyimpan tegangan
float nilaiTDS = 0;         // Variabel untuk menyimpan hasil PPM

unsigned long previousMillis = 0;
const long interval = 1000; // 5 detik

#include <WiFi.h>
#include <Wire.h>
#include <Firebase.h>
#include "secrets.h"

Firebase fb(FIREBASE_URL);

bool statusPompa;
int paramTDSMin;
int paramTDSMax;
String mode;

// notif
bool notifSentTDS = false;

void setup() {
  Serial.begin(115200);     // Kecepatan komunikasi ke laptop
  pinMode(tdsPin, INPUT);   // Mengatur pin D32 sebagai input
  // Serial.println("--- GrowBit TDS Monitor Dimulai ---");
  
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

  // semua pompa mati awal
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);
  digitalWrite(RELAY3, LOW);
}

void loop() {
  // 1. Baca data kasar dari sensor (0 - 4095 untuk ESP32)
  int analogValue = analogRead(tdsPin);
  
  // 2. Ubah data kasar jadi Voltase (ESP32 biasanya 3.3V)
  voltase = analogValue * (3.3 / 4095.0);
  
  // 3. Rumus Konversi Voltase ke PPM (Standar Sensor TDS Meter)
  // Rumus ini mengasumsikan suhu air standar (25°C)
  float koefisienKompensasi = 1.0; 
  float voltaseKompensasi = voltase / koefisienKompensasi;
  
  nilaiTDS = (133.42 * pow(voltaseKompensasi, 3) - 255.86 * pow(voltaseKompensasi, 2) + 857.39 * voltaseKompensasi) * 0.5;

  // 4. Tampilkan hasil ke Serial Monitor
  Serial.print("Voltase: ");
  Serial.print(voltase);
  Serial.print("V | Nilai TDS: ");
  Serial.print(nilaiTDS);
  Serial.println(" PPM");

  unsigned long currentMillis = millis();
   // Ambil data dari Firebase

  int modeCode = fb.getString("/SmartIndoorFarming/Aktuator/mode", mode);
  int responseCode = fb.getBool("/SmartIndoorFarming/Aktuator/pompaAir", statusPompa);
  fb.getInt("/SmartIndoorFarming/Parameter/tdsMin", paramTDSMin);
  fb.getInt("/SmartIndoorFarming/Parameter/tdsMax", paramTDSMax);
  if (responseCode == 200 && modeCode == 200) {
  Serial.println(mode);
  if(mode == "MANUAL"){
    if(statusPompa){
      digitalWrite(RELAY1, HIGH);
      digitalWrite(RELAY2, HIGH);
      digitalWrite(RELAY3, HIGH);
      Serial.println("Pompa ON");
    } else{
        digitalWrite(RELAY1, LOW);
        digitalWrite(RELAY2, LOW);
        digitalWrite(RELAY3, LOW);
        Serial.println("Pompa OFF");
      }
  } else{
    if(nilaiTDS < paramTDSMin){
      digitalWrite(RELAY1, HIGH);
      digitalWrite(RELAY2, HIGH);
      digitalWrite(RELAY3, HIGH);
      Serial.println("Pompa ON - TDS Rendah");
    } else if(nilaiTDS >= paramTDSMax){
      digitalWrite(RELAY1, LOW);
      digitalWrite(RELAY2, LOW);
      digitalWrite(RELAY3, LOW);
      Serial.println("Pompa OFF- TDS Tinggi");
    } else{
        Serial.println("Pompa Stabil");
    }
  }
  // NOTIFIKASI TDS
  bool kondisiBahayaTDS = false;
  String pesanTDS = "";

  if (nilaiTDS < paramTDSMin) {
    kondisiBahayaTDS = true;
    pesanTDS = "TDS rendah! Nutrisi kurang.";
  } 
  else if (nilaiTDS > paramTDSMax) {
    kondisiBahayaTDS = true;
    pesanTDS = "TDS terlalu tinggi! Nutrisi berlebih.";
  }

  // kirim notif (sekali)
  if (kondisiBahayaTDS && !notifSentTDS) {
    fb.setString("/SmartIndoorFarming/Notifikasi/pesanTDS", pesanTDS);
    fb.setBool("/SmartIndoorFarming/Notifikasi/statusTDS", true);

    Serial.println("NOTIF: " + pesanTDS);

    notifSentTDS = true;
  }
  
  // reset notif
  if (!kondisiBahayaTDS && notifSentTDS) {
    fb.setBool("/SmartIndoorFarming/Notifikasi/statusTDS", false);

    Serial.println("TDS kembali normal");

    notifSentTDS = false;
  }
  delay(500);
}