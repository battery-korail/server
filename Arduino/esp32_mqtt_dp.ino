/***** ESP32 SG + Level + Button + Nextion + MQTT (최종 버전: SG 기존코드 100% 동일) *****/

#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>

/* ============================================================
   BUTTON
   ============================================================ */
const int BTN_PIN = 4;
bool lastBtn = HIGH;

/* ============================================================
   SG SENSOR (기존코드 그대로)
   ============================================================ */
static const int SDA_SG = 21, SCL_SG = 22;
static const uint8_t I2C_ADDR_SG = 0x28;

// ±2 inH2O → ±0.498178 kPa
static const float SG_P_MIN_kPa = -0.498178f;
static const float SG_P_MAX_kPa = +0.498178f;

// 디지털 카운트 범위 (10~90% of 2^24)
static const uint32_t SG_OUT_MIN = 1677722;   
static const uint32_t SG_OUT_MAX = 15099494;

// TARE 오프셋
float offset_kPa = 0.0f;

// 고정 캘리브레이션 게인
static const float CALIB_GAIN_FIXED = 2.0951f;
float calib_gain = CALIB_GAIN_FIXED;

// 수두 높이 (4.5mm)
float HEAD_M = 0.0045f;

static const float G = 9.80665f;
float RHO_WATER = 997.0f;

/* ============================================================
   LEVEL SENSOR (Wire1)
   ============================================================ */
static const int SDA_LV = 25, SCL_LV = 26;
static const uint8_t I2C_ADDR_LV = 0x28;

static const float LV_P_MIN = 0.0f;
static const float LV_P_MAX = 6.0f;

static const uint32_t LV_OUT_MIN = 1677722;
static const uint32_t LV_OUT_MAX = 15099494;

static const float RHO_WATER_LV = 1000.0f;
static const float G_ACCEL = 9.80665f;

static const float CONTAINER_FULL_HEIGHT_M = 0.20f;
static const float SENSOR_FROM_BOTTOM_M = 0.0f;

float P_ZERO_LV = 0.0f;

/* ============================================================
   WiFi / MQTT
   ============================================================ */
static const int UART2_TX = 17;
static const int UART2_RX = 16;
static const uint32_t NEXTION_BAUD = 115200;

const char* WIFI_SSID = "KT_105_L1";
const char* WIFI_PASS = "44444445";

const char* MQTT_BROKER = "43.200.169.54";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "stm/dp";

WiFiClient espClient;
PubSubClient mqtt(espClient);

/* ============================================================
   함수 프로토타입
   ============================================================ */
void mqttEnsure();

/* ============================================================
   SG Sensor Utility (기존코드 그대로)
   ============================================================ */
float sg_counts_to_kPa(uint32_t c){
  return ((float)((int32_t)c - (int32_t)SG_OUT_MIN)) *
         (SG_P_MAX_kPa - SG_P_MIN_kPa) /
         (float)(SG_OUT_MAX - SG_OUT_MIN) +
         SG_P_MIN_kPa;
}

inline float kPa_to_Pa(float kPa){
  return kPa * 1000.0f;
}

/* ============================================================
   LEVEL Sensor Utility
   ============================================================ */
float lv_counts_to_kPa(uint32_t c){
  return ((float)((int32_t)c - (int32_t)LV_OUT_MIN)) *
         (LV_P_MAX - LV_P_MIN) /
         (float)(LV_OUT_MAX - LV_OUT_MIN) +
         LV_P_MIN;
}

float lv_counts_to_C(uint32_t t){
  return ((float)t) * 200.0f / 16777215.0f - 50.0f;
}

/* ============================================================
   NEXTION
   ============================================================ */
void nextionSendFF(){
  Serial2.write(0xFF);
  Serial2.write(0xFF);
  Serial2.write(0xFF);
}

void nextionCmd(const String& cmd){
  Serial2.print(cmd);
  nextionSendFF();
}

void nextionSetNum(const char* obj, long val){
  String cmd = String(obj) + ".val=" + String(val);
  nextionCmd(cmd);
}

/* ============================================================
   LEVEL SENSOR (Wire1)
   ============================================================ */
