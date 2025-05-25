// 1. Sertakan Library yang Dibutuhkan
// Library adalah kumpulan kode yang sudah jadi untuk melakukan tugas tertentu.
// Kita menggunakannya agar tidak perlu menulis semuanya dari nol.

#include <WiFi.h> // Untuk menghubungkan ESP32 ke jaringan Wi-Fi.
#include <WebServer.h> // Untuk membuat ESP32 bisa diakses seperti website kecil melalui browser.
#include <PubSubClient.h> // Untuk mengirim data ke layanan MQTT (semacam pesan instan untuk perangkat).
#include "DHT.h" // Untuk membaca data dari sensor suhu dan kelembapan DHT.
                 // Pastikan file DHT.h ada di sketch Anda atau di library path.
#include <ESP32Servo.h> // Untuk mengontrol motor servo (misalnya, untuk palang parkir).

// --- Konfigurasi Jaringan Wi-Fi ---
// Di sini kita mengatur nama dan password Wi-Fi yang akan dihubungkan oleh ESP32.
char ssid[] = "Wokwi-GUEST";      // SSID (nama) jaringan Wi-Fi. Ganti jika Anda menggunakan jaringan sendiri.
char password[] = "";             // Password Wi-Fi. Untuk Wokwi-GUEST biasanya kosong.

// --- Definisi Pin ---
// Pin adalah kaki-kaki pada ESP32 yang terhubung ke komponen elektronik lain.
// Kita memberi nama pada nomor pin agar kode lebih mudah dibaca.

// Sensor Suhu dan Kelembapan DHT11/DHT22
#define DHTPIN 4                  // Sensor DHT terhubung ke pin GPIO nomor 4.
#define DHTTYPE DHT22             // Jenis sensor DHT yang digunakan (DHT22 atau DHT11).
                                  // PENTING: Pastikan DHT terhubung dengan benar.

// Sensor Jarak Ultrasonik HC-SR04 untuk Slot Parkir 1
#define TRIG_PIN_S1 23            // Pin TRIG (pemicu) HC-SR04 Slot 1 terhubung ke GPIO 23.
#define ECHO_PIN_S1 22            // Pin ECHO (penerima) HC-SR04 Slot 1 terhubung ke GPIO 22.

// Sensor Jarak Ultrasonik HC-SR04 untuk Slot Parkir 2
#define TRIG_PIN_S2 21            // Pin TRIG HC-SR04 Slot 2 terhubung ke GPIO 21.
#define ECHO_PIN_S2 19            // Pin ECHO HC-SR04 Slot 2 terhubung ke GPIO 19.

// Sensor Jarak Ultrasonik HC-SR04 untuk Gerbang
#define TRIG_PIN_G 18             // Pin TRIG HC-SR04 Gerbang terhubung ke GPIO 18.
#define ECHO_PIN_G 5              // Pin ECHO HC-SR04 Gerbang terhubung ke GPIO 5.

// Sensor Cahaya LDR (Light Dependent Resistor)
#define LDR_PIN 32                // Sensor LDR terhubung ke pin ADC1_CH4 (GPIO 32).
                                  // ADC (Analog-to-Digital Converter) mengubah sinyal analog (cahaya) jadi digital.

// Motor Servo (Palang Parkir)
#define SERVO_PIN 13              // Motor Servo terhubung ke GPIO 13.

// Buzzer (Penghasil Suara)
#define BUZZER_PIN 27             // Buzzer terhubung ke GPIO 27.

// --- Inisialisasi Objek ---
// Objek adalah representasi dari komponen atau layanan dalam kode.
// Kita membuat objek agar bisa menggunakan fungsi-fungsi dari library yang telah disertakan.

DHT dht(DHTPIN, DHTTYPE);         // Membuat objek 'dht' untuk sensor DHT, memberitahunya pin dan tipenya.
Servo gateServo;                  // Membuat objek 'gateServo' untuk motor servo.
WebServer server(80);             // Membuat objek 'server' yang akan berjalan di port 80 (port standar untuk web).

WiFiClient espClient;             // Membuat objek klien Wi-Fi, diperlukan oleh PubSubClient.
PubSubClient mqttClient(espClient); // Membuat objek 'mqttClient' untuk komunikasi MQTT, menggunakan koneksi Wi-Fi dari 'espClient'.

// --- Konfigurasi MQTT ---
// MQTT adalah protokol untuk mengirim pesan antar perangkat, sering digunakan di IoT.
const char* mqtt_server = "broker.hivemq.com"; // Alamat server MQTT publik yang akan kita gunakan.
const int mqtt_port = 1883;                   // Port standar untuk koneksi MQTT.
const char* mqtt_topic_status = "parkir/upr/status/data"; // Topik (seperti channel) tempat kita akan mengirim data status parkir.
                                                          // Sebaiknya buat topik unik agar tidak bentrok dengan pengguna lain.
const char* mqtt_client_id_base = "esp32-smartpark-upr-"; // Bagian awal dari ID unik untuk ESP32 ini saat konek ke MQTT.
                                                        // ID lengkap akan ditambahi angka acak.

// --- Variabel Global ---
// Variabel adalah tempat untuk menyimpan data yang bisa berubah-ubah selama program berjalan.
// Variabel global bisa diakses dari mana saja dalam kode.

float temperatureC = -999.0;      // Menyimpan suhu dalam Celsius. Diisi -999.0 sebagai tanda belum ada data valid.
float humidity = -999.0;          // Menyimpan kelembapan. Diisi -999.0 sebagai tanda belum ada data valid.
long distanceS1_cm;               // Menyimpan jarak dari sensor Slot 1 dalam cm.
long distanceS2_cm;               // Menyimpan jarak dari sensor Slot 2 dalam cm.
long distanceG_cm;                // Menyimpan jarak dari sensor Gerbang dalam cm.
int ldrValue;                     // Menyimpan nilai mentah dari sensor LDR (0-4095).
int lightPercent;                 // Menyimpan persentase tingkat cahaya (0-100%).

