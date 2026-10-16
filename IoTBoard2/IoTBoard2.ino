#define RELAY1 23
#define RELAY2 22
#define RELAY3 21

const int tdsPin = 34;

#include <WiFi.h>
#include <Firebase.h>
#include <ArduinoJson.h>
#include "secrets.h"

Firebase fb(FIREBASE_URL);


//proses interval
unsigned long previousMillisSensor = 0;
const unsigned long intervalSensor = 1000;

unsigned long previousMillisFirebase = 0;
const unsigned long intervalFirebase = 2000;

unsigned long previousMillisWifiCheck = 0;
const unsigned long intervalWifiCheck = 5000;

//kontrol pompa berjalan sendiri, terpisah dari Firebase.
unsigned long previousMillisKontrol = 0;
const unsigned long intervalKontrol = 200;


//tds
float voltase = 0.0;
float nilaiTDS = 0.0;

float suhuAir = 25.0;

//jumlah sampel ganjil agar median tepat di data tengah.
const int SCOUNT = 21;

//kalibrasi tds
const float KAL_A3 = 0.0;
const float KAL_A2 = 0.0;
const float KAL_A1 = 446.8;
const float KAL_A0 = -305.3;


//aktuator 
bool statusPompaNutrisiA = false;
bool statusPompaNutrisiB = false;
bool statusPompaAir = false;


int paramTDSMin = 600;
int paramTDSMax = 800;


String mode = "MANUAL";


enum StateTDS {
  IDLE,
  DOSING,
  MIXING,
  SETTLING,   //jeda tunggu setelah mixing sebelum evaluasi
  GAGAL       //dosing dihentikan karena pengaman
};

StateTDS stateTDS = IDLE;

unsigned long stateStartTime = 0;

const unsigned long DOSING_TIME   = 3000;    // 3 detik
const unsigned long MIXING_TIME   = 30000;   // 30 detik
const unsigned long SETTLING_TIME = 15000;   // 15 detik

//pengaman: maksimal siklus dosing berturut-turut tanpa TDS
//mencapai batas minimum. Setelah itu dosing dihentikan.
const int MAX_SIKLUS_BERTURUT = 5;
int siklusBerturut = 0;

//false: HIGH = ON, LOW = OFF
//true : LOW = ON, HIGH = OFF
const bool RELAY_ACTIVE_LOW = false;

String pesanNotifTerakhir = "";

bool sudahKirimStatus = false;
bool terakhirKirimA = false;
bool terakhirKirimB = false;
bool terakhirKirimAir = false;

//batas waktu menunggu Wi-Fi saat booting.
const unsigned long WIFI_TIMEOUT_SETUP = 20000;


float bacaTDS();
int ambilMedian(int arr[], int len);

void setRelay(int pin, bool nyala);
void tampilkanStatusPompa();
void kontrolPompa();

void syncFirebase();
void kirimStatusPompaKeFirebase();
void prosesNotifikasiTDS();

void cekWifi();


void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(tdsPin, INPUT);

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);

  statusPompaNutrisiA = false;
  statusPompaNutrisiB = false;
  statusPompaAir = false;

  setRelay(RELAY1, false);
  setRelay(RELAY2, false);
  setRelay(RELAY3, false);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  //tunggu Wi-Fi maksimal 20 detik. jika gagal, sistem tetap jalan dan cekWifi() akan mencoba menyambung ulang.
  unsigned long mulaiTunggu = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - mulaiTunggu < WIFI_TIMEOUT_SETUP) {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi terhubung");
  } else {
    Serial.println("WiFi belum terhubung, lanjut tanpa Wi-Fi");
  }

  nilaiTDS = bacaTDS();
}

//lOOP

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
    Serial.print(" V | Nilai TDS: ");
    Serial.print(nilaiTDS, 2);
    Serial.println(" ppm");

    Serial.print("Mode: ");
    Serial.print(mode);
    Serial.print(" | State: ");
    switch (stateTDS) {
      case IDLE:     Serial.print("IDLE");     break;
      case DOSING:   Serial.print("DOSING");   break;
      case MIXING:   Serial.print("MIXING");   break;
      case SETTLING: Serial.print("SETTLING"); break;
      case GAGAL:    Serial.print("GAGAL");    break;
    }
    Serial.print(" | Siklus: ");
    Serial.print(siklusBerturut);
    Serial.print("/");
    Serial.print(MAX_SIKLUS_BERTURUT);
    Serial.print(" | ");
    tampilkanStatusPompa();
  }

  // Kontrol pompa jalan sendiri tiap 200 ms,
  // tidak bergantung pada Firebase/Wi-Fi.
  if (currentMillis - previousMillisKontrol >= intervalKontrol) {
    previousMillisKontrol = currentMillis;
    kontrolPompa();
  }

  if (currentMillis - previousMillisFirebase >= intervalFirebase) {
    previousMillisFirebase = currentMillis;

    if (WiFi.status() == WL_CONNECTED) {
      syncFirebase();
    }
  }
}

