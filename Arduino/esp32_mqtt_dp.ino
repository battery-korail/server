/***** ESP32 + I2C 압력센서 → Nextion(UART2) + MQTT Publish (SG only) *****/
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>

/* ====== Pin / Port Config ====== */
static const int SDA_PIN = 21, SCL_PIN = 22;  // I2C
static const int UART2_TX = 17;              // ESP32 TX2 -> Nextion RX
static const int UART2_RX = 16;              // ESP32 RX2 <- Nextion TX(옵션)
static const uint32_t NEXTION_BAUD = 115200; // HMI 프로젝트 보레이트와 동일하게

/* ====== WiFi & MQTT ====== */
const char* WIFI_SSID = "KT_105_L1";
const char* WIFI_PASS = "44444445";

const char* MQTT_BROKER = "43.200.169.54";
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
const uint8_t  SAMPLES   = 30;   // 평균 샘플 수
const uint32_t PERIOD_MS = 500;  // 측정/전송 주기

/* ====== 보정/물성 ====== */
static const float G = 9.80665f;  // 중력가속도
float RHO_WATER = 997.0f;        // 물 밀도 (20~25°C 근사)

// 사용 수두(헤드) 4.5 mm  → 이론압력 ≈ 44 Pa
float HEAD_M = 0.0045f;

// 20 Pa에서 c를 눌러 캘리브한 것과 같은 효과를 내기 위한 고정 게인
// 이론압력(≈44 Pa) / 측정압력(≈20 Pa) ≈ 2.2
static const float CALIB_GAIN_FIXED = 2.1999f;
float calib_gain = CALIB_GAIN_FIXED;  // 항상 이 값으로 사용

float offset_kPa = 0.0f;  // 영점 보정값 (tare)

/* ====== Utility ====== */
inline float counts_to_kPa(uint32_t c){
  return ((float)((int32_t)c - (int32_t)OUT_MIN)) * (P_MAX_kPa - P_MIN_kPa)
         / (float)(OUT_MAX - OUT_MIN) + P_MIN_kPa;
}
inline float kPa_to_Pa(float kPa){ return kPa * 1000.0f; }