bool slot1_occupied = false;      // Status Slot 1: true jika terisi, false jika kosong.
bool slot2_occupied = false;      // Status Slot 2: true jika terisi, false jika kosong.
bool vehicle_at_gate = false;     // Status Gerbang: true jika ada kendaraan, false jika tidak ada.
int available_slots = 2;          // Jumlah slot parkir yang tersedia. Awalnya 2.
int current_servo_angle = 0;      // Menyimpan posisi sudut servo (palang) saat ini.

// --- Ambang Batas Sensor ---
// Nilai batas untuk menentukan suatu kondisi. Misalnya, jarak berapa cm dianggap ada mobil.
const int SLOT_OCCUPIED_DISTANCE_CM = 20; // Jika jarak < 20cm, slot dianggap terisi. Sesuaikan nilainya.
const int GATE_VEHICLE_DISTANCE_CM = 30;  // Jika jarak < 30cm, dianggap ada kendaraan di gerbang. Sesuaikan.

// --- Pengaturan Servo ---
// Sudut untuk posisi terbuka dan tertutupnya palang parkir.
int gateOpenAngle = 90;           // Sudut servo saat palang terbuka (misalnya 90 derajat).
int gateClosedAngle = 0;          // Sudut servo saat palang tertutup (misalnya 0 derajat).

// --- Timer untuk tugas periodik ---
// Digunakan untuk menjalankan beberapa tugas secara berkala tanpa menghentikan program utama.
unsigned long previousMillisSensors = 0; // Menyimpan waktu terakhir sensor dibaca.
unsigned long previousMillisMQTT = 0;    // Menyimpan waktu terakhir data dikirim via MQTT.
const long intervalSensors = 2500;       // Jeda waktu untuk membaca sensor (2500 ms = 2.5 detik).
const long intervalMQTT = 5000;          // Jeda waktu untuk mengirim data MQTT (5000 ms = 5 detik).

// --- FUNGSI SETUP ---
// Fungsi setup() dijalankan sekali saat ESP32 pertama kali menyala atau di-reset.
void setup() {
  Serial.begin(115200);           // Memulai komunikasi serial dengan komputer (untuk menampilkan pesan debug) pada kecepatan 115200 bps.
  delay(1000);                    // Jeda 1 detik untuk memberi waktu serial monitor siap.
  Serial.println("Booting Smart Parking System (UPR Version)..."); // Pesan saat mulai.

  dht.begin();                    // Menginisialisasi sensor DHT.
  delay(100);                     // Jeda singkat setelah inisialisasi DHT.

  gateServo.attach(SERVO_PIN);    // Memberitahu objek servo pin mana yang terhubung ke motor servo.
  gateServo.write(gateClosedAngle); // Mengatur posisi awal servo ke posisi tertutup.
  current_servo_angle = gateClosedAngle; // Menyimpan sudut awal servo.

  // Mengatur mode pin untuk sensor ultrasonik: TRIG sebagai OUTPUT (mengirim sinyal), ECHO sebagai INPUT (menerima sinyal).
  pinMode(TRIG_PIN_S1, OUTPUT); pinMode(ECHO_PIN_S1, INPUT);
  pinMode(TRIG_PIN_S2, OUTPUT); pinMode(ECHO_PIN_S2, INPUT);
  pinMode(TRIG_PIN_G, OUTPUT); pinMode(ECHO_PIN_G, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);    // Mengatur pin buzzer sebagai OUTPUT.
  digitalWrite(BUZZER_PIN, LOW);  // Memastikan buzzer mati di awal.

  connectToWiFi();                // Memanggil fungsi untuk menghubungkan ESP32 ke Wi-Fi.

  // Membuat ID Klien MQTT yang unik dengan menambahkan angka acak (hexadecimal) ke nama dasar.
  String clientId = String(mqtt_client_id_base) + String(random(0xffff), HEX);
  mqttClient.setServer(mqtt_server, mqtt_port); // Memberitahu klien MQTT alamat server dan portnya.
  // mqttClient.setCallback(callback); // Baris ini dikomentari, artinya kita tidak akan menerima pesan dari MQTT saat ini.
                                      // Jika ingin menerima, fungsi 'callback' perlu dibuat.

  // Mengatur halaman web yang akan ditampilkan oleh server ESP32.
  server.on("/", HTTP_GET, handleRoot);           // Jika browser meminta alamat utama ("/"), jalankan fungsi handleRoot.
  server.on("/data.json", HTTP_GET, handleDataJson); // Jika browser meminta "/data.json", jalankan fungsi handleDataJson.
  server.begin();                 // Memulai web server.
  Serial.println("HTTP server started"); // Pesan bahwa server web sudah siap.
}

// --- FUNGSI LOOP UTAMA ---
// Fungsi loop() dijalankan berulang-ulang setelah setup() selesai.
void loop() {
  server.handleClient();          // Mengecek apakah ada permintaan masuk dari browser ke web server.

  unsigned long currentMillis = millis(); // Mendapatkan waktu saat ini (sejak ESP32 menyala) dalam milidetik.

  // Logika untuk membaca sensor secara periodik
  if (currentMillis - previousMillisSensors >= intervalSensors) { // Jika sudah waktunya (intervalSensors telah berlalu)
    previousMillisSensors = currentMillis; // Simpan waktu saat ini sebagai waktu terakhir pembacaan sensor.
    readAllSensors();             // Panggil fungsi untuk membaca semua data sensor.
    processParkingLogic();        // Panggil fungsi untuk memproses logika parkir berdasarkan data sensor.
  }

  // Logika untuk koneksi dan pengiriman data MQTT
  if (WiFi.status() == WL_CONNECTED) { // Hanya lakukan jika Wi-Fi terhubung.
    if (!mqttClient.connected()) {   // Jika klien MQTT tidak terhubung,
      reconnectMQTT();              // coba hubungkan kembali.
    }
    mqttClient.loop();              // Penting untuk menjaga koneksi MQTT dan memproses pesan masuk/keluar.

    // Jika sudah waktunya mengirim data MQTT dan klien MQTT terhubung
    if (currentMillis - previousMillisMQTT >= intervalMQTT && mqttClient.connected()) {
      previousMillisMQTT = currentMillis; // Simpan waktu saat ini sebagai waktu terakhir pengiriman MQTT.
      publishMQTTData();            // Panggil fungsi untuk mengirim data ke MQTT broker.
    }
  }
}