bool trigger_LV(){
  Wire1.beginTransmission(I2C_ADDR_LV);
  Wire1.write(0xAA); Wire1.write(0x00); Wire1.write(0x00);
  return Wire1.endTransmission(true) == 0;
}

bool read7_LV(uint8_t d[7]){
  int n = Wire1.requestFrom((int)I2C_ADDR_LV, 7, (int)true);
  if(n != 7) return false;
  for(int i=0;i<7;i++) d[i] = Wire1.read();
  return true;
}

bool readOnce_LV(float &P_kPa, float &T_C){
  uint8_t d[7], status;

  if(!trigger_LV()) return false;

  for(int i=0;i<10;i++){
    delay(1);
    if(!read7_LV(d)) continue;
    status = d[0];

    if(!(status & 0x20)){
      uint32_t p = ((uint32_t)d[1] << 16) |
                   ((uint32_t)d[2] << 8 ) |
                   d[3];
      uint32_t t = ((uint32_t)d[4] << 16) |
                   ((uint32_t)d[5] << 8 ) |
                   d[6];

      P_kPa = lv_counts_to_kPa(p);
      T_C    = lv_counts_to_C(t);
      return true;
    }
  }
  return false;
}

void calibrateZero_LV(){
  float s = 0; int good = 0;
  float P,T;

  for(int i=0;i<30;i++){
    if(readOnce_LV(P,T)){
      s += P;
      good++;
    }
    delay(5);
  }

  if(good > 0) P_ZERO_LV = s / good;
  else P_ZERO_LV = 0;
}

/* ============================================================
   SG SENSOR (기존코드 그대로)
   ============================================================ */
bool trigger_one_shot(){
  Wire.beginTransmission(I2C_ADDR_SG);
  Wire.write(0xAA); Wire.write(0x00); Wire.write(0x00);
  return Wire.endTransmission(true) == 0;
}

bool read7_SG(uint8_t d[7]){
  int n = Wire.requestFrom((int)I2C_ADDR_SG, 7, (int)true);
  if(n != 7) return false;
  for(int i=0;i<7;i++) d[i] = Wire.read();
  return true;
}

bool read_ready_SG(uint8_t d[7], uint32_t &p24, uint32_t &t24, uint8_t &status){
  for(int i=0;i<200;i++){
    if(!read7_SG(d)) continue;
    status = d[0];
    if(!(status & 0x20)){
      p24 = ((uint32_t)d[1]<<16) |
            ((uint32_t)d[2]<<8 ) |
             d[3];
      t24 = ((uint32_t)d[4]<<16) |
            ((uint32_t)d[5]<<8 ) |
             d[6];
      return true;
    }
    delayMicroseconds(10);
  }
  return false;
}

/* ===================== SG TARE (기존 동일) ===================== */
void do_tare(uint16_t N = 200){
  float s = 0.0f; uint16_t n = 0;
  uint8_t d[7]; uint32_t p, t; uint8_t st;

  for(uint16_t i=0; i<N; i++){
    if (!trigger_one_shot()) { delay(2); continue; }
    if (!read_ready_SG(d, p, t, st)) { delay(2); continue; }

    s += sg_counts_to_kPa(p);
    n++;
    delay(5);
  }

  if(n) offset_kPa = s / (float)n;
  else  offset_kPa = 0;

  Serial.print("[TARE] offset_kPa=");
  Serial.println(offset_kPa, 6);
}

/* ============================================================
   MQTT FUNCTIONS
   ============================================================ */
void mqttEnsure(){
  if(mqtt.connected()) return;

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);

  if(mqtt.connect("esp32-dp")){
    Serial.println("[MQTT] Connected");
  } else {
    Serial.print("[MQTT] Failed, state=");
    Serial.println(mqtt.state());
  }
}

bool mqttPublishSGandLevel(float sg, float level_cm){
  if(!mqtt.connected()) return false;

  char buf[120];
  snprintf(buf, sizeof(buf),
    "{\"sg\":%.3f,\"samples\":%.1f}",
     sg, level_cm);

  return mqtt.publish(MQTT_TOPIC, buf, false);
}

