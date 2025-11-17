/***** ESP32 + I2C 압력센서 → Nextion(UART2) + MQTT Publish (JSON) *****/
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>

/* ====== Pin / Port Config ====== */
static const int SDA_PIN = 21, SCL_PIN = 22;  // I2C
static const int UART2_TX = 17;               // ESP32 TX2 -> Nextion RX
static const int UART2_RX = 16;               // ESP32 RX2 <- Nextion TX(옵션)
static const uint32_t NEXTION_BAUD = 115200;    // HMI 프로젝트 보레이트와 동일하게

/* ====== WiFi & MQTT ====== */
const char* WIFI_SSID = "KT_105_L1";
const char* WIFI_PASS = "44444445";

// 브로커 주소/포트는 서버쪽 Python과 동일하게
const char* MQTT_BROKER = "43.200.169.54"; // 또는 "192.168.x.y" / "afa2025.ddns.net" 등
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "stm/dp";

WiFiClient espClient;
PubSubClient mqtt(espClient);

/* ====== Sensor (Honeywell ABP2 계열 24-bit) ====== */
static const uint8_t I2C_ADDR = 0x28;

// ±2 inH2O → ±0.498178 kPa
static const float P_MIN_kPa = -0.498178f;
static const float P_MAX_kPa = +0.498178f;

// 디지털 카운트 범위 (10~90% of 2^24)
static const uint32_t OUT_MIN = 1677722;   // 0x19999A
static const uint32_t OUT_MAX = 15099494;  // 0xE66666

/* ====== App Settings ====== */
const uint8_t  SAMPLES   = 30;    // 평균 샘플 수
const uint32_t PERIOD_MS = 500;   // 측정/전송 주기
float offset_kPa = 0.0f;

/* ====== Utility ====== */
inline float counts_to_kPa(uint32_t c){
  return ((float)((int32_t)c - (int32_t)OUT_MIN)) * (P_MAX_kPa - P_MIN_kPa)
         / (float)(OUT_MAX - OUT_MIN) + P_MIN_kPa;
}
inline float kPa_to_Pa(float kPa){ return kPa * 1000.0f; }

/* ====== Nextion Helpers ====== */
void nextionSendFF(){ Serial2.write(0xFF); Serial2.write(0xFF); Serial2.write(0xFF); }
void nextionCmd(const String& cmd){
  Serial2.print(cmd); nextionSendFF();
}
void nextionSetNum(const char* obj, long val){
  String cmd = String(obj) + ".val=" + String(val);
  nextionCmd(cmd);
}

/* ====== I2C Helpers ====== */
bool trigger_one_shot(){
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(0xAA); Wire.write(0x00); Wire.write(0x00);
  return Wire.endTransmission(true) == 0;
}
bool read7(uint8_t d[7]){
  int n = Wire.requestFrom((int)I2C_ADDR, 7, (int)true);
  if (n != 7) return false;
  for (int i=0;i<7;i++) d[i] = Wire.read();
  return true;
}
bool read_ready(uint8_t d[7], uint32_t &p24, uint32_t &t24, uint8_t &status){
  for (int i=0;i<200;i++){
    if (!read7(d)) continue;
    status = d[0];
    if ((status & 0x20) == 0){
      p24 = ((uint32_t)d[1]<<16)|((uint32_t)d[2]<<8)|d[3];
      t24 = ((uint32_t)d[4]<<16)|((uint32_t)d[5]<<8)|d[6];
      return true;
    }
    delayMicroseconds(10);
  }
  return false;
}

/* ====== Tare ====== */
void do_tare(uint16_t N=200){
  float s = 0.0f; uint16_t n = 0;
  uint8_t d[7]; uint32_t p,t; uint8_t st;
  for (uint16_t i=0;i<N;i++){
    if (!trigger_one_shot()) { delay(2); continue; }
    if (!read_ready(d, p, t, st)) { delay(2); continue; }
    s += counts_to_kPa(p);
    n++; delay(5);
  }
  if (n) offset_kPa = s / (float)n;
  Serial.print("[TARE] offset_kPa="); Serial.println(offset_kPa, 6);
}