// --- IMPLEMENTASI FUNGSI-FUNGSI ---
// Di sini adalah penjelasan detail dari fungsi-fungsi yang dipanggil di setup() dan loop().

// Fungsi untuk menghubungkan ESP32 ke jaringan Wi-Fi
void connectToWiFi() {
  Serial.print("Connecting to WiFi: "); Serial.println(ssid); // Tampilkan pesan sedang menghubungkan ke Wi-Fi 'ssid'.
  WiFi.mode(WIFI_STA);            // Mengatur ESP32 sebagai Station (klien Wi-Fi), bukan Access Point.
  WiFi.begin(ssid, password);     // Mulai proses koneksi ke Wi-Fi dengan ssid dan password yang diberikan.
  int attempt = 0;
  // Tunggu hingga terhubung, dengan batas maksimal 20 kali percobaan (sekitar 10 detik).
  while (WiFi.status() != WL_CONNECTED && attempt < 20) {
    delay(500); Serial.print("."); attempt++; // Tunggu 0.5 detik, cetak titik sebagai indikator proses.
  }
  if (WiFi.status() == WL_CONNECTED) { // Jika berhasil terhubung
    Serial.println("\nWiFi connected!"); // Tampilkan pesan berhasil.
    Serial.print("IP Address for Web Server: http://"); Serial.println(WiFi.localIP()); // Tampilkan alamat IP ESP32.
                                                                                       // Alamat ini digunakan untuk mengakses web server dari browser.
  } else { // Jika gagal terhubung setelah 20 percobaan
    Serial.println("\nFailed to connect to WiFi."); // Tampilkan pesan gagal.
  }
}

// Fungsi untuk menghubungkan kembali ke MQTT broker jika koneksi terputus
void reconnectMQTT() {
  String clientId = String(mqtt_client_id_base) + String(random(0xffff), HEX); // Buat ID klien yang unik lagi.
  // Ulangi selama belum terhubung ke MQTT dan Wi-Fi masih aktif.
  while (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    Serial.print("Attempting MQTT connection to "); Serial.print(mqtt_server);
    Serial.print(" as "); Serial.print(clientId); Serial.print("...");
    // Coba hubungkan ke MQTT broker dengan ID klien yang dibuat.
    if (mqttClient.connect(clientId.c_str())) { // .c_str() mengubah String Arduino menjadi format yang diterima fungsi connect.
      Serial.println("connected"); // Jika berhasil terhubung.
      // Di sini bisa ditambahkan untuk subscribe ke topik jika perlu menerima pesan.
      // Contoh: mqttClient.subscribe("parkir/upr/command");
    } else { // Jika gagal terhubung.
      Serial.print("failed, rc="); Serial.print(mqttClient.state()); // Tampilkan kode error dari MQTT.
      Serial.println(" try again in 5 seconds");
      delay(5000); // Tunggu 5 detik sebelum mencoba lagi.
    }
  }
}

// Fungsi untuk membaca jarak dari sensor ultrasonik HC-SR04
// Menerima pin TRIG dan ECHO sebagai parameter.
long readUltrasonic(int trigPin, int echoPin) {
  // Prosedur standar untuk membaca sensor HC-SR04:
  digitalWrite(trigPin, LOW);  // Pastikan pin TRIG mati (LOW) selama 2 mikrodetik.
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); // Nyalakan pin TRIG (HIGH) selama 10 mikrodetik untuk mengirimkan gelombang suara.
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);  // Matikan lagi pin TRIG.

  // Baca durasi pin ECHO menjadi HIGH (waktu pantulan gelombang suara kembali).
  // Timeout 30000 mikrodetik (30 ms) untuk mencegah program macet jika tidak ada pantulan.
  long duration = pulseIn(echoPin, HIGH, 30000);

  if (duration == 0) return 999; // Jika durasi 0 (timeout atau tidak ada objek terdeteksi), kembalikan 999 cm (indikasi error/jauh).
  // Hitung jarak: (durasi * kecepatan suara (0.0343 cm/µs)) / 2 (karena suara bolak-balik).
  return (duration * 0.0343) / 2;
}

// Fungsi untuk membaca semua data dari sensor-sensor yang terpasang
void readAllSensors() {
  // Baca suhu dan kelembapan dari sensor DHT.
  float newHumidity = dht.readHumidity();
  float newTemperatureC = dht.readTemperature();

  // Cek apakah pembacaan humidity valid (bukan NaN - Not a Number).
  if (!isnan(newHumidity)) {
    humidity = newHumidity; // Jika valid, simpan nilainya.
  } else {
    Serial.println("Failed to read humidity from DHT sensor!"); // Jika gagal, tampilkan pesan error.
    // humidity akan tetap memakai nilai lama atau -999.0 jika ini pembacaan pertama yg gagal.
  }
  // Cek apakah pembacaan temperature valid.
  if (!isnan(newTemperatureC)) {
    temperatureC = newTemperatureC; // Jika valid, simpan nilainya.
  } else {
    Serial.println("Failed to read temperature from DHT sensor!"); // Jika gagal, tampilkan pesan error.
  }

  // Baca nilai analog dari sensor LDR.
  ldrValue = analogRead(LDR_PIN); // Nilai mentah antara 0 (gelap total) hingga 4095 (terang maksimal untuk ESP32 12-bit ADC).
  // Konfigurasi LDR diasumsikan: VCC --- LDR --- ADC_PIN --- 10K_Resistor --- GND
  // Artinya: Semakin terang, resistansi LDR makin kecil, tegangan di ADC_PIN makin TINGGI.
  // Semakin gelap, resistansi LDR makin besar, tegangan di ADC_PIN makin RENDAH.
  // Ubah nilai mentah LDR (0-4095) menjadi persentase (0-100%).
  // Jika LDR Anda terbalik (misal, gelap = nilai tinggi), sesuaikan pemetaan ini atau rangkaiannya.
  lightPercent = map(ldrValue, 0, 4095, 0, 100); // Di sini, 0 = gelap, 100 = terang.

  // Baca jarak dari semua sensor ultrasonik.
  distanceS1_cm = readUltrasonic(TRIG_PIN_S1, ECHO_PIN_S1); // Jarak slot 1.
  distanceS2_cm = readUltrasonic(TRIG_PIN_S2, ECHO_PIN_S2); // Jarak slot 2.
  distanceG_cm = readUltrasonic(TRIG_PIN_G, ECHO_PIN_G);   // Jarak di gerbang.

  // Tampilkan semua hasil pembacaan sensor ke Serial Monitor untuk debugging.
  Serial.println("--- Sensor Readings ---");
  Serial.print("Temp: "); Serial.print(temperatureC); Serial.print(" *C, Hum: "); Serial.print(humidity); Serial.println(" %");
  Serial.print("Light: "); Serial.print(lightPercent); Serial.println("% (LDR Raw: " + String(ldrValue) + ")");
  Serial.print("Slot 1 Dist: "); Serial.print(distanceS1_cm); Serial.println(" cm");
  Serial.print("Slot 2 Dist: "); Serial.print(distanceS2_cm); Serial.println(" cm");
  Serial.print("Gate Dist: "); Serial.print(distanceG_cm); Serial.println(" cm");
}