/* ====== Nextion Helpers ====== */
void nextionSendFF(){
  Serial2.write(0xFF); Serial2.write(0xFF); Serial2.write(0xFF);
}
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
  for (int i = 0; i < 7; i++) d[i] = Wire.read();
  return true;
}
bool read_ready(uint8_t d[7], uint32_t &p24, uint32_t &t24, uint8_t &status){
  for (int i = 0; i < 200; i++){
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
void do_tare(uint16_t N = 200){
  float s = 0.0f; uint16_t n = 0;
  uint8_t d[7]; uint32_t p, t; uint8_t st;

  for (uint16_t i = 0; i < N; i++){
    if (!trigger_one_shot()) { delay(2); continue; }
    if (!read_ready(d, p, t, st)) { delay(2); continue; }
    s += counts_to_kPa(p);
    n++;
    delay(5);
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
    if (millis() - t0 > 15000){
      Serial.println("\n[WiFi] Timeout");
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED){
    Serial.printf("\n[WiFi] OK: %s  IP=%s\n",
                  WIFI_SSID, WiFi.localIP().toString().c_str());
  }
}

void mqttEnsure(){
  if (mqtt.connected()) return;
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);

  Serial.printf("[MQTT] Connecting to %s:%u ...\n", MQTT_BROKER, MQTT_PORT);
  const char* clientId = "esp32-dp";

  if (mqtt.connect(clientId)){
    Serial.println("[MQTT] Connected");
  } else {
    Serial.printf("[MQTT] Failed rc=%d\n", mqtt.state());
  }
}

// SG(비중) + 샘플 수만 서버로 보내는 함수
bool mqttPublishSG(float sg, uint8_t samples){
  if (!mqtt.connected()) return false;

  char buf[80];
  // sg는 소수 3자리, samples는 정수
  snprintf(buf, sizeof(buf),
           "{\"sg\":%.3f,\"samples\":%u}",
           sg, (unsigned)samples);

  bool ok = mqtt.publish(MQTT_TOPIC, buf, false); // retain=false
  if (!ok) Serial.println("[MQTT] publish failed");
  return ok;
}

/* ====== Serial Command (optional) ====== */
void check_serial(){
  while (Serial.available()){
    char c = Serial.read();
    if (c == 't') {
      do_tare();
    }
    // c/h/r 캘리브/헤드/밀도 설정은 이제 안 씀 (고정 게인 사용)
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
  nextionCmd("bkcmd=3");   // 응답 모드 ON (디버깅용)

  // WiFi & MQTT
  wifiConnect();
  mqttEnsure();

  Serial.println("ESP32: DP sensor -> Nextion + MQTT (SG mode, fixed calib)");

  // 부팅 시 자동 TARE만 수행 (캘리브레이션은 고정 게인 사용)
  do_tare(120);
}

void loop(){
  unsigned long cycleStart = millis();
  check_serial();

  // 연결 유지
  if (WiFi.status() != WL_CONNECTED) wifiConnect();
  if (!mqtt.connected())             mqttEnsure();
  mqtt.loop(); // keepalive

  // --- 측정 수집 ---
  uint8_t d[7]; uint32_t p24, t24; uint8_t st;
  float sum_kPa = 0.0f; uint8_t got = 0;
  const uint16_t MAX_ATTEMPTS = 200; uint16_t attempts = 0;

  while (got < SAMPLES && attempts < MAX_ATTEMPTS){
    attempts++;
    if (!trigger_one_shot()){ delay(2); continue; }
    if (!read_ready(d, p24, t24, st)){ delay(2); continue; }

    sum_kPa += counts_to_kPa(p24);
    got++;
    delay(10);
  }

  if (got > 0){
    float mean_kPa      = sum_kPa / (float)got;
    float corrected_kPa = mean_kPa - offset_kPa;
    float dP_Pa_raw     = kPa_to_Pa(corrected_kPa);      // 게인 전
    float dP_Pa_cal     = dP_Pa_raw * calib_gain;        // 고정 스케일 보정 적용

    // 수두가 설정돼 있으면 실시간 밀도/비중 추정
    float rho_est = 0.0f;
    float sg      = 0.0f;
    if (HEAD_M > 0.0f){
      rho_est = dP_Pa_cal / (G * HEAD_M);
      sg      = rho_est / 997.0f; // 물=1 기준 비중
    }

    // 콘솔 출력
    Serial.print("dP_raw=");
    Serial.print(dP_Pa_raw, 2);
    Serial.print(" Pa,  dP_cal=");
    Serial.print(dP_Pa_cal, 2);
    Serial.print(" Pa  (samples=");
    Serial.print(got);
    Serial.print(")");

    if (HEAD_M > 0.0f){
      Serial.print("  rho≈");
      Serial.print(rho_est, 1);
      Serial.print(" kg/m^3 (SG≈");
      Serial.print(sg, 3);
      Serial.print(")");
    }
    Serial.println();

    // ===== 디스플레이 전송 =====
    // 요청대로: 비중 × 1 (스케일 안 씀)
    // Number 컴포넌트는 정수라서 소수부는 잘림 → 물이면 1, 1.03이면 1로 보일 수 있음
    long sg_int = lroundf(sg);     // 곱하기 1 (스케일 없음)
    nextionSetNum("n0", sg_int);   // n0: 비중(정수화)
    nextionSetNum("n1", (long)got);// n1: 샘플 수

    // ===== MQTT 전송: SG만 전송 =====
    mqttPublishSG(sg, got);

  } else {
    Serial.println("No samples");
    nextionSetNum("n0", 0);
    nextionSetNum("n1", 0);
    mqttPublishSG(0.0f, 0);
  }

  // 주기 유지
  unsigned long elapsed = millis() - cycleStart;
  if (elapsed < PERIOD_MS) delay(PERIOD_MS - elapsed);
}