bool mqttPublishSaveCmd(){
  if(!mqtt.connected()) return false;
  return mqtt.publish(MQTT_TOPIC, "{\"cmd\":\"save\"}", false);
}

/* ============================================================
   WiFi
   ============================================================ */
void wifiConnect(){
  if(WiFi.status() == WL_CONNECTED) return;

  Serial.println("[WiFi] Connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while(WiFi.status() != WL_CONNECTED){
    delay(300);
    Serial.print(".");
  }

  Serial.println("\n[WiFi] Connected!");
}

/* ============================================================
   SETUP
   ============================================================ */
void setup(){
  pinMode(BTN_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  delay(200);

  Wire.begin(SDA_SG, SCL_SG, 400000);
  Wire1.begin(SDA_LV, SCL_LV, 100000);

  Serial2.begin(NEXTION_BAUD, SERIAL_8N1, UART2_RX, UART2_TX);
  nextionCmd("bkcmd=1");

  wifiConnect();
  mqttEnsure();

  Serial.println("[SG] Performing TARE...");
  do_tare(120);   // 기존 코드와 동일

  calibrateZero_LV();

  Serial.println("System Ready: SG + LEVEL + Button");
}

/* ============================================================
   LOOP
   ============================================================ */
void loop(){

  /* ============================================================
     SG 측정 (기존코드와 100% 동일)
     ============================================================ */
  uint8_t d[7]; uint32_t p24, t24; uint8_t st;
  float sum_kPa = 0; int got = 0;

  for(int i=0;i<30;i++){
    if(!trigger_one_shot()) continue;
    if(read_ready_SG(d, p24, t24, st)){
      sum_kPa += sg_counts_to_kPa(p24);
      got++;
    }
    delay(10);
  }

  float sg = 0.0f;

  if(got > 0){

    float mean_kPa      = sum_kPa / got;
    float corrected_kPa = mean_kPa - offset_kPa;     // TARE 적용
    float dP_raw_Pa     = kPa_to_Pa(corrected_kPa);
    float dP_cal_Pa     = dP_raw_Pa * calib_gain;    // 캘리브 게인 적용
    float rho_est       = dP_cal_Pa / (G * HEAD_M);
    sg                  = rho_est / 997.0f;

  } else {
    sg = 0;
  }

  /* ============================================================
     LEVEL 측정
     ============================================================ */
  float sumP = 0, sumT = 0; 
  int good = 0;
  float P,T;

  for(int i=0;i<10;i++){
    if(readOnce_LV(P,T)){
      sumP += P;
      sumT += T;
      good++;
    }
    delay(5);
  }

  float level_cm = 0.0f;

  if(good > 0){
    float P_avg = sumP/good - P_ZERO_LV;
    if(P_avg < 0) P_avg = 0;

    float depth_m = (P_avg * 1000.0f) / (RHO_WATER_LV * G_ACCEL);
    float level_m = SENSOR_FROM_BOTTOM_M + depth_m;

    if(level_m > CONTAINER_FULL_HEIGHT_M)
      level_m = CONTAINER_FULL_HEIGHT_M;

    level_cm = level_m * 100.0f;
  }

  /* ============================================================
     SERIAL 출력
     ============================================================ */
  Serial.print("[SG] ");
  Serial.print(sg, 3);
  Serial.print("  |  [LEVEL] ");
  Serial.print(level_cm, 1);
  Serial.println(" cm");

  /* ============================================================
     Nextion 출력
     ============================================================ */
  nextionSetNum("n0", (long)lround(sg));
  nextionSetNum("n1", (long)lround(level_cm));

  /* ============================================================
     MQTT 전송
     ============================================================ */
  mqttEnsure();
  mqtt.loop();
  mqttPublishSGandLevel(sg, level_cm);

  /* ============================================================
     버튼 저장 명령
     ============================================================ */
  bool btnNow = digitalRead(BTN_PIN);

  if(lastBtn == HIGH && btnNow == LOW){
    Serial.println("[BTN] PRESSED → SAVE CMD");
    mqttPublishSaveCmd();
  }

  lastBtn = btnNow;

  delay(200);
}