//relay

void setRelay(int pin, bool nyala) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(pin, nyala ? LOW : HIGH);
  } else {
    digitalWrite(pin, nyala ? HIGH : LOW);
  }
}

//median filter

int ambilMedian(int arr[], int len) {
  int dataSementara[SCOUNT];

  for (int i = 0; i < len; i++) {
    dataSementara[i] = arr[i];
  }

  for (int j = 0; j < len - 1; j++) {
    for (int i = 0; i < len - j - 1; i++) {
      if (dataSementara[i] > dataSementara[i + 1]) {
        int nilaiSementara = dataSementara[i];
        dataSementara[i] = dataSementara[i + 1];
        dataSementara[i + 1] = nilaiSementara;
      }
    }
  }

  return dataSementara[len / 2];
}

//pembacaan sensor tds

float bacaTDS() {
  int bufferTDS[SCOUNT];

  // Ambil 21 sampel tegangan dalam milivolt.
  for (int i = 0; i < SCOUNT; i++) {
    bufferTDS[i] = analogReadMilliVolts(tdsPin);
    delay(5);
  }

  // Median, lalu mV -> V.
  voltase = ambilMedian(bufferTDS, SCOUNT) / 1000.0;

  // Kompensasi suhu terhadap acuan 25 derajat Celsius.
  float koefisienTemperatur = 1.0 + 0.02 * (suhuAir - 25.0);
  float v = voltase / koefisienTemperatur;

  // Konversi tegangan ke TDS memakai koefisien kalibrasi,
  // tanpa flag, constrain, atau pemotongan nilai.
  float hasilTDS =
      KAL_A3 * v * v * v
    + KAL_A2 * v * v
    + KAL_A1 * v
    + KAL_A0;

  //batas rentang sensor sesuai spesifikasi (0-1000 ppm)
  hasilTDS = constrain(hasilTDS, 0.0, 1000.0);

  return hasilTDS;
}



void cekWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}


void tampilkanStatusPompa() {
  Serial.print("Nutrisi A: ");
  Serial.print(statusPompaNutrisiA ? "ON" : "OFF");

  Serial.print(" | Nutrisi B: ");
  Serial.print(statusPompaNutrisiB ? "ON" : "OFF");

  Serial.print(" | Mixing: ");
  Serial.println(statusPompaAir ? "ON" : "OFF");
}


void kontrolPompa() {


  if (mode != "OTOMATIS") {

    stateTDS = IDLE;
    siklusBerturut = 0;   // pindah ke manual = reset pengaman

    setRelay(RELAY1, statusPompaNutrisiA);
    setRelay(RELAY2, statusPompaNutrisiB);
    setRelay(RELAY3, statusPompaAir);

    return;
  }

  unsigned long now = millis();

  switch (stateTDS) {

    case IDLE:

      statusPompaNutrisiA = false;
      statusPompaNutrisiB = false;
      statusPompaAir = false;

      if (nilaiTDS >= paramTDSMin) {
        //target tercapai, reset penghitung pengaman.
        siklusBerturut = 0;
      }
      else if (siklusBerturut >= MAX_SIKLUS_BERTURUT) {
        stateTDS = GAGAL;
        Serial.println("PENGAMAN: dosing dihentikan, TDS tidak mencapai minimum");
      }
      else {
        statusPompaNutrisiA = true;
        statusPompaNutrisiB = true;

        siklusBerturut++;
        stateTDS = DOSING;
        stateStartTime = now;

        Serial.print("DOSING DIMULAI, siklus ke-");
        Serial.println(siklusBerturut);
      }

      break;

    // DOSING 
    case DOSING:

      statusPompaNutrisiA = true;
      statusPompaNutrisiB = true;
      statusPompaAir = false;

      if (now - stateStartTime >= DOSING_TIME) {

        statusPompaNutrisiA = false;
        statusPompaNutrisiB = false;
        statusPompaAir = true;

        stateTDS = MIXING;
        stateStartTime = now;

        Serial.println("DOSING SELESAI");
        Serial.println("MIXING DIMULAI");
      }

      break;

    // MIXING 
    case MIXING:

      statusPompaNutrisiA = false;
      statusPompaNutrisiB = false;
      statusPompaAir = true;

      if (now - stateStartTime >= MIXING_TIME) {

        statusPompaAir = false;

        stateTDS = SETTLING;
        stateStartTime = now;

        Serial.println("MIXING SELESAI");
        Serial.println("MENUNGGU PEMBACAAN STABIL");
      }

      break;

    // SETTLING 
    case SETTLING:

      statusPompaNutrisiA = false;
      statusPompaNutrisiB = false;
      statusPompaAir = false;

      if (now - stateStartTime >= SETTLING_TIME) {
        stateTDS = IDLE;
        Serial.println("EVALUASI TDS ULANG");
      }

      break;

    // GAGAL 
    case GAGAL:

      //semua pompa mati. Keluar dari state ini dengan
      //memindahkan mode ke MANUAL lalu kembali ke OTOMATIS.
      statusPompaNutrisiA = false;
      statusPompaNutrisiB = false;
      statusPompaAir = false;

      break;
  }

  setRelay(RELAY1, statusPompaNutrisiA);
  setRelay(RELAY2, statusPompaNutrisiB);
  setRelay(RELAY3, statusPompaAir);
}


