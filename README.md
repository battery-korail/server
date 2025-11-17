# Korail Battery Monitoring System

ESP32 기반 실시간 배터리 계측·전송·모니터링 서비스

본 프로젝트는 배터리의 액위 · 비중 등 주요 지표를 센서를 통해 측정하고, ESP32가 이를 **MQTT 기반으로 실시간 전송**하여 웹 대시보드에서 모니터링할 수 있는 IoT 시스템입니다.

---

## 📑 Table of Contents

- [프로젝트 개요](#-프로젝트-개요)
- [시스템 아키텍처](#-시스템-아키텍처)
- [주요 기능](#-주요-기능)
- [기술 스택](#-기술-스택)
- [구성 요소 상세 설명](#-구성-요소-상세-설명)
- [프로젝트 구조](#-프로젝트-구조)

---

## 📌 프로젝트 개요

Korail 철도·산업용 배터리 유지보수 업무에서 **비효율적인 수동 측정 방식을 개선**하기 위해 개발한 프로젝트입니다.

기존에는 액위/비중/전압을 개별 장비로 측정하고 수기로 기록해야 했지만, 본 시스템은 다음을 목표로 합니다:

- 센서 데이터 **자동 측정**
- ESP32가 데이터를 **MQTT로 실시간 전송**
- 서버가 데이터를 **DB에 저장 후 상태 분석**
- 웹 대시보드에서 **실시간 그래프/로그 확인**

---
## 🧩 시스템 아키텍처

```

[Sensors]
│
▼

[ESP32]
├─ 센서 데이터 수집
├─ 필터링 및 유효성 검사
├─ MQTT Publish 
├─ Wi-Fi 연결 관리
└─ OTA Firmware Update
▼                 ▼            

[MQTT Broker]   [Local Display]
└─ Mosquitto    └─ 배터리 상태 실시간 표시

▼

[Backend - Flask]
├─ MQTT Subscriber
├─ 데이터 파싱 & Validation
├─ PostgreSQL 저장
└─ REST API 제공
▼

[Database - PostgreSQL]
├─ 실시간 로그
├─ 배터리별 측정값 저장
└─ 알람/이벤트 테이블
▼

[Web Dashboard - Next.js]
├─ 실시간 데이터 그래프
├─ 장비 상태 모니터링
└─ 알람/히스토리 조회

[Grafana]
└─ 장기 데이터 트렌드 분석

````

---

##  주요 기능

- ESP32가 모든 센서를 직접 제어 및 측정 
-  MQTT를 통한 무선 전송
- 백엔드가 MQTT 메시지 수신 후 DB 저장

### 대시보드 시각화

* 실시간 그래프 렌더링
* 이전 측정 저장, 이력 조회


---

## 기술 스택

### Device / Embedded

| 기술                              | 설명           |
| ------------------------------- | ------------ |
| ESP32 (Arduino Core)            | 메인 컨트롤러      |
| MQTT Publish                    | Telemetry 전송 |
| OTA 업데이트                        | 원격 펌웨어 업데이트  |

### Backend

| 기술             | 설명                       |
| -------------- | ------------------------ |
| Flask (Python) | 서버 API & MQTT Subscriber |
| Paho-MQTT      | MQTT 메시지 구독              |
| PostgreSQL     | 시계열 데이터 저장               |
| SQLAlchemy     | ORM, DB 모델링              |
| Docker         | 서버 배포 환경                 |


### Frontend

| 기술                  | 설명          |
| ------------------- | ----------- |
| Next.js 14          | 웹 대시보드      |
| TypeScript          | 안정적인 타입 환경  |
| TailwindCSS         | UI 스타일링     |
| Recharts / Chart.js | 실시간 그래프 시각화 |

### Infra

| 기술        | 설명          |
| --------- | ----------- |
| Mosquitto | MQTT Broker |
| Grafana   | 장기 데이터 분석   |




```