// Fungsi untuk memproses logika parkir berdasarkan data sensor
void processParkingLogic() {
  // Tentukan apakah slot 1 terisi.
  // Slot terisi jika sensor mendeteksi objek (distanceS1_cm > 0, bukan error/timeout)
  // DAN jaraknya kurang dari ambang batas (SLOT_OCCUPIED_DISTANCE_CM).
  slot1_occupied = (distanceS1_cm > 0 && distanceS1_cm < SLOT_OCCUPIED_DISTANCE_CM);
  // Tentukan apakah slot 2 terisi, dengan logika yang sama.
  slot2_occupied = (distanceS2_cm > 0 && distanceS2_cm < SLOT_OCCUPIED_DISTANCE_CM);

  // Hitung jumlah slot yang tersedia.
  available_slots = 0;
  if (!slot1_occupied) available_slots++; // Jika slot 1 tidak terisi, tambah jumlah slot tersedia.
  if (!slot2_occupied) available_slots++; // Jika slot 2 tidak terisi, tambah jumlah slot tersedia.

  // Tentukan apakah ada kendaraan di gerbang.
  // Ada kendaraan jika sensor gerbang mendeteksi objek (distanceG_cm > 0)
  // DAN jaraknya kurang dari ambang batas (GATE_VEHICLE_DISTANCE_CM).
  vehicle_at_gate = (distanceG_cm > 0 && distanceG_cm < GATE_VEHICLE_DISTANCE_CM);

  // Tampilkan status logika parkir ke Serial Monitor.
  Serial.println("--- Parking Logic ---");
  Serial.print("Slot 1 Occupied: "); Serial.println(slot1_occupied ? "YES" : "NO"); // '?:' adalah if-else singkat.
  Serial.print("Slot 2 Occupied: "); Serial.println(slot2_occupied ? "YES" : "NO");
  Serial.print("Available Slots: "); Serial.println(available_slots);
  Serial.print("Vehicle at Gate: "); Serial.println(vehicle_at_gate ? "YES" : "NO");

  // Logika untuk mengontrol palang parkir (servo) dan buzzer.
  if (vehicle_at_gate) { // Jika ada kendaraan di gerbang
    if (available_slots > 0) { // Dan jika ada slot parkir yang tersedia
      if (current_servo_angle == gateClosedAngle) { // Dan jika palang saat ini tertutup
        Serial.println("Vehicle at gate, Opening gate...");
        gateServo.write(gateOpenAngle);         // Buka palang.
        current_servo_angle = gateOpenAngle;    // Update status sudut servo.
        digitalWrite(BUZZER_PIN, HIGH); delay(200); digitalWrite(BUZZER_PIN, LOW); // Bunyikan buzzer sebentar.
      }
      // Jika palang sudah terbuka, tidak melakukan apa-apa, biarkan terbuka.
    } else { // Jika ada kendaraan di gerbang TAPI TIDAK ADA slot tersedia (parkir penuh)
      Serial.println("Vehicle at gate, but PARKING FULL!");
      // Bunyikan buzzer 3 kali untuk menandakan parkir penuh.
      for(int i=0; i<3; i++){
        digitalWrite(BUZZER_PIN, HIGH); delay(150);
        digitalWrite(BUZZER_PIN, LOW); delay(150);
      }
      // Palang tetap tertutup jika memang sedang tertutup.
    }
  } else { // Jika TIDAK ADA kendaraan di gerbang
    // Logika tambahan: jika palang terbuka dan tidak ada mobil di gerbang, tutup palang.
    // Ini adalah implementasi sederhana. Bisa dikembangkan agar lebih pintar,
    // misal menunggu beberapa detik setelah mobil lewat sensor gerbang.
    if (current_servo_angle == gateOpenAngle) { // Jika palang saat ini terbuka
      Serial.println("No vehicle at gate (or vehicle passed), Closing gate...");
      gateServo.write(gateClosedAngle);       // Tutup palang.
      current_servo_angle = gateClosedAngle;  // Update status sudut servo.
    }
  }
}

