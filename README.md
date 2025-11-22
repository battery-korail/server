# Korail Battery Monitoring System

ESP32 기반의 배터리 비중(SG)·액위(Level) 실시간 계측/표시/전송 시스템

ESP32가 I2C 압력 센서로부터 데이터를 수집하고 Nextion HMI에 표시하며, 측정값을 MQTT로 전송합니다. 백엔드는 MQTT 메시지를 수신해 웹소켓으로 프론트엔드에 푸시하고, 사용자가 원할 때 현재 값을 DB에 저장해 이력을 조회할 수 있습니다.

---
<img width="706" height="508" alt="스크린샷 2025-09-30 오후 12 25 10" src="https://github.com/user-attachments/assets/773b3702-16c6-47c7-a8fc-c425b9864300" />


## 프로젝트 개요

- **문제**: 철도·산업용 배터리 유지보수 현장에서 비중/액위 수치를 수기로 측정·기록 → 오류와 비효율.
- **해결**: 자체 제작 장비로 비중/액위 동시 측정 → ESP32가 실시간 전송 → 웹 대시보드로 시각화/저장.

목표:
- 센서 데이터 자동 측정 및 유효성 확인
- ESP32 → MQTT 브로커로 실시간 전송(JSON)
- 서버가 수신 값을 캐시·웹소켓 푸시, 요청 시 DB 저장
- 프론트엔드에서 실시간 그래프와 저장 이력 조회

---
## 시스템 아키텍처

```

[Sensors] 자체 제작
│
▼
[ESP32]
├─ I2C 수집(24-bit) → 필터링/보정(고정 게인·Tare)
├─ Nextion HMI 표시(UART2, n0:SG, n1:샘플수)
├─ MQTT Publish(JSON: {"sg","samples"})
└─ Wi‑Fi 연결 관리

▼                 ▼            

[MQTT Broker]   [Local Display]
└─ Mosquitto    └─ 배터리 상태 실시간 표시

▼
[Backend: Flask + Socket.IO]
├─ Paho-MQTT Subscriber
├─ 최신 측정 캐시 및 batteryUpdate 소켓 이벤트 푸시
├─ PostgreSQL 저장(dp_saved)
└─ REST API 제공

▼

[Database - PostgreSQL]
├─ 실시간 로그

▼
[Web Dashboard: Next.js + Chart.js]
├─ 실시간 SG/Level 그래프
└─ 현재 값 수동 저장 및 이력 페이지네이션 조회
```

---
## 주요 기능

- **디바이스(ESP32)**: ABP2 압력센서 계측, Tare, 고정 캘리브 게인 적용, Nextion 표시, MQTT 전송
- **백엔드(Flask)**: MQTT 구독/파싱, Socket.IO 실시간 푸시, DB 저장/조회 API
- **프론트엔드(Next.js)**: 실시간 그래프, 현재 값 저장 버튼, 저장 이력 테이블(페이지/정렬/날짜)

### MQTT 토픽/페이로드
- 토픽: `STM/DP` 또는 환경/설정의 `MQTT_TOPIC`(기본값: `stm/dp`)
- 페이로드(JSON):
```json
{"sg": 1.234, "samples": 30}
```

---

##  주요 기능

- ESP32가 모든 센서를 직접 제어 및 측정 
-  MQTT를 통한 무선 전송
- 백엔드가 MQTT 메시지 수신 후 DB 저장

### 대시보드 시각화

* 실시간 그래프 렌더링
* 이전 측정 저장, 이력 조회

### 디바이스(ESP32) 펌웨어
- `Arduino/config.example.h`를 복사해 `Arduino/config.h` 생성 후 값 설정:
  - `WIFI_SSID`, `WIFI_PASS`
  - `MQTT_BROKER`, `MQTT_PORT`, `MQTT_TOPIC`(기본: `stm/dp`)
- 하드웨어 연결(기본값):
  - I2C: SDA=21, SCL=22
  - Nextion UART2: TX2=GPIO17, RX2=GPIO16, Baud=115200
- 빌드/업로드 후 부팅 시 자동 Tare 수행, 주기적으로 MQTT에 SG 전송

---

---
## 기술 스택

### Device / Embedded
- **ESP32(Arduino Core)**: 메인 컨트롤러
- **Honeywell ABP2**: I2C 압력 센서(24-bit)
- **Nextion HMI**: 로컬 실시간 표시(UART2)
- **MQTT Publish**: SG·샘플수 전송(JSON)

### Backend
- **Flask + Flask-SocketIO + CORS**
- **Paho-MQTT**: 브로커 구독
- **PostgreSQL(psycopg2-binary)**: 이력 저장
- **python-dotenv**: 환경변수 관리

### Frontend
- **Next.js 14 + TypeScript**
- **Chart.js**: 실시간 그래프
- **TailwindCSS**: UI 스타일링

### Infra
- **Mosquitto**: MQTT Broker
- **PostgreSQL**: 데이터베이스

