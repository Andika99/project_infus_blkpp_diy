#include <WiFi.h>
#include <PubSubClient.h>

#include <WiFiManager.h> 

#include "HX711.h"
#include <Preferences.h>

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const int TRIGGER_PIN = 27; // Pin tombol trigger AP
unsigned long buttonPressedTime = 0;
bool isPressed = false;

// Sesuaikan pin dengan fisik yang Anda hubungkan ke Load Cell
const int LOADCELL_DOUT_PIN = 16;
const int LOADCELL_SCK_PIN = 4;

HX711 scale;
Preferences preferences; // Membuat objek preferences

LiquidCrystal_I2C lcd(0x27, 20, 4); // Membuat object lcd

String mqtt_server;
String mqtt_port;
String mqtt_topic;

bool shouldSaveConfig = false;

#define TARE_PIN 25
#define BUZZER_PIN 32

WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long lastSend = 0;
const long sendInterval = 1000; // kirim tiap 1 detik

int buzzer_state = LOW;
unsigned long previousMillis_buzzer = 0;
const long interval_buzzer = 1000; 

int persen = 100;

void saveConfigCallback() {
  Serial.println("Konfigurasi baru diterima dari WiFiManager!");
  shouldSaveConfig = true;
}

// Fungsi khusus untuk MEMBACA data dari Preferences ke global variabel
void readMQTTConfig() {
  preferences.begin("mqtt-config", false); // true = Read Only
  
  mqtt_server = preferences.getString("server", "mqtt.ichibot.id"); // <- Default Server
  mqtt_port   = preferences.getString("port", "1883");               // <- Default Port
  mqtt_topic  = preferences.getString("topic", "infus/1/persen"); 

  // Jika ini pertama kali dinyalakan (memori kosong), tulis nilai default ke Flash permanen
  if (!preferences.isKey("server")) {
    preferences.putString("server", mqtt_server);
    preferences.putString("port", mqtt_port);
    preferences.putString("topic", mqtt_topic);
    Serial.println("[PREF] Memori kosong. Nilai default berhasil disimpan.");
  } else {
    Serial.println("[PREF] Data MQTT berhasil dibaca dari memori.");
  }

  // preferences.end();
  Serial.println("[PREF] Data MQTT berhasil dibaca.");
}

void saveMQTTConfig(String server, String port, String topic) {

  preferences.begin("mqtt-config", false); // false = Read and Write
  
  preferences.putString("server", server);
  preferences.putString("port", port);
  preferences.putString("topic", topic);
  
  // preferences.end();
  Serial.println("[PREF] Data MQTT baru berhasil disimpan permanen.");
  
  // Perbarui juga variabel global agar program langsung memakai data baru
  mqtt_server = server;
  mqtt_port = port;
  mqtt_topic = topic;
}

void tampilan_lcd_provisioning() {
  lcd.clear();
  lcd.setCursor(0, 0); 
  lcd.print("Mode Provisioning   ");
  lcd.setCursor(0, 1); 
  lcd.print("Koneksikan ke:      ");
  lcd.setCursor(0, 2); 
  lcd.print("Infus_IOT           ");
  lcd.setCursor(0, 1); 
}

void connectWiFi() {
  Serial.print("Menghubungkan ke WiFi");
  // WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.begin();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
// 2. DETEKSI TOMBOL UNTUK PROVISIONING (Ditahan 3 detik)
  if (digitalRead(TRIGGER_PIN) == LOW) {
      if (!isPressed) {
          buttonPressedTime = millis();
          isPressed = true;
      }
      if (millis() - buttonPressedTime >= 3000) {
          isPressed = false;
          tampilan_lcd_provisioning();

          jalankanProvisioning(); // Panggil fungsi provisioning
          lcd.clear();
        }
      } else {
          isPressed = false;
      }

  }
  Serial.println();
  Serial.print("WiFi terhubung, IP: ");
  Serial.println(WiFi.localIP());
}

void sendTelemetry() {
  mqtt.publish(mqtt_topic.c_str(), String(persen).c_str());

  Serial.print("Terkirim -> ");
  Serial.println(mqtt_topic);
}

void reconnectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Menghubungkan ke broker MQTT...");
    // ID client harus unik juga, tambahkan random supaya tidak bentrok
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);

    // if (mqtt.connect(clientId.c_str(), TOKEN_THINGSBOARD, NULL)) {
    //   Serial.println("terhubung ke Thingsboard!");
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("terhubung!");
    } else {
      Serial.print("gagal, rc=");
      Serial.print(mqtt.state());
      Serial.println(" coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

void melakukan_tare() {
  digitalWrite(BUZZER_PIN, LOW);
  lcd.backlight();

  while (1) {
    if (scale.is_ready()) {
      scale.set_scale();
      Serial.println("Tare... kosongkan timbangan.");

      lcd.clear();
      delay(10);
      lcd.setCursor(0, 1); 
      lcd.print("Melakukan Tare");
      lcd.setCursor(0, 2); 
      lcd.print("Kosongkan timbangan");


      delay(3000);
      scale.tare();
      Serial.println("Tare selesai.");
      break;
    }
  }
}

void simpan_tare_preference() {
      // Simpan nilai offset baru ke Preferences

      preferences.begin("infus_tare", false);

      long new_offset = scale.get_offset();
      preferences.putLong("offset", new_offset);
      
      Serial.print("[SUKSES] Nilai tare baru disimpan: ");
      Serial.println(new_offset);

      // preferences.end();
}

void read_load_cell(unsigned long * time_now) {
  if (scale.is_ready()) {

    //Serial.println("Letakkan beban yang sudah diketahui beratnya...");
    delay(10);

    float reading = scale.get_units(10) * -1;
    // if (reading >= 220000) {
    if (reading >= 200000) {
      persen = 100;

      lcd.setCursor(0, 1); 
      lcd.print("Infus: 100%         ");
      lcd.setCursor(0, 2); 
      lcd.print("                    ");

      digitalWrite(BUZZER_PIN, LOW);
      lcd.backlight();
    } else if (reading >= 171000) {
      persen = 75;

      lcd.setCursor(0, 1); 
      lcd.print("Infus: 75%          ");
      lcd.setCursor(0, 2); 
      lcd.print("                    ");

      digitalWrite(BUZZER_PIN, LOW);
      lcd.backlight();
    } else if (reading >= 122000) {
      persen = 50;

      lcd.setCursor(0, 1); 
      lcd.print("Infus: 50%          ");
      lcd.setCursor(0, 2); 
      lcd.print("                    ");

      digitalWrite(BUZZER_PIN, LOW);
      lcd.backlight();
    } else if (reading >= 65000) {
      persen = 25;

      lcd.setCursor(0, 1); 
      lcd.print("Infus: 25%          ");
      lcd.setCursor(0, 2); 
      lcd.print("                    ");

      digitalWrite(BUZZER_PIN, LOW);
      lcd.backlight();
    } else if (reading >= 38000) {
      persen = 10;

      lcd.setCursor(0, 1); 
      lcd.print("Infus: 10%          ");
      lcd.setCursor(0, 2); 
      lcd.print("                    ");

      digitalWrite(BUZZER_PIN, LOW);
      lcd.backlight();
    } else {
      persen = 5;

      lcd.setCursor(0, 1); 
      lcd.print("Infus: 5%           ");
      lcd.setCursor(0, 2); 
      lcd.print("INFUS HABIS!!!!     ");

      // Buzzer berbunyi jika infus 5 persen
      if (*time_now - previousMillis_buzzer >= interval_buzzer) {
        previousMillis_buzzer = *time_now;

        if (buzzer_state == LOW) { buzzer_state = HIGH; } 
        else { buzzer_state = LOW; }
        
        digitalWrite(BUZZER_PIN, buzzer_state);
      }

      if (buzzer_state) {
        lcd.backlight();
      } else {
        lcd.noBacklight();
      }
    }
    
    Serial.print("Hasil dalam persen: ");
    Serial.println(persen);
    Serial.println(reading);
  } else {
    Serial.println("HX711 tidak ditemukan. Periksa kabel DT/SCK Anda!");
    // delay(2000);
  }
  delay(100);
}

void jalankanProvisioning() {
    digitalWrite(BUZZER_PIN, LOW);
    lcd.backlight();

    Serial.println("Membuka Portal Konfigurasi Wi-Fi...");
    WiFiManager wm;

    // Set callback simpan
    wm.setSaveConfigCallback(saveConfigCallback);


    char server_buf[40];
    char port_buf[6];
    char topic_buf[50];

    mqtt_server.toCharArray(server_buf, 40);
    mqtt_port.toCharArray(port_buf, 6);
    mqtt_topic.toCharArray(topic_buf, 50);

    WiFiManagerParameter server_broker_param("server_broker", "MQTT Server", server_buf, 40);
    WiFiManagerParameter port_broker_param("port_broker", "MQTT Port", port_buf, 6);
    WiFiManagerParameter topic_param("topic", "Topic", topic_buf, 50);

    wm.addParameter(&server_broker_param);
    wm.addParameter(&port_broker_param);
    wm.addParameter(&topic_param);
    
    // Set timeout portal 3 menit agar tidak macet selamanya jika ditinggal
    wm.setConfigPortalTimeout(180); 

    // Paksa buka portal AP
    if (wm.startConfigPortal("Infus_IOT")) {
        Serial.println("Kredensial baru disimpan! ESP32 akan restart...");

        String new_server = String(server_broker_param.getValue());
        String new_port   = String(port_broker_param.getValue());
        String new_topic  = String(topic_param.getValue());

        saveMQTTConfig(new_server, new_port, new_topic);
        delay(1000);

        // Serial.print("Custom text box entry: ");
        // Serial.println( custom_text_box.getValue() );

        Serial.print("Server: ");
        Serial.println(new_server);
        Serial.print("Port: ");
        Serial.println(new_port);
        Serial.print("Topic: ");
        Serial.println(new_topic);


        ESP.restart(); // Restart agar WiFi class membaca kredensial baru dari flash
    } else {
        Serial.println("Portal timeout atau diclose tanpa menyimpan.");
    }

}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  Serial.println("Infus IOT");

  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();   // Hidupkan lampu latar

  lcd.setCursor(5, 1); 
  lcd.print("Infus IOT");

  delay(1000);

  pinMode(TARE_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  // Dirubah agar setiap hidup tidak melakukan tare
  // Tare dilakukan ketika pin 25 ditrigger ke LOW
  // Membuka ruang penyimpanan bernama "infus_tare" dengan mode Read/Write (false)
  preferences.begin("infus_tare", false);

  // Membaca data dengan Key "offset". Jika Key belum ada, nilai default-nya adalah 0
  long saved_offset = preferences.getLong("offset", 0);

  if(saved_offset == 0) {
    Serial.println("[INFO] Belum ada data tare tersimpan. Silakan tekan tombol.");

    lcd.clear();
    delay(10);
    lcd.setCursor(0, 1); 
    lcd.print("Data tare belum ada");
    lcd.setCursor(0, 2); 
    lcd.print("Silahkan tekan tare");
  }

  while(1) {
    if (saved_offset != 0) {
      scale.set_offset(saved_offset);
      Serial.print("[SUKSES] Nilai tare dimuat dari Preferences: ");
      Serial.println(saved_offset);

      lcd.clear();
      lcd.setCursor(0, 1); 
      lcd.print("Memuat Nilai Tare");
      delay(100);

      break;
    } 
    delay(10);

    if (digitalRead(TARE_PIN) == LOW) {
      delay(50); // Debounce sederhana
      if (digitalRead(TARE_PIN) == LOW) {
        melakukan_tare();
        simpan_tare_preference();
      }
    }

    saved_offset = preferences.getLong("offset", 0);

  }

  // preferences.end();

  readMQTTConfig();

  lcd.clear();
  lcd.setCursor(0, 1); 
  lcd.print("Koneksi ke Wifi     ");
  delay(100);
  connectWiFi();

  lcd.clear();
  lcd.setCursor(0, 1); 
  lcd.print("Koneksi ke Broker   ");
  delay(100);
  mqtt.setServer(mqtt_server.c_str(), mqtt_port.toInt());
}

void loop() {
  // put your main code here, to run repeatedly:
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  
  if (!mqtt.connected()) {
    reconnectMQTT();
  }

  mqtt.loop();

  // Deteksi tombol untuk Tare
  if (digitalRead(TARE_PIN) == LOW) {
    delay(10); // Debounce sederhana
    if (digitalRead(TARE_PIN) == LOW) {
      melakukan_tare();
      simpan_tare_preference();
    }
  }

  // 2. DETEKSI TOMBOL UNTUK PROVISIONING (Ditahan 3 detik)
  if (digitalRead(TRIGGER_PIN) == LOW) {
      if (!isPressed) {
          buttonPressedTime = millis();
          isPressed = true;
      }
      if (millis() - buttonPressedTime >= 3000) {
          isPressed = false;
          tampilan_lcd_provisioning();

          jalankanProvisioning(); // Panggil fungsi provisioning
          lcd.clear();
      }
  } else {
      isPressed = false;
  }

  unsigned long now = millis();

  if (now - lastSend > sendInterval) {
    lastSend = now;
    
    // Serial.println(status_tare);
    read_load_cell(&now);
    sendTelemetry();
  }

}