void kirimStatusPompaKeFirebase() {
  //hanya di mode otomatis agar tidak menimpa perintah manual.
  if (mode != "OTOMATIS") {
    sudahKirimStatus = false;   // saat kembali ke otomatis, kirim ulang
    return;
  }

  if (!sudahKirimStatus || statusPompaNutrisiA != terakhirKirimA) {
    fb.setBool("/SmartIndoorFarming/Aktuator/pompaNutrisiA", statusPompaNutrisiA);
    terakhirKirimA = statusPompaNutrisiA;
  }

  if (!sudahKirimStatus || statusPompaNutrisiB != terakhirKirimB) {
    fb.setBool("/SmartIndoorFarming/Aktuator/pompaNutrisiB", statusPompaNutrisiB);
    terakhirKirimB = statusPompaNutrisiB;
  }

  if (!sudahKirimStatus || statusPompaAir != terakhirKirimAir) {
    fb.setBool("/SmartIndoorFarming/Aktuator/pompaAir", statusPompaAir);
    terakhirKirimAir = statusPompaAir;
  }

  sudahKirimStatus = true;
}


void prosesNotifikasiTDS() {
  bool kondisiNotifikasi = false;
  String pesanTDS = "";

  // Pengaman dosing punya prioritas tertinggi.
  if (mode == "OTOMATIS" && stateTDS == GAGAL) {
    kondisiNotifikasi = true;
    pesanTDS =
        "Dosing dihentikan: TDS tidak mencapai batas minimum "
        "setelah beberapa siklus. Periksa probe, pompa, dan larutan.";
  }
  else if (nilaiTDS < paramTDSMin) {
    kondisiNotifikasi = true;
    pesanTDS =
        "TDS rendah. Konsentrasi nutrisi "
        "belum mencapai batas minimum.";
  }
  else if (nilaiTDS >= paramTDSMax) {
    kondisiNotifikasi = true;
    pesanTDS =
        "TDS telah mencapai batas maksimum "
        "yang ditetapkan.";
  }

  if (kondisiNotifikasi && pesanTDS != pesanNotifTerakhir) {
    fb.setString("/SmartIndoorFarming/Notifikasi/pesanTDS", pesanTDS);
    fb.setBool("/SmartIndoorFarming/Notifikasi/statusTDS", true);
    pesanNotifTerakhir = pesanTDS;
  }

  if (!kondisiNotifikasi && pesanNotifTerakhir != "") {
    fb.setBool("/SmartIndoorFarming/Notifikasi/statusTDS", false);
    pesanNotifTerakhir = "";
  }
}


void syncFirebase() {
  String jsonRoot;

  int codeRoot = fb.getJson("/SmartIndoorFarming", jsonRoot);

  //jika gagal, status terakhir dipertahankan
  //(kontrol pompa tetap jalan sendiri di loop).
  if (codeRoot != 200) {
    return;
  }

  JsonDocument docRoot;

  DeserializationError errorJson = deserializeJson(docRoot, jsonRoot);

  if (errorJson) {
    return;
  }

  // MODE POMPA 
  JsonObject aktuator = docRoot["Aktuator"];

  mode = aktuator["modePompa"] | mode;
  mode.toUpperCase();

  //status pompa dari Firebase dipakai selain di mode otomatis.
  if (mode != "OTOMATIS") {
    statusPompaNutrisiA = aktuator["pompaNutrisiA"] | statusPompaNutrisiA;
    statusPompaNutrisiB = aktuator["pompaNutrisiB"] | statusPompaNutrisiB;
    statusPompaAir = aktuator["pompaAir"] | statusPompaAir;
  }

  // PARAMETER TDS 
  JsonObject parameter = docRoot["Parameter"];

  paramTDSMin = parameter["tdsMin"] | paramTDSMin;
  paramTDSMax = parameter["tdsMax"] | paramTDSMax;

  //data sensor selalu dikirim, apa pun kondisi parameternya.
  fb.setFloat("/SmartIndoorFarming/Sensor/tds", nilaiTDS);

  //parameter tidak valid: lewati status pompa dan notifikasi.
  if (paramTDSMax <= paramTDSMin) {
    Serial.println("Parameter TDS tidak valid (max <= min)");
    return;
  }

  kirimStatusPompaKeFirebase();
  prosesNotifikasiTDS();
}