// Fungsi untuk mempublikasikan (mengirim) data sensor ke MQTT broker
void publishMQTTData() {
  // Siapkan data suhu dan kelembapan dalam format String.
  // Jika nilai masih -999.0 (tidak valid), kirim "N/A". Jika valid, format dengan 1 angka desimal.
  String tempStr = (temperatureC == -999.0) ? "\"N/A\"" : String(temperatureC, 1);
  String humStr = (humidity == -999.0) ? "\"N/A\"" : String(humidity, 1);

  // Buat payload (isi pesan) dalam format JSON.
  // JSON adalah format teks yang mudah dibaca manusia dan mesin, sering dipakai untuk transfer data.
  // Format: {"nama_data":"nilai_data", "nama_lain":nilai_lain_angka}
  String payload = "{";
  payload += "\"slot1_status\":\"" + String(slot1_occupied ? "TERISI" : "KOSONG") + "\",";
  payload += "\"slot2_status\":\"" + String(slot2_occupied ? "TERISI" : "KOSONG") + "\",";
  payload += "\"available_slots\":" + String(available_slots) + ",";
  payload += "\"gate_vehicle\":\"" + String(vehicle_at_gate ? "ADA" : "TIDAK ADA") + "\",";
  payload += "\"temperature\":" + tempStr + ",";  // Perhatikan tidak ada tanda kutip ganda untuk angka di JSON.
  payload += "\"humidity\":" + humStr + ",";      // tempStr dan humStr sudah mengandung kutip jika "N/A".
  payload += "\"light_percent\":" + String(lightPercent);
  payload += "}";

  Serial.print("Publishing MQTT data to "); Serial.print(mqtt_topic_status); Serial.print(": "); Serial.println(payload);
  // Kirim payload ke topik MQTT yang ditentukan. Parameter 'true' artinya pesan ini 'retained'
  // (broker akan menyimpan pesan terakhir ini untuk klien baru yang subscribe).
  if (!mqttClient.publish(mqtt_topic_status, payload.c_str(), true)) { // .c_str() diperlukan.
     Serial.println("MQTT Publish Failed."); // Jika gagal mengirim.
  } else {
     Serial.println("MQTT Published Successfully."); // Jika berhasil.
  }
}

// Fungsi yang dipanggil ketika browser meminta alamat "/data.json"
// Fungsi ini mengirimkan data sensor dalam format JSON ke browser.
void handleDataJson() {
  // Siapkan data suhu dan kelembapan untuk JSON.
  // Jika tidak valid, kirim 'null' (standar JSON untuk nilai kosong/tidak ada).
  String tempStrJson = (temperatureC == -999.0) ? "null" : String(temperatureC, 1);
  String humStrJson = (humidity == -999.0) ? "null" : String(humidity, 1);

  // Buat string JSON. Mirip dengan MQTT payload, tapi bisa ditambah info lain jika perlu.
  String json = "{";
  json += "\"slot1_status\":\"" + String(slot1_occupied ? "TERISI" : "KOSONG") + "\",";
  json += "\"slot1_distance\":" + String(distanceS1_cm) + ","; // Jarak mentah slot 1
  json += "\"slot2_status\":\"" + String(slot2_occupied ? "TERISI" : "KOSONG") + "\",";
  json += "\"slot2_distance\":" + String(distanceS2_cm) + ","; // Jarak mentah slot 2
  json += "\"available_slots\":" + String(available_slots) + ",";
  json += "\"gate_vehicle\":\"" + String(vehicle_at_gate ? "ADA" : "TIDAK ADA") + "\",";
  json += "\"gate_distance\":" + String(distanceG_cm) + ",";   // Jarak mentah gerbang
  json += "\"servo_angle\":" + String(current_servo_angle) + ","; // Sudut servo saat ini
  json += "\"temperature\":" + tempStrJson + ",";
  json += "\"humidity\":" + humStrJson + ",";
  json += "\"light_percent\":" + String(lightPercent) + ",";
  json += "\"ldr_raw\":" + String(ldrValue); // Nilai mentah LDR
  json += "}";
  // Kirim respons ke browser: kode 200 (OK), tipe konten "application/json", dan isi JSON nya.
  server.send(200, "application/json", json);
}

