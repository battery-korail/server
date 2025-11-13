// file: esp32_mqtt_dp.ino
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>

// ---------- WiFi / MQTT 설정 ----------
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASS";

// AWS public IP (EC2) or 도메인, 포트 1883
const char* mqtt_broker = "YOUR_AWS_PUBLIC_IP";
const uint16_t mqtt_port = 1883;
const char* mqtt_topic = "stm/dp";

WiFiClient espClient;
PubSubClient client(espClient);

// ---------- I2C / 센서 상수 (your code 그대로) ----------
static const uint8_t I2C_ADDR = 0x28;
static const int SDA_PIN = 21, SCL_PIN = 22;
static const float P_MIN_kPa = -0.498178f;
static const float P_MAX_kPa =  +0.498178f;
static const uint32_t OUT_MIN = 1677722;
static const uint32_t OUT_MAX = 15099494;
const uint8_t SAMPLES = 10;
const unsigned long PERIOD_MS = 500;
float offset_kPa = 0.0f;

// ---------- 유틸 함수 ----------
inline float counts_to_kPa(uint32_t c){
  return ((float)((int32_t)c - (int32_t)OUT_MIN)) * (P_MAX_kPa - P_MIN_kPa)
         / (float)(OUT_MAX - OUT_MIN) + P_MIN_kPa;
}
inline float kPa_to_Pa(float kPa){ return kPa * 1000.0f; }

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
void do_tare(uint16_t N=120){
  float s = 0.0f; uint16_t n = 0;
  uint8_t d[7]; uint32_t p,t; uint8_t st;
  for (uint16_t i=0;i<N;i++){
    if (!trigger_one_shot()) { delay(2); continue; }
    if (!read_ready(d, p, t, st)) { delay(2); continue; }
    s += counts_to_kPa(p);
    n++;
    delay(5);
  }
  if (n) offset_kPa = s / (float)n;
  Serial.print("[TARE] offset_kPa="); Serial.println(offset_kPa, 6);
}

// ---------- MQTT 연결 보장 ----------
void mqttReconnect(){
  while (!client.connected()) {
    if (client.connect("esp32_client")) {
      // connected
    } else {
      delay(2000);
    }
  }
}

// ---------- setup / loop ----------
void setup(){
  Serial.begin(115200);
  delay(200);

  Wire.begin(SDA_PIN, SCL_PIN, 400000);

  // WiFi connect
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  client.setServer(mqtt_broker, mqtt_port);

  do_tare(120);
}

void loop(){
  if (!client.connected()) mqttReconnect();
  client.loop();

  unsigned long cycleStart = millis();

  float sum_kPa = 0.0f;
  uint8_t got = 0;
  const uint16_t MAX_ATTEMPTS = 200;
  uint16_t attempts = 0;
  uint8_t d[7]; uint32_t p24, t24; uint8_t st;

  while (got < SAMPLES && attempts < MAX_ATTEMPTS){
    attempts++;
    if (!trigger_one_shot()){ delay(2); continue; }
    if (!read_ready(d, p24, t24, st)){ delay(2); continue; }
    float kPa = counts_to_kPa(p24);
    sum_kPa += kPa;
    got++;
    delay(10);
  }

  if (got > 0){
    float mean_kPa = sum_kPa / (float)got;
    float corrected_kPa = mean_kPa - offset_kPa;
    float dP_Pa = kPa_to_Pa(corrected_kPa);

    // JSON payload
    char payload[128];
    // include samples and timestamp (UNIX)
    snprintf(payload, sizeof(payload),
             "{\"dp_pa\": %.2f, \"samples\": %d, \"ts\": %lu}",
             dP_Pa, got, (unsigned long)time(NULL));

    client.publish(mqtt_topic, payload);
    Serial.println(payload);
  } else {
    Serial.println("No samples");
  }

  unsigned long elapsed = millis() - cycleStart;
  if (elapsed < PERIOD_MS) delay(PERIOD_MS - elapsed);
}