/* ====== WiFi/MQTT ====== */
void wifiConnect(){
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED){
    delay(300);
    Serial.print(".");
    if (millis() - t0 > 15000) { Serial.println("\n[WiFi] Timeout"); break; }
  }
  if (WiFi.status() == WL_CONNECTED){
    Serial.printf("\n[WiFi] OK: %s  IP=%s\n", WIFI_SSID, WiFi.localIP().toString().c_str());
  }
}

void mqttEnsure(){
  if (mqtt.connected()) return;
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);

  Serial.printf("[MQTT] Connecting to %s:%u ...\n", MQTT_BROKER, MQTT_PORT);
  // LWT(선택): 오프라인 표시하고 싶으면 사용
  const char* clientId = "esp32-dp";
  if (mqtt.connect(clientId /*, willTopic, willQoS, willRetain, willMessage*/)){
    Serial.println("[MQTT] Connected");
    // 필요 시 구독도 가능(ex: 원격 TARE): mqtt.subscribe("stm/cmd");
  } else {
    Serial.printf("[MQTT] Failed rc=%d\n", mqtt.state());
  }
}

bool mqttPublishDP(float dp_pa, uint8_t samples){
  if (!mqtt.connected()) return false;
  // 경량 JSON (ArduinoJson 없이)
  char buf[96];
  // 소수 2자리로 전송 (Python에서 float로 parse됨)
  snprintf(buf, sizeof(buf), "{\"dp_pa\":%.2f,\"samples\":%u}", dp_pa, (unsigned)samples);
  bool ok = mqtt.publish(MQTT_TOPIC, buf, false); // retain=false
  if (!ok) Serial.println("[MQTT] publish failed");
  return ok;
}

/* ====== Serial Command (optional) ====== */
void check_serial(){
  while (Serial.available()){
    char c = Serial.read();
    if (c == 't') { do_tare(); }
  }
}

/* ====== Setup / Loop ====== */
void setup(){
  Serial.begin(115200);
  delay(200);

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN, 400000);

  // Nextion UART2
  Serial2.begin(NEXTION_BAUD, SERIAL_8N1, UART2_RX, UART2_TX);
  nextionCmd("bkcmd=3");   // 응답 모드 ON (디버깅에 유용)
  // 필요 시 표시 페이지 지정: nextionCmd("page 0");

  // WiFi & MQTT
  wifiConnect();
  mqttEnsure();

  Serial.println("ESP32: DP sensor -> Nextion + MQTT");
  do_tare(120); // 부팅시 자동 TARE
}

void loop(){
  unsigned long cycleStart = millis();
  check_serial();

  // 연결 유지
  if (WiFi.status() != WL_CONNECTED) wifiConnect();
  if (!mqtt.connected())             mqttEnsure();
  mqtt.loop(); // 콜백/keepalive

  // --- 측정 수집 ---
  uint8_t d[7]; uint32_t p24, t24; uint8_t st;
  float sum_kPa = 0.0f; uint8_t got = 0;
  const uint16_t MAX_ATTEMPTS = 200; uint16_t attempts = 0;

  while (got < SAMPLES && attempts < MAX_ATTEMPTS){
    attempts++;
    if (!trigger_one_shot()){ delay(2); continue; }
    if (!read_ready(d, p24, t24, st)){ delay(2); continue; }
    sum_kPa += counts_to_kPa(p24);
    got++; delay(10);
  }

  if (got > 0){
    float mean_kPa     = sum_kPa / (float)got;
    float corrected_kPa= mean_kPa - offset_kPa;
    float dP_Pa        = kPa_to_Pa(corrected_kPa);

    // 콘솔
    Serial.printf("dP=%.2f Pa (samples=%u)\n", dP_Pa, got);

    // Nextion 전송 (정수 Pa)
    long dP_int_Pa = lroundf(dP_Pa);
    nextionSetNum("n0", dP_int_Pa);
    nextionSetNum("n1", (long)got);

    // MQTT 전송
    mqttPublishDP(dP_Pa, got);
  } else {
    Serial.println("No samples");
    nextionSetNum("n0", 0);
    nextionSetNum("n1", 0);
  }

  // 주기 유지
  unsigned long elapsed = millis() - cycleStart;
  if (elapsed < PERIOD_MS) delay(PERIOD_MS - elapsed);
}
