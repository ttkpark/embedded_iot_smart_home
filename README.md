# 능동형 환자 맞춤 환경 제어 시스템

> ESP-NOW 기반 의료용 스마트홈 반응형 제어 시스템 (시연용)

---

## 프로젝트 개요

병실 환경에서 환자의 이상 상태(낙상, 호흡 이상, 고열 등)가 감지될 때, **ESP-NOW 엣지 통신망**을 통해 환경 장치(환풍기·에어컨·창문)를 자동 제어하고, 웹 대시보드로 실시간 모니터링 및 수동 조작을 지원하는 임베디드 IoT 시스템입니다.

## 시스템 구성 (3-Node 구조)

| 노드 | MCU | 역할 |
|------|-----|------|
| **Node A** (Central) | ESP32-WROOM | Wi-Fi 웹 서버 + 대시보드 호스팅 + ESP-NOW 중계 |
| **Node B** (Actuator) | ESP32-LoRa / Pico | 릴레이(환풍기), IR(에어컨), 모터(창문) 제어 |
| **Node C** (Sensor Mock) | ESP32-LoRa / Pico | 물리 버튼으로 정상/응급 상태 시뮬레이션 |

## 핵심 기술

- **ESP-NOW:** 공유기 없이 MAC 주소 기반 직접 통신, 지연 시간 수 ms 이내
- **ESPAsyncWebServer + WebSocket:** 실시간 대시보드 양방향 통신
- **IRremoteESP8266:** 에어컨 적외선 원격 제어
- **LittleFS:** ESP32 플래시에 웹 파일(HTML/CSS/JS) 저장

## 시연 시나리오

```
① Node C '응급' 버튼 누름
② Node C → Node A ESP-NOW 트리거 전송 (< 10ms)
③ 웹 대시보드 붉은 경고 점멸
④ Node A → Node B 자동 제어 명령 발송
⑤ 환풍기 ON, 에어컨 18°C, 창문 열림 자동 실행
```

## 대시보드 주요 기능

- 실시간 환자 상태 / 장치 상태 모니터링 (WebSocket)
- 환풍기·에어컨·창문 수동 제어 (토글·슬라이더·버튼)
- 타임스탬프 기반 이벤트 로그 (`[HH:MM:SS] 내용`)
- 응급 시 화면 테두리 붉은 점멸 (다크 모드 UI)

## 디렉터리 구조

```
embedded_iot_smart_home/
├── SPEC.md                   # 전체 개발 명세서
├── common/                   # 공통 헤더 (구조체, 핀 설정)
├── node_a_central/           # Node A 펌웨어 + 웹 파일
│   └── data/                 # LittleFS 업로드 대상
├── node_b_actuator/          # Node B 펌웨어
└── node_c_sensor_mock/       # Node C 펌웨어
```

## 문서

- **[SPEC.md](./SPEC.md)** — 하드웨어 핀맵, 통신 프로토콜, 펌웨어 요구사항, 테스트 시나리오 전체 포함