// Fungsi yang dipanggil ketika browser meminta alamat utama ("/")
// Fungsi ini membuat dan mengirim halaman HTML ke browser.
void handleRoot() {
  // Tentukan kondisi cahaya berdasarkan persentase.
  String lightConditionStr;
  if (lightPercent < 30) {
    lightConditionStr = "Gelap";
  } else if (lightPercent < 70) {
    lightConditionStr = "Redup";
  } else {
    lightConditionStr = "Terang";
  }

  // Tentukan warna latar belakang dan status awal untuk JavaScript berdasarkan status slot.
  String slot1_color_class = slot1_occupied ? "bg-red-700 cursor-default" : "bg-green-600"; // Merah jika terisi, hijau jika kosong.
  String slot1_initial_js_occupied = slot1_occupied ? "true" : "false"; // Status awal untuk JavaScript.
  String slot2_color_class = slot2_occupied ? "bg-red-700 cursor-default" : "bg-green-600";
  String slot2_initial_js_occupied = slot2_occupied ? "true" : "false";

  // Mulai membuat string HTML. Ini adalah cara manual membuat halaman web.
  // HTML adalah bahasa untuk menstrukturkan konten halaman web.
  String html = "<!DOCTYPE html><html lang=\"en\">"; // Deklarasi standar HTML5.
  html += "<head>"; // Bagian 'kepala' dari HTML, berisi info meta, judul, dan link ke style/script.
  html += "<meta charset=\"UTF-8\" />"; // Pengaturan karakter encoding.
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />"; // Untuk tampilan responsif di berbagai perangkat.
  html += "<title>Parkiran Pintar</title>"; // Judul yang muncul di tab browser.
  html += "<script src=\"https://cdn.tailwindcss.com\"></script>"; // Menggunakan Tailwind CSS dari CDN untuk styling cepat.
                                                                 // Tailwind menyediakan kelas-kelas siap pakai (misal 'bg-red-700').
  html += "<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.3/css/all.min.css\" />"; // Untuk ikon (misal ikon mobil).
  html += "<style>"; // CSS kustom (Cascading Style Sheets) untuk mengatur tampilan lebih detail.
  html += "@import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;700&display=swap');"; // Impor font 'Inter'.
  html += "body, html { margin: 0; padding: 0; height: 100%; font-family: 'Inter', sans-serif; background-color: #f3f4f6; }"; // Style dasar untuk body dan html.
  // CSS untuk animasi popup.
  html += ".popup-fade-in { animation: fadeInScale 0.3s ease forwards; }";
  html += ".popup-fade-out { animation: fadeOutScale 0.3s ease forwards; }";
  html += "@keyframes fadeInScale { 0% { opacity: 0; transform: scale(0.8); } 100% { opacity: 1; transform: scale(1); } }";
  html += "@keyframes fadeOutScale { 0% { opacity: 1; transform: scale(1); } 100% { opacity: 0; transform: scale(0.8); } }";
  // CSS untuk animasi pemilihan slot.
  html += ".slot-select-animate { animation: slotSelectPulse 0.4s ease forwards; }";
  html += "@keyframes slotSelectPulse { 0% { box-shadow: 0 0 0 0 rgba(252, 211, 77, 0.7); } 50% { box-shadow: 0 0 10px 6px rgba(252, 211, 77, 0.7); } 100% { box-shadow: 0 0 0 0 rgba(252, 211, 77, 0); } }";
  // CSS untuk tanda seru di popup.
  html += "#popupExclamation { position: absolute; top: -40px; left: 50%; transform: translateX(-50%); background-color: #facc15; color: #92400e; width: 56px; height: 56px; border-radius: 9999px; display: flex; justify-content: center; align-items: center; font-size: 32px; font-weight: 700; box-shadow: 0 0 10px 2px rgba(250, 204, 21, 0.7); z-index: 60; user-select: none; }";
  html += "</style>";
  html += "</head>";
  html += "<body class=\"flex flex-col bg-gray-100 h-full w-full relative\">"; // Awal dari 'tubuh' halaman web, tempat konten utama.
  // Konten utama, diatur dalam sebuah div dengan background gelap.
  html += "<div class=\"flex flex-col bg-gray-900 text-white h-full w-full p-4\">";
  // Bagian header halaman.
  html += "<div class=\"flex justify-between items-center mb-3\">";
  html += "<div><h1 class=\"font-extrabold text-[16px] leading-tight\">Selamat Datang!</h1><p class=\"text-gray-300 text-[11px]\">Di Parkiran Pintar UPR</p></div>";
  html += "<i class=\"fas fa-car text-white text-[16px]\"></i></div>"; // Ikon mobil.

  // Bagian untuk menampilkan data sensor (suhu, kelembapan, cahaya).
  html += "<div class=\"flex justify-between mb-3\">";
  // Box Suhu
  html += "<div class=\"flex flex-col justify-center items-center text-center rounded-lg shadow-md w-[80px] h-[80px]\" style=\"background: linear-gradient(135deg, #4ade80 0%, #22d3ee 100%);\">";
  html += "<p class=\"text-black font-semibold text-[16px] leading-none\">" + ((temperatureC == -999.0) ? "N/A" : String(temperatureC,1)) + " &deg;C</p><p class=\"text-black text-[10px] mt-0.5\">Suhu</p></div>";
  // Box Kelembapan
  html += "<div class=\"flex flex-col justify-center items-center text-center rounded-lg shadow-md w-[80px] h-[80px]\" style=\"background: linear-gradient(135deg, #22c55e 0%, #bef264 100%);\">";
  html += "<p class=\"text-black font-semibold text-[16px] leading-none\">" + ((humidity == -999.0) ? "N/A" : String(humidity,1)) + " %</p><p class=\"text-black text-[10px] mt-0.5\">Kelembapan</p></div>";
  // Box Kondisi Cahaya
  html += "<div class=\"flex flex-col justify-center items-center text-center rounded-lg shadow-md w-[80px] h-[80px]\" style=\"background: linear-gradient(135deg, #facc15 0%, #eab308 50%, #ca8a04 100%);\">";
  html += "<p class=\"text-black font-semibold text-[16px] leading-none\">" + lightConditionStr + "</p><p class=\"text-black text-[10px] mt-0.5\">Kondisi Cahaya (" + String(lightPercent) + "%)</p></div></div>";

  // Bagian untuk menampilkan slot parkir.
  html += "<div class=\"font-extrabold text-[14px] mb-2 text-center\">SLOT PARKIR (" + String(available_slots) + " tersedia)</div>";
  html += "<div class=\"flex justify-center space-x-6 mb-1\">";
  // Slot 1: 'id="slot1"' digunakan oleh JavaScript. Kelas CSS menentukan tampilan awal.
  html += "<div id=\"slot1\" class=\"w-[48px] h-[48px] rounded-md shadow-md cursor-pointer " + slot1_color_class + "\"></div>";
  // Slot 2
  html += "<div id=\"slot2\" class=\"w-[48px] h-[48px] rounded-md shadow-md cursor-pointer " + slot2_color_class + "\"></div></div>";
  html += "<div class=\"flex justify-center space-x-6 text-white text-[12px] font-semibold -mt-1\"><span class=\"w-[48px] inline-block text-center leading-[48px]\">Slot 1</span><span class=\"w-[48px] inline-block text-center leading-[48px]\">Slot 2</span></div>";
  
  // Bagian info gerbang.
  html += "<div class=\"font-extrabold text-[14px] mb-2 mt-4 text-center\">INFO GERBANG</div>";
  html += "<div class=\"bg-gray-800 rounded-md p-3 text-sm shadow-inner\">";
  // 'id' di span ini digunakan oleh JavaScript untuk update status.
  html += "<p>Kendaraan di Gerbang: <span id=\"gateVehicleStatus\">" + String(vehicle_at_gate ? "ADA" : "TIDAK ADA") + "</span></p>";
  html += "<p>Status Palang: <span id=\"gateServoStatus\">" + String(current_servo_angle == gateClosedAngle ? "Tertutup" : "Terbuka") + "</span></p></div>";
  
  // Tombol Bayar / Buka Portal.
  html += "<button id=\"payBtn\" class=\"mt-5 bg-green-600 text-white font-extrabold text-[14px] rounded-full py-3 w-full shadow-md disabled:bg-green-900 disabled:opacity-70 disabled:cursor-not-allowed\">Bayar Parkir / Buka Portal</button></div>";
  
  // Struktur HTML untuk popup (dialog konfirmasi). Awalnya tersembunyi ('hidden').
  html += "<div id=\"popupOverlay\" class=\"fixed inset-0 bg-black bg-opacity-50 flex justify-center items-center hidden z-50\"><div id=\"popupContent\" class=\"relative bg-white rounded-lg p-6 max-w-xs w-full text-center shadow-lg opacity-0 scale-90\">";
  html += "<div id=\"popupExclamation\" class=\"hidden\">!</div><h2 id=\"popupTitle\" class=\"text-lg font-bold mb-4\">Konfirmasi</h2><p id=\"popupText\" class=\"mb-6 text-gray-700\"></p>";
  html += "<button id=\"confirmBtn\" class=\"bg-green-600 text-white font-bold py-2 px-6 rounded-full mr-4 hover:bg-green-700 transition\">Konfirmasi</button>";
  html += "<button id=\"cancelBtn\" class=\"bg-gray-300 text-gray-700 font-bold py-2 px-6 rounded-full hover:bg-gray-400 transition\">Batal</button></div></div>";

  // Awal dari kode JavaScript. Kode ini berjalan di browser pengguna.
  // JavaScript membuat halaman web menjadi interaktif.
  html += "<script>";
  // Mendapatkan referensi ke elemen HTML berdasarkan ID-nya.
  html += "const slot1_el = document.getElementById('slot1'); const slot2_el = document.getElementById('slot2');";
  html += "const payBtn = document.getElementById('payBtn'); const popupOverlay = document.getElementById('popupOverlay');";
  html += "const popupContent = document.getElementById('popupContent'); const popupExclamation = document.getElementById('popupExclamation');";
  html += "const popupText = document.getElementById('popupText'); const popupTitle = document.getElementById('popupTitle');";
  html += "const confirmBtn = document.getElementById('confirmBtn'); const cancelBtn = document.getElementById('cancelBtn');";
  // Variabel JavaScript untuk melacak slot yang dipilih pengguna dan slot yang memang sudah terisi dari server.
  html += "let selectedSlots = new Set(); let occupiedSlots = new Set();";
  // Tandai slot yang sudah terisi dari server saat halaman pertama kali dimuat.
  html += "if (" + slot1_initial_js_occupied + ") { occupiedSlots.add('Slot 1'); }";
  html += "if (" + slot2_initial_js_occupied + ") { occupiedSlots.add('Slot 2'); }";

  // Fungsi untuk mengaktifkan/menonaktifkan tombol bayar.
  html += "function updatePayButton() { payBtn.disabled = selectedSlots.size === 0; }";
  // Fungsi untuk animasi saat slot dipilih.
  html += "function animateSlotSelect(el) { el.classList.add('slot-select-animate'); el.addEventListener('animationend', () => el.classList.remove('slot-select-animate'), { once: true }); }";
  // Fungsi untuk menangani klik pada slot parkir.
  html += "function toggleSlot(el, name) { if (occupiedSlots.has(name)) return; if (selectedSlots.has(name)) { selectedSlots.delete(name); el.classList.remove('bg-yellow-400'); el.classList.add('bg-green-600'); } else { if (selectedSlots.size > 0 && !selectedSlots.has(name)) return; selectedSlots.add(name); el.classList.remove('bg-green-600'); el.classList.add('bg-yellow-400'); animateSlotSelect(el); } updatePayButton(); }";
  // Fungsi untuk menandai slot sebagai terisi (merah) secara visual.
  html += "function markSlotOccupied(el, name) { occupiedSlots.add(name); el.className = 'w-[48px] h-[48px] rounded-md shadow-md bg-red-700 cursor-default'; el.style.boxShadow = '0 0 8px 2px rgba(220,38,38,0.7)'; }";
  
  // Tambahkan event listener (aksi saat diklik) ke slot jika slot tersebut tidak terisi.
  html += "if (!occupiedSlots.has('Slot 1')) slot1_el.addEventListener('click', () => toggleSlot(slot1_el, 'Slot 1'));";
  html += "if (!occupiedSlots.has('Slot 2')) slot2_el.addEventListener('click', () => toggleSlot(slot2_el, 'Slot 2'));";
  html += "updatePayButton();"; // Atur status awal tombol bayar.

  // Fungsi untuk menampilkan popup.
  html += "function showPopup() { popupExclamation.classList.remove('hidden'); popupOverlay.classList.remove('hidden'); popupContent.classList.remove('popup-fade-out','opacity-0','scale-90'); popupContent.classList.add('popup-fade-in'); popupContent.style.opacity = '1'; popupContent.style.transform = 'scale(1)'; }";
  // Fungsi untuk menyembunyikan popup.
  html += "function hidePopup() { popupContent.classList.remove('popup-fade-in'); popupContent.classList.add('popup-fade-out'); popupContent.addEventListener('animationend', () => { popupOverlay.classList.add('hidden'); popupContent.style.opacity = '0'; popupContent.style.transform = 'scale(0.9)'; popupExclamation.classList.add('hidden'); }, { once: true }); }";

  // Event listener untuk tombol "Bayar Parkir / Buka Portal".
  html += "payBtn.addEventListener('click', () => {";
  html += "  if (selectedSlots.size > 0) { popupTitle.textContent = 'Konfirmasi Pembayaran'; popupText.textContent = 'Anda akan membayar untuk ' + Array.from(selectedSlots).join(', ') + '. Lanjutkan?'; confirmBtn.dataset.action = 'pay'; confirmBtn.classList.remove('hidden'); cancelBtn.textContent = 'Batal'; showPopup(); }"; // Jika ada slot dipilih, munculkan popup bayar.
  html += "  else { fetch('/data.json').then(r => r.json()).then(d => { if (d.gate_vehicle === 'ADA') { if (d.available_slots > 0) { popupTitle.textContent = 'Buka Portal Manual'; popupText.textContent = 'Kendaraan terdeteksi di gerbang dan ada slot tersedia. Buka portal?'; confirmBtn.dataset.action = 'opengate'; confirmBtn.classList.remove('hidden'); cancelBtn.textContent = 'Batal'; } else { popupTitle.textContent = 'Parkir Penuh!'; popupText.textContent = 'Maaf, parkir penuh. Portal tidak dapat dibuka.'; confirmBtn.classList.add('hidden'); cancelBtn.textContent = 'OK'; } } else { popupTitle.textContent = 'Info'; popupText.textContent = 'Tidak ada kendaraan di gerbang atau tidak ada slot dipilih.'; confirmBtn.classList.add('hidden'); cancelBtn.textContent = 'OK'; } showPopup(); }); }"; // Jika tidak ada slot dipilih, cek data server untuk buka portal manual atau info.
  html += "});";

  // Event listener untuk tombol "Konfirmasi" di dalam popup.
  html += "confirmBtn.addEventListener('click', () => { const action = confirmBtn.dataset.action; if (action === 'pay') { popupTitle.textContent = 'Pembayaran Berhasil'; popupText.textContent = 'Gerbang akan terbuka otomatis saat kendaraan mendekat. Silahkan menuju slot parkir Anda!'; selectedSlots.forEach(s => { if (s === 'Slot 1') markSlotOccupied(slot1_el, s); else if (s === 'Slot 2') markSlotOccupied(slot2_el, s); }); selectedSlots.clear(); updatePayButton(); } else if (action === 'opengate') { popupTitle.textContent = 'Portal Dibuka'; popupText.textContent = 'Portal akan dikontrol oleh sensor gerbang.'; } confirmBtn.classList.add('hidden'); cancelBtn.textContent = 'Tutup'; delete confirmBtn.dataset.action; });";
  // Event listener untuk tombol "Batal" atau "Tutup" di dalam popup.
  html += "cancelBtn.addEventListener('click', () => { if (confirmBtn.classList.contains('hidden')) { hidePopup(); confirmBtn.classList.remove('hidden'); cancelBtn.textContent = 'Batal'; } else { hidePopup(); } delete confirmBtn.dataset.action; });";
  // Event listener untuk menutup popup jika area di luar popup diklik.
  html += "popupOverlay.addEventListener('click', (e) => { if (e.target === popupOverlay) { if (confirmBtn.classList.contains('hidden')) { hidePopup(); confirmBtn.classList.remove('hidden'); cancelBtn.textContent = 'Batal'; } else { hidePopup(); } delete confirmBtn.dataset.action; } });";

  // Fungsi untuk mengambil data terbaru dari ESP32 ("/data.json") dan memperbarui tampilan halaman.
  html += "function refreshDynamicData() { fetch('/data.json').then(r => r.json()).then(d => {"; // 'fetch' mengambil data dari URL.
  // Update nilai suhu, kelembapan, kondisi cahaya.
  html += "  document.querySelector('.flex-col.justify-center.items-center p.text-black.font-semibold').textContent = (d.temperature === null ? 'N/A' : d.temperature.toFixed(1)) + ' °C';";
  html += "  document.querySelectorAll('.flex-col.justify-center.items-center p.text-black.font-semibold')[1].textContent = (d.humidity === null ? 'N/A' : d.humidity.toFixed(1)) + ' %';";
  html += "  let lc = 'Redup'; if(d.light_percent < 30) lc = 'Gelap'; else if (d.light_percent > 70) lc = 'Terang';";
  html += "  document.querySelectorAll('.flex-col.justify-center.items-center p.text-black.font-semibold')[2].textContent = lc;";
  html += "  document.querySelectorAll('.flex-col.justify-center.items-center p.text-black.text-\\[10px\\]')[2].textContent = 'Kondisi Cahaya (' + d.light_percent + '%)';";
  // Update jumlah slot tersedia.
  html += "  document.querySelector('.font-extrabold.text-\\[14px\\].mb-2.text-center').textContent = 'SLOT PARKIR (' + d.available_slots + ' tersedia)';";
  // Update status kendaraan di gerbang dan status palang.
  html += "  document.getElementById('gateVehicleStatus').textContent = d.gate_vehicle;";
  html += "  document.getElementById('gateServoStatus').textContent = (d.servo_angle === " + String(gateClosedAngle) + " ? 'Tertutup' : 'Terbuka');"; // Bandingkan dengan sudut servo tertutup.
  // Update status visual slot parkir berdasarkan data terbaru dari server.
  html += "  const serverOccupied = new Set(); if (d.slot1_status === 'TERISI') serverOccupied.add('Slot 1'); if (d.slot2_status === 'TERISI') serverOccupied.add('Slot 2'); occupiedSlots = new Set(serverOccupied);"; // Update daftar slot yang terisi dari server.
  // Perbarui tampilan slot 1 jika tidak sedang dipilih oleh pengguna.
  html += "  if (!selectedSlots.has('Slot 1')) { if (occupiedSlots.has('Slot 1')) { slot1_el.className = 'w-[48px] h-[48px] rounded-md shadow-md bg-red-700 cursor-default'; slot1_el.style.boxShadow = '0 0 8px 2px rgba(220,38,38,0.7)'; } else { slot1_el.className = 'w-[48px] h-[48px] rounded-md shadow-md bg-green-600 cursor-pointer'; slot1_el.style.boxShadow = ''; if(!slot1_el.onclick) slot1_el.addEventListener('click', () => toggleSlot(slot1_el, 'Slot 1'));}}";
  // Perbarui tampilan slot 2 jika tidak sedang dipilih oleh pengguna.
  html += "  if (!selectedSlots.has('Slot 2')) { if (occupiedSlots.has('Slot 2')) { slot2_el.className = 'w-[48px] h-[48px] rounded-md shadow-md bg-red-700 cursor-default'; slot2_el.style.boxShadow = '0 0 8px 2px rgba(220,38,38,0.7)'; } else { slot2_el.className = 'w-[48px] h-[48px] rounded-md shadow-md bg-green-600 cursor-pointer'; slot2_el.style.boxShadow = ''; if(!slot2_el.onclick) slot2_el.addEventListener('click', () => toggleSlot(slot2_el, 'Slot 2'));}}";
  html += " }).catch(e => console.error('Error fetching dynamic data:', e)); }"; // Tangani error jika gagal mengambil data.
  // Jalankan fungsi refreshDynamicData setiap 3 detik untuk memperbarui halaman secara otomatis.
  html += "setInterval(refreshDynamicData, 3000);";
  // Jalankan refreshDynamicData juga saat halaman pertama kali selesai dimuat.
  html += "document.addEventListener('DOMContentLoaded', refreshDynamicData);";
  html += "</script>"; // Akhir dari kode JavaScript.
  html += "</body></html>"; // Akhir dari HTML.

  // Kirim halaman HTML yang sudah dibuat ke browser.
  server.send(200, "text/html", html); // Kode 200 (OK), tipe konten "text/html".
}