# 능동형 환자 맞춤 환경 제어 시스템 — 개발 명세서

> **문서 버전:** v1.1  
> **최초 작성일:** 2026-03-03  
> **최종 수정일:** 2026-03-03  
> **목적:** 의료용 스마트홈 반응형 제어 시스템 구축을 위한 하드웨어·펌웨어·소프트웨어 전체 규격 정의  
> **개발 기간:** 2주 (Sprint 기반)

| 버전 | 날짜 | 변경 내용 |
|------|------|-----------|
| v1.0 | 2026-03-03 | 최초 작성 |
| v1.1 | 2026-03-03 | GPIO 핀 번호 확정 (Strapping Pin 회피), 대시보드 UI/UX 레이아웃 상세화, 색상 코드 업데이트 |

---

## 목차

1. [프로젝트 개요](#1-프로젝트-개요)
2. [ESP-NOW 배경지식 요약](#2-esp-now-배경지식-요약)
3. [시스템 아키텍처](#3-시스템-아키텍처)
4. [하드웨어 구성 및 핀맵](#4-하드웨어-구성-및-핀맵) *(GPIO 핀 번호 확정 포함)*
5. [통신 프로토콜 규격](#5-통신-프로토콜-규격)
6. [펌웨어 개발 요구사항](#6-펌웨어-개발-요구사항)
7. [대시보드 소프트웨어 요구사항](#7-대시보드-소프트웨어-요구사항)
8. [자동화 제어 로직 흐름](#8-자동화-제어-로직-흐름)
9. [개발 환경 및 라이브러리 목록](#9-개발-환경-및-라이브러리-목록)
10. [디렉터리 구조 (권장)](#10-디렉터리-구조-권장)
11. [테스트 시나리오](#11-테스트-시나리오)
12. [개발 일정 (Sprint 계획)](#12-개발-일정-sprint-계획)
13. [용어 정의](#13-용어-정의)

---

## 1. 프로젝트 개요

### 1.1 프로젝트명

**능동형 환자 맞춤 환경 제어 시스템 (Active Patient-Adaptive Environment Control System)**

### 1.2 목적

병실 환경에서 환자의 이상 상태(낙상, 호흡 이상, 고열 등)가 감지될 때, 담당자의 개입 없이 자동으로 환경 장치(환풍기·에어컨·창문)를 즉각 제어하여 2차 피해를 최소화하고, 웹 대시보드를 통해 원격으로 상황을 모니터링 및 수동 조작할 수 있는 시스템을 구축한다.

### 1.3 핵심 요구사항 요약

| 구분 | 요구사항 |
|------|----------|
| 반응 시간 | 이상 감지 → 액추에이터 동작까지 **100ms 이내** |
| 신뢰성 | 중앙 Wi-Fi 망 다운 시에도 ESP-NOW 엣지망으로 **독립 동작** |
| 인터페이스 | 웹 브라우저 기반 대시보드 (PC/모바일 반응형) |
| 데이터 전송 | ESP-NOW 기반 C 구조체 직렬 전송 (JSON 대신) |
| 시뮬레이션 | 물리 버튼 2개로 '정상' / '응급' 상태 모의 트리거 |

### 1.4 시연 시나리오

```
① Node C의 '응급' 버튼 누름
② Node C → Node A로 응급 트리거 구조체 전송
③ Node A 대시보드에 붉은 경고 점멸
④ Node A → Node B로 환경 제어 명령 자동 발송
⑤ Node B가 환풍기 ON, 에어컨 18°C 설정, 창문 열기 실행
⑥ 대시보드에서 관리자가 수동으로 장치 조작 가능
```

---

## 2. ESP-NOW 배경지식 요약

> 개발자 전달용 배경지식입니다. 구현 시 반드시 숙지하세요.

ESP-NOW는 공유기(AP)를 거치지 않고 ESP32 기기들끼리 **MAC 주소를 기반으로 직접 데이터를 주고받는 2.4GHz 무선 통신 프로토콜**입니다.

| 항목 | 내용 |
|------|------|
| 프로토콜 계층 | Wi-Fi Layer 2 (Data Link), IP 미사용 |
| 지연 시간(Latency) | **수 밀리초(ms) 이내** (TCP/IP 대비 극히 짧음) |
| 최대 페이로드 | **250 Bytes** (1회 전송 기준) |
| 연결 방식 | MAC 주소 등록(Peer 등록) 후 Unicast 또는 Broadcast |
| 동시 Peer 수 | 최대 20개 노드 페어링 가능 |
| Wi-Fi 공존 | `WIFI_AP_STA` 모드로 Wi-Fi + ESP-NOW 동시 사용 가능 |
| 보안 | PMK(Primary Master Key) / LMK(Local Master Key)로 암호화 가능 |

**본 프로젝트 사용 목적:**

- "환자 이상 감지 → 환경 제어" 간 즉각적인 반응성 보장
- 중앙 Wi-Fi 망 다운 시에도 독립적으로 작동하는 **신뢰성 높은 엣지(Edge) 제어망** 구축

---

## 3. 시스템 아키텍처

### 3.1 전체 구성도

```
┌─────────────────────────────────────────────────────────────────┐
│                        로컬 네트워크 (Wi-Fi)                      │
│                                                                   │
│   [브라우저 / 관리자 PC]  ←──── HTTP/WebSocket ────→  [Node A]   │
│                                                     (Central)    │
└─────────────────────────────────────────────────────────────────┘
                                    │ ↑
                          ESP-NOW   │ │  ESP-NOW
                        (명령 발송) │ │ (상태 수신)
                                    ↓ │
          ┌─────────────────────────────────────────┐
          │                                         │
     [Node B]                                  [Node C]
   (Actuator Node)                          (Sensor Mock Node)
   ESP32-LoRa / Pico                        ESP32-LoRa / Pico
          │                                         │
   ┌──────┼──────┐                          ┌───────┼───────┐
   │      │      │                          │               │
 SSR    IR LED  L293D                    버튼1(정상)  버튼2(응급)
릴레이  에어컨  모터드라이버
 환풍기        창문 리니어
              액추에이터
```

### 3.2 통신 방향 정의

| 방향 | 프로토콜 | 용도 |
|------|----------|------|
| Node C → Node A | ESP-NOW | 환자 상태 트리거 전송 |
| Node A → Node B | ESP-NOW | 환경 제어 명령 발송 |
| Node A ↔ 브라우저 | WebSocket (ws://) | 실시간 상태 업데이트 / 수동 명령 수신 |
| Node A ← 브라우저 | HTTP GET (ESPAsyncWebServer) | 대시보드 정적 파일 서빙 |

---

## 4. 하드웨어 구성 및 핀맵

### 4.1 Node A — Central & Dashboard Node

| 항목 | 내용 |
|------|------|
| **MCU** | ESP32-WROOM-32 |
| **Wi-Fi 역할** | `WIFI_AP_STA` 모드 — Station(AP 연결) + ESP-NOW 동시 구동 |
| **스토리지** | SPIFFS 또는 LittleFS (웹 파일 저장) |
| **외부 연결** | 공유기(AP)와 Wi-Fi 연결 |
| **전원** | USB 5V 또는 외부 3.3V 레귤레이터 |

#### 핀맵

| GPIO | 역할 | 비고 |
|------|------|------|
| `GPIO 2` | On-board LED (Output) | 시스템 정상 작동 및 통신 상태 표시용 깜빡임 |
| `GPIO 21` | I2C SDA (Optional) | 소형 OLED 디스플레이 연결 시 IP 주소 표시 가능 |
| `GPIO 22` | I2C SCL (Optional) | 소형 OLED 디스플레이 연결 시 IP 주소 표시 가능 |

> **[시연 Tip]** GPIO 21/22에 I2C OLED 디스플레이(SSD1306 등)를 추가하면 시연 현장에서 IP 주소 및 시스템 상태를 별도 시리얼 모니터 없이 확인할 수 있어 전문적인 인상을 줍니다.  
> Node A는 물리적 액추에이터 연결 없음. 펌웨어 + 웹 서버 구동에 집중.

### 4.2 Node B — Actuator Node (병실 제어기)

| 항목 | 내용 |
|------|------|
| **MCU** | ESP32-LoRa 또는 Raspberry Pi Pico (개발자 지정) |
| **ESP-NOW 역할** | Receiver (명령 수신 전용) |

#### 핀맵

| GPIO | 연결 부품 | 역할 | 동작 설명 |
|------|-----------|------|-----------|
| `GPIO 25` | SSR 릴레이 | 환풍기 ON/OFF (Active High) | HIGH = ON, LOW = OFF |
| `GPIO 26` | IR LED (NPN 트랜지스터 증폭 회로 포함) | 에어컨 IR 신호 송출 | PWM 지원 핀. NPN 트랜지스터 베이스에 연결. IRremoteESP8266으로 신호 출력 |
| `GPIO 27` | L293D 모터드라이버 IN1 | 창문 리니어 액추에이터 전진 (열림) | HIGH + IN2 LOW = 전진 |
| `GPIO 14` | L293D 모터드라이버 IN2 | 창문 리니어 액추에이터 후진 (닫힘) | LOW + IN2 HIGH = 후진 |

> **[Hardware Tip]** GPIO 25, 27, 14에는 **10kΩ 풀다운 저항을 물리적으로 부착**할 것. 전원 인가 순간 핀이 플로팅 상태가 되어 릴레이나 모터가 순간 오작동(Glitch)하는 현상을 완벽히 차단합니다.

#### IR 회로 구성 (참고)

```
ESP32 GPIO 26 ──[330Ω]──→ NPN 트랜지스터(베이스)
                             │
                           컬렉터 ──→ IR LED 애노드
                             │
                           에미터 ──→ GND

IR LED 캐소드 ──→ GND
IR LED 애노드 ──→ 컬렉터 (전류 제한 저항 포함)
```

#### 모터 드라이버 L293D 연결 (참고)

```
IN1(GPIO 27) = HIGH, IN2(GPIO 14) = LOW  → 전진 (창문 열림)
IN1(GPIO 27) = LOW,  IN2(GPIO 14) = HIGH → 후진 (창문 닫힘)
IN1(GPIO 27) = LOW,  IN2(GPIO 14) = LOW  → 정지
```

### 4.3 Node C — Sensor Mock Node (가상 센서)

| 항목 | 내용 |
|------|------|
| **MCU** | ESP32-LoRa 또는 Raspberry Pi Pico (개발자 지정) |
| **ESP-NOW 역할** | Transmitter (트리거 전송 전용) |

#### 핀맵

| GPIO | 연결 부품 | 역할 |
|------|-----------|------|
| `GPIO 32` | Tactile Switch (내부 풀업 사용) | 정상 상태(Normal) 전송 버튼 — 버튼 클릭 시 GND 쇼트 |
| `GPIO 33` | Tactile Switch (내부 풀업 사용) | 응급 상태(Emergency — 호흡/낙상) 전송 버튼 — 버튼 클릭 시 GND 쇼트 |

> 버튼은 **내부 풀업(`INPUT_PULLUP`)** 사용. 누를 때 LOW 신호. GPIO 32/33은 ADC1 채널이며 입출력 모두 안전하게 사용 가능한 핀입니다.

### 4.4 GPIO 핀 선정 근거 및 주의사항

#### Strapping Pin (부팅 시 특수 동작 핀) — 반드시 회피

아래 핀들은 ESP32 부팅 모드 결정에 사용되므로, **출력용 액추에이터(릴레이·모터)에 절대 사용하지 말 것.**

| GPIO | 부팅 시 역할 | 위험성 |
|------|-------------|--------|
| `GPIO 0` | 부팅 모드 선택 (LOW = 다운로드 모드) | 릴레이 연결 시 부팅 실패 |
| `GPIO 2` | 부팅 로그 출력 (NODE A에서 LED 전용 용도만 허용) | 출력 기기 연결 금지 |
| `GPIO 5` | SPI CS 기본값, 부팅 시 HIGH 출력 | 부팅 순간 릴레이 오작동 |
| `GPIO 12` | 플래시 전압 선택 (LOW여야 정상 부팅) | 연결 시 부팅 불가 위험 |
| `GPIO 15` | JTAG, 부팅 시 HIGH 출력 | 부팅 순간 모터 순간 구동 |

#### 입력 전용 핀 (Output 불가) — 센서/버튼 외 사용 금지

| GPIO | 제한 사항 |
|------|-----------|
| `GPIO 34` | 입력 전용, 내부 풀업/풀다운 없음 |
| `GPIO 35` | 입력 전용, 내부 풀업/풀다운 없음 |
| `GPIO 36` | 입력 전용 (VP) |
| `GPIO 39` | 입력 전용 (VN) |

#### 확정 핀 선정 요약

| 노드 | GPIO | 용도 | 선정 이유 |
|------|------|------|-----------|
| Node A | 2 | On-board LED | Strapping Pin이나 저전류 LED는 영향 없음 |
| Node B | 25 | 환풍기 SSR | 부팅 시 안전, 풀다운 저항으로 Glitch 방지 |
| Node B | 26 | IR LED (PWM) | PWM 지원, 부팅 시 LOW 유지, 안전 |
| Node B | 27 | 모터 IN1 (열림) | 부팅 시 안전, 풀다운 저항 필수 |
| Node B | 14 | 모터 IN2 (닫힘) | 부팅 시 안전, 풀다운 저항 필수 |
| Node C | 32 | 정상 버튼 | ADC1, 입출력 가능, 내부 풀업 지원 |
| Node C | 33 | 응급 버튼 | ADC1, 입출력 가능, 내부 풀업 지원 |

---

## 5. 통신 프로토콜 규격

### 5.1 ESP-NOW Payload 구조체

데이터 파싱 시간 최소화를 위해 JSON 대신 **C/C++ 고정 크기 구조체(Struct)** 를 사용합니다.

```cpp
// 파일: common/struct_message.h
// 모든 노드 공통 사용 — 변경 시 전체 노드 재컴파일 필요

typedef struct struct_message {
    uint8_t msg_type;     // 메시지 유형
                          //   0: 상태 보고 (Keep-alive, 주기 5초)
                          //   1: 센서 트리거 (Node C → Node A)
                          //   2: 제어 명령   (Node A → Node B)

    uint8_t node_id;      // 발신 노드 ID
                          //   1: Node A (Central)
                          //   2: Node B (Actuator)
                          //   3: Node C (Sensor Mock)

    uint8_t patient_stat; // 환자 상태
                          //   0: Normal (정상)
                          //   1: Emergency (응급 — 호흡 이상 / 낙상)

    uint8_t fan_state;    // 환풍기 상태
                          //   0: OFF
                          //   1: ON

    uint8_t ac_temp;      // 에어컨 설정 온도
                          //   0: 에어컨 OFF
                          //   18~30: 설정 온도(°C)

    uint8_t window_act;   // 창문 액추에이터 동작
                          //   0: 정지 (현 상태 유지)
                          //   1: 열림 (리니어 액추에이터 전진)
                          //   2: 닫힘 (리니어 액추에이터 후진)
} struct_message;

// 구조체 크기: 6 Bytes (ESP-NOW 250B 제한 대비 여유)
```

### 5.2 메시지 유형별 사용 규칙

| msg_type | 발신 노드 | 수신 노드 | 유효 필드 | 설명 |
|----------|-----------|-----------|-----------|------|
| `0` (Keep-alive) | A, B, C | 대상 노드 | `node_id` | 5초 주기 생존 확인 |
| `1` (센서 트리거) | Node C | Node A | `node_id`, `patient_stat` | 버튼 누름 이벤트 |
| `2` (제어 명령) | Node A | Node B | `node_id`, `fan_state`, `ac_temp`, `window_act` | 자동/수동 제어 명령 |

### 5.3 MAC 주소 등록

각 노드의 MAC 주소는 첫 번째 시리얼 부팅 시 출력되는 값을 수동으로 기록하여 `config.h`에 등록합니다.

```cpp
// 파일: common/config.h

// ── MAC 주소 (최초 부팅 시 시리얼 모니터에서 확인 후 기입) ──────────────
#define MAC_NODE_A  {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX}
#define MAC_NODE_B  {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX}
#define MAC_NODE_C  {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX}

// ── Node A GPIO ────────────────────────────────────────────────────────────
#define PIN_STATUS_LED   2   // On-board LED (시스템 상태 표시)
// Optional I2C OLED
#define PIN_OLED_SDA     21
#define PIN_OLED_SCL     22

// ── Node B GPIO ────────────────────────────────────────────────────────────
#define PIN_FAN_RELAY    25  // SSR 릴레이 — 환풍기 (Active High, 풀다운 10kΩ)
#define PIN_IR_LED       26  // IR LED — 에어컨 (NPN 증폭 회로, PWM 지원)
#define PIN_MOTOR_IN1    27  // L293D IN1 — 창문 전진/열림 (풀다운 10kΩ)
#define PIN_MOTOR_IN2    14  // L293D IN2 — 창문 후진/닫힘 (풀다운 10kΩ)
#define MOTOR_RUN_MS     3000  // 창문 완전 개폐 소요 시간(ms) — 캘리브레이션 필요

// ── Node C GPIO ────────────────────────────────────────────────────────────
#define PIN_BTN_NORMAL    32  // 정상(Normal) 버튼 — INPUT_PULLUP, FALLING 인터럽트
#define PIN_BTN_EMERGENCY 33  // 응급(Emergency) 버튼 — INPUT_PULLUP, FALLING 인터럽트
```

---

## 6. 펌웨어 개발 요구사항

### 6.1 Node A — Central & Dashboard Node

#### 6.1.1 Wi-Fi 설정

```cpp
// WIFI_AP_STA 모드: 로컬 AP 연결(웹 서버) + ESP-NOW 동시 구동
WiFi.mode(WIFI_AP_STA);
WiFi.begin(SSID, PASSWORD);
// ESP-NOW 초기화는 WiFi.begin() 이후에 수행
esp_now_init();
```

#### 6.1.2 비동기 웹 서버

- **라이브러리:** `ESPAsyncWebServer` + `AsyncTCP`
- **정적 파일:** LittleFS에 저장된 `index.html`, `style.css`, `app.js` 서빙
- **WebSocket 경로:** `ws://[NodeA_IP]/ws`

#### 6.1.3 자동 제어 로직 (ESP-NOW 수신 콜백)

```cpp
void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    struct_message incoming;
    memcpy(&incoming, data, sizeof(incoming));

    if (incoming.msg_type == 1 && incoming.patient_stat == 1) {
        // ① 대시보드에 응급 경고 WebSocket 브로드캐스트
        ws.textAll("{\"alert\":\"EMERGENCY\",\"patient_stat\":1}");

        // ② Node B에 환경 제어 명령 발송
        struct_message cmd;
        cmd.msg_type    = 2;
        cmd.node_id     = 1;         // Node A 발신
        cmd.fan_state   = 1;         // 환풍기 ON
        cmd.ac_temp     = 18;        // 에어컨 18°C
        cmd.window_act  = 1;         // 창문 열기
        esp_now_send(MAC_NODE_B, (uint8_t*)&cmd, sizeof(cmd));

        // ③ 이벤트 로그 기록
        logEvent("환자 이상 감지 - 자동 환경 제어 실행");
    }
}
```

#### 6.1.4 수동 제어 (WebSocket 수신 핸들러)

브라우저에서 JSON 형태로 수동 명령이 오면 파싱 후 Node B로 제어 명령 발송:

```json
// 브라우저 → Node A WebSocket 명령 포맷
{"cmd":"fan","value":1}
{"cmd":"ac","value":24}
{"cmd":"window","value":2}
```

#### 6.1.5 이벤트 로그 관리

- 최대 50개 로그 항목을 순환 버퍼(Ring Buffer)로 메모리에 유지
- 로그 형식: `[HH:MM:SS] 메시지 내용`
- 대시보드 접속 시 전체 로그 초기 전송, 이후 신규 로그만 WebSocket으로 push

---

### 6.2 Node B — Actuator Node

#### 6.2.1 ESP-NOW 수신 콜백

```cpp
void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    struct_message cmd;
    memcpy(&cmd, data, sizeof(cmd));

    if (cmd.msg_type != 2) return;  // 제어 명령만 처리

    // 환풍기 제어
    digitalWrite(PIN_FAN_RELAY, cmd.fan_state ? HIGH : LOW);

    // 에어컨 IR 제어
    if (cmd.ac_temp == 0) {
        sendAcOff();          // 에어컨 끄기 IR 코드 전송
    } else {
        sendAcTemp(cmd.ac_temp);  // 온도 설정 IR 코드 전송
    }

    // 창문 모터 제어
    controlWindow(cmd.window_act);  // 0:정지, 1:열림, 2:닫힘
}
```

#### 6.2.2 IR 에어컨 제어

- **라이브러리:** `IRremoteESP8266`
- **사전 작업:** IR 리시버 모듈로 실제 에어컨 리모컨 신호를 캡처하여 코드 확보
- 에어컨 브랜드별 프로토콜 클래스 사용 (예: `IRDaikinESP`, `IRSamsungAc`, `IRLGAc` 등)
- 지원 브랜드 확인: [IRremoteESP8266 지원 목록](https://github.com/crankyoldgit/IRremoteESP8266/blob/master/SupportedProtocols.md)

#### 6.2.3 창문 제어 함수

```cpp
void controlWindow(uint8_t action) {
    switch(action) {
        case 0:  // 정지
            digitalWrite(PIN_MOTOR_IN1, LOW);
            digitalWrite(PIN_MOTOR_IN2, LOW);
            break;
        case 1:  // 열림 (전진)
            digitalWrite(PIN_MOTOR_IN1, HIGH);
            digitalWrite(PIN_MOTOR_IN2, LOW);
            delay(MOTOR_RUN_MS);  // 이동 완료 후 자동 정지
            digitalWrite(PIN_MOTOR_IN1, LOW);
            break;
        case 2:  // 닫힘 (후진)
            digitalWrite(PIN_MOTOR_IN1, LOW);
            digitalWrite(PIN_MOTOR_IN2, HIGH);
            delay(MOTOR_RUN_MS);
            digitalWrite(PIN_MOTOR_IN2, LOW);
            break;
    }
}
```

> `MOTOR_RUN_MS`는 실제 리니어 액추에이터 이동 거리에 맞게 캘리브레이션 필요.

---

### 6.3 Node C — Sensor Mock Node

#### 6.3.1 버튼 디바운싱

인터럽트(ISR) + 타이머 방식으로 디바운싱 처리합니다.

```cpp
#define DEBOUNCE_MS 200

volatile unsigned long lastPressTime = 0;
volatile uint8_t pendingStatus = 255;  // 255 = 없음

void IRAM_ATTR ISR_NormalButton() {
    if (millis() - lastPressTime > DEBOUNCE_MS) {
        lastPressTime = millis();
        pendingStatus = 0;  // 정상
    }
}

void IRAM_ATTR ISR_EmergencyButton() {
    if (millis() - lastPressTime > DEBOUNCE_MS) {
        lastPressTime = millis();
        pendingStatus = 1;  // 응급
    }
}

// setup()에서 인터럽트 등록
attachInterrupt(digitalPinToInterrupt(PIN_BTN_NORMAL),   ISR_NormalButton,   FALLING);
attachInterrupt(digitalPinToInterrupt(PIN_BTN_EMERGENCY), ISR_EmergencyButton, FALLING);
```

#### 6.3.2 트리거 전송

```cpp
void loop() {
    if (pendingStatus != 255) {
        struct_message msg;
        msg.msg_type    = 1;               // 센서 트리거
        msg.node_id     = 3;               // Node C
        msg.patient_stat = pendingStatus;  // 0: 정상, 1: 응급

        esp_now_send(MAC_NODE_A, (uint8_t*)&msg, sizeof(msg));
        pendingStatus = 255;  // 초기화
    }
}
```

---

## 7. 대시보드 소프트웨어 요구사항

Node A의 LittleFS에 저장되어 구동되는 **경량 단일 페이지 앱(SPA)** 으로 개발합니다.  
외부 CDN 의존 없이 모든 에셋을 LittleFS에 포함해야 합니다.

### 7.1 파일 구성

```
data/
├── index.html       # 메인 페이지 (단일 파일)
├── style.css        # 다크 모드 전용 스타일시트
└── app.js           # WebSocket 로직 + UI 제어
```

### 7.2 UI/UX 디자인 지침

| 항목 | 규격 |
|------|------|
| **테마** | 다크 모드(Dark Mode) 전용 — 배경: `#1E1E1E` (차분한 다크 그레이) |
| **응급 시각 효과** | 화면 테두리(`border`) 및 상단 배너가 **붉은색(`#FF1744`)으로 1초 주기 점멸** (CSS `@keyframes` 사용) |
| **폰트** | 시스템 폰트 또는 `Roboto` (LittleFS 용량 고려 CDN 사용 최소화) |
| **반응형** | CSS Grid/Flexbox 사용, 모바일(375px) ~ 데스크탑(1440px) 대응 |
| **색상 코드** | 정상: `#00E676` (형광 그린), 응급: `#FF1744` (경고 레드), 비활성: `#FFA726` (주황) |
| **레이아웃 구조** | 좌측: 환자 상태 관제 뷰 / 우측: 즉각 개입 액션 뷰 / 하단: 터미널형 이벤트 로그 |

### 7.3 레이아웃 구성

```
================================================================================
[ 헤더 ]  SmartCare+ Ambient System            시스템 상태: ● Online (ESP-NOW)
================================================================================
[ 좌측 패널: 실시간 환자 모니터링 ]      | [ 우측 패널: 환경 제어 (Manual/Auto) ]
                                         |
 +------------------------------------+  |  [ 에어컨 제어 ] ------------------+
 |                                    |  |   상태: 켜짐                       |
 |        (가상 카메라 뷰)             |  |   온도: [ - ]  22°C  [ + ]        |
 |     Patient Room #101              |  |   모드: [ 자동 ] [ 수동 ]          |
 |                                    |  +------------------------------------+
 |   현재 상태: ● 안정 (Stable)        |  |
 |   (응급 시 ● 붉은 경고 점멸)         |  |  [ 환기 시스템 ] ------------------+
 |                                    |  |   환풍기 전원: [ ON ] / [ OFF ]    |
 +------------------------------------+  |   공기질: 양호                     |
                                         +------------------------------------+
 [ 센서 데이터 요약 ]                     |
   실내 온도: 24°C                        |  [ 창문 개폐 제어 ] ----------------+
   활동량 감지: 낮음                      |   현재 상태: 닫힘                   |
                                         |   액션: [ 열기 ]  [ 닫기 ]  [ 정지 ]|
                                         +------------------------------------+
================================================================================
[ 하단 패널: 실시간 시스템 이벤트 로그 (Console) ]
================================================================================
> [13:42:01] System Boot... ESP-NOW Network Ready.
> [13:42:15] Node B (Actuator) 연결 확인됨.
> [13:45:22] ⚠ [응급 이벤트 수신] 환자 낙상 감지 (Node C)
> [13:45:22] ⚡ 환경 제어 개입: 에어컨(22도) ON, 환풍기 ON, 창문 개방
> [13:45:23] 명령 전송 완료 (Latency: 4ms)
================================================================================
```

#### 대시보드 UI 핵심 포인트 (프론트엔드 개발자 지시용)

1. **레이아웃 분리 (Grid):** 좌측은 환자 상태를 한눈에 파악하는 '관제 뷰', 우측은 즉각적인 개입이 가능한 '액션 뷰'로 명확히 분리. CSS Grid `grid-template-columns: 1fr 1fr` 사용 권장.
2. **응급 점멸 효과:** CSS `@keyframes` 로 `border-color`를 `#FF1744` ↔ `transparent`로 1초 주기로 전환. 응급 해제 시 즉시 animation 제거.
3. **이벤트 로그 터미널 스타일:** 하단 로그 패널은 `overflow-y: scroll` + `font-family: monospace` 로 터미널 창처럼 구현. 신규 로그는 WebSocket으로 수신하여 기존 목록 상단에 **append** (페이지 새로고침 없이). 응급 관련 로그는 `color: #FF1744` 강조.

### 7.4 필수 구현 기능 상세

#### 기능 1. 실시간 모니터링 패널

- WebSocket 연결 (`ws://[NodeA_IP]/ws`) 수립 및 자동 재연결 로직 포함
- 수신 JSON 파싱 후 DOM 즉시 업데이트
- 연결 끊김 시 상태 패널에 `⚠ 연결 끊김` 표시

#### 기능 2. 수동 제어 패널

| 장치 | UI 컴포넌트 | 전송 JSON |
|------|-------------|-----------|
| 환풍기 | 토글 스위치 (ON/OFF) | `{"cmd":"fan","value":0}` or `1` |
| 에어컨 | 슬라이더 (18~30°C) + OFF 버튼 | `{"cmd":"ac","value":24}` or `0` |
| 창문 | 버튼 3개 (열기/닫기/정지) | `{"cmd":"window","value":1}` or `2` or `0` |

#### 기능 3. 이벤트 로그

- 화면 하단 스크롤 가능한 리스트 영역
- 최신 로그가 상단에 표시 (역순 정렬)
- 각 로그 항목 포맷: `[HH:MM:SS] 메시지 내용`
- 응급 관련 로그는 **적색** 텍스트로 강조
- 최대 50개 유지 (초과 시 오래된 항목 자동 삭제)

### 7.5 WebSocket 수신 JSON 규격 (Node A → 브라우저)

```json
// 상태 전체 업데이트
{
  "type": "status",
  "patient_stat": 0,
  "fan_state": 1,
  "ac_temp": 18,
  "window_act": 1
}

// 응급 경고
{
  "type": "alert",
  "alert": "EMERGENCY",
  "patient_stat": 1
}

// 로그 추가
{
  "type": "log",
  "time": "14:22:15",
  "message": "환자 이상 감지 - 환풍기 자동 가동",
  "level": "emergency"
}
```

---

## 8. 자동화 제어 로직 흐름

### 8.1 응급 상황 자동 제어 흐름

```
Node C 응급 버튼 누름
    │
    ▼
ISR 디바운싱 처리
    │
    ▼
ESP-NOW 전송: {msg_type=1, node_id=3, patient_stat=1} → Node A
    │
    ▼ (< 10ms)
Node A ESP-NOW 수신 콜백(OnDataRecv) 실행
    │
    ├─→ WebSocket 브로드캐스트 (경고 JSON) → 브라우저 화면 붉은 점멸
    │
    ├─→ 이벤트 로그 기록: "[XX:XX:XX] 환자 이상 감지 - 자동 제어 실행"
    │
    └─→ ESP-NOW 전송: {msg_type=2, fan=1, ac=18, window=1} → Node B
            │
            ▼ (< 10ms)
        Node B OnDataRecv 실행
            │
            ├─→ GPIO 25 HIGH → SSR 릴레이 ON → 환풍기 가동
            ├─→ GPIO 26 IR 신호 송출 → 에어컨 18°C 설정
            └─→ GPIO 27 HIGH, GPIO 14 LOW → 리니어 액추에이터 전진 → 창문 열림
```

### 8.2 수동 제어 흐름

```
관리자 브라우저 클릭
    │
    ▼
WebSocket 명령 JSON 전송 → Node A
    │
    ▼
Node A WebSocket 핸들러 파싱
    │
    ▼
ESP-NOW 제어 명령 전송 → Node B
    │
    ▼
Node B 하드웨어 제어
    │
    ▼
Node B → (Keep-alive로 상태 보고) → Node A → WebSocket 브로드캐스트 → 브라우저 UI 업데이트
```

### 8.3 정상 복귀 흐름

```
Node C 정상 버튼 누름
    │
    ▼
ESP-NOW 전송: {msg_type=1, patient_stat=0} → Node A
    │
    ▼
Node A: 경고 해제, 대시보드 정상 상태로 복귀
    │
    └─→ 자동 복귀 여부는 관리자 설정 (기본: 수동 복귀)
        → 수동 복귀 시 대시보드의 '경고 해제' 버튼으로 처리
```

---

## 9. 개발 환경 및 라이브러리 목록

### 9.1 개발 환경

| 항목 | 내용 |
|------|------|
| **IDE** | Arduino IDE 2.x 또는 VS Code + PlatformIO |
| **SDK** | ESP32 Arduino Core 2.x 이상 |
| **플래시 도구** | esptool.py (LittleFS 업로드) |
| **시리얼 모니터** | 115200 baud |

### 9.2 필수 라이브러리

| 라이브러리 | 버전 | 용도 | 설치 방법 |
|------------|------|------|-----------|
| `ESPAsyncWebServer` | 최신 | Node A 비동기 웹 서버 | Arduino Library Manager |
| `AsyncTCP` | 최신 | ESPAsyncWebServer 의존성 | Arduino Library Manager |
| `IRremoteESP8266` | 최신 | Node B 에어컨 IR 제어 | Arduino Library Manager |
| `LittleFS` | 내장 | Node A 파일 시스템 | ESP32 Core 내장 |
| `esp_now.h` | 내장 | 전 노드 ESP-NOW 통신 | ESP32 Core 내장 |
| `WiFi.h` | 내장 | Node A Wi-Fi 연결 | ESP32 Core 내장 |

---

## 10. 디렉터리 구조 (권장)

```
embedded_iot_smart_home/
│
├── README.md                    # 프로젝트 개요
├── SPEC.md                      # 본 개발 명세서
│
├── common/
│   ├── struct_message.h         # 공통 ESP-NOW 구조체
│   └── config.h                 # MAC 주소, GPIO 핀 번호 상수 정의
│
├── node_a_central/
│   ├── node_a_central.ino       # Node A 메인 펌웨어
│   └── data/                    # LittleFS 업로드 대상 폴더
│       ├── index.html
│       ├── style.css
│       └── app.js
│
├── node_b_actuator/
│   └── node_b_actuator.ino      # Node B 메인 펌웨어
│
└── node_c_sensor_mock/
    └── node_c_sensor_mock.ino   # Node C 메인 펌웨어
```

---

## 11. 테스트 시나리오

### TC-01: 응급 트리거 → 자동 제어 End-to-End

| 단계 | 동작 | 기대 결과 | 허용 시간 |
|------|------|-----------|-----------|
| 1 | Node C 응급 버튼 누름 | ESP-NOW 패킷 전송 | 즉시 |
| 2 | Node A 수신 | 응급 JSON → WebSocket 브로드캐스트 | < 50ms |
| 3 | 브라우저 | 화면 붉은 점멸 시작, 상태 업데이트 | < 100ms |
| 4 | Node B 수신 | 환풍기 ON, 에어컨 IR 송출, 창문 열림 | < 100ms |

### TC-02: 수동 제어 동작 확인

| 단계 | 동작 | 기대 결과 |
|------|------|-----------|
| 1 | 브라우저에서 환풍기 토글 OFF | WebSocket 명령 전송 |
| 2 | Node B | 릴레이 GPIO LOW → 환풍기 OFF |
| 3 | 브라우저 | 환풍기 상태 표시 OFF로 업데이트 |

### TC-03: 정상 복귀 동작

| 단계 | 동작 | 기대 결과 |
|------|------|-----------|
| 1 | Node C 정상 버튼 누름 | ESP-NOW 정상 패킷 전송 |
| 2 | Node A 수신 | 경고 상태 해제 신호 브로드캐스트 |
| 3 | 브라우저 | 경고 배너 사라짐, 화면 정상 복귀 |

### TC-04: Wi-Fi 단절 시 ESP-NOW 독립 동작

| 단계 | 동작 | 기대 결과 |
|------|------|-----------|
| 1 | 공유기(AP) 전원 차단 | Wi-Fi 연결 끊김 |
| 2 | Node C 응급 버튼 | ESP-NOW는 여전히 동작 |
| 3 | Node B | 환경 제어 명령 수신 및 실행됨 |

### TC-05: 디바운싱 검증

| 단계 | 동작 | 기대 결과 |
|------|------|-----------|
| 1 | 버튼 빠르게 연속 5회 누름 | 패킷 1개만 전송 |
| 2 | 200ms 후 다시 누름 | 정상 1개 전송 |

---

## 12. 개발 일정 (Sprint 계획)

### Week 1: 하드웨어 + 펌웨어 코어

| Day | 작업 내용 |
|-----|-----------|
| Day 1 | 부품 배치, 배선, MAC 주소 확인, `config.h` 작성 |
| Day 2 | Node C 펌웨어 — 버튼 디바운싱 + ESP-NOW 전송 구현 및 테스트 |
| Day 3 | Node A 펌웨어 — ESP-NOW 수신 + 자동 제어 로직 구현 |
| Day 4 | Node B 펌웨어 — 수신 콜백 + 릴레이/모터 제어 구현 |
| Day 5 | IR 신호 캡처 + Node B IR 제어 통합, TC-01 ~ TC-05 기본 테스트 |

### Week 2: 대시보드 + 통합 테스트

| Day | 작업 내용 |
|-----|-----------|
| Day 6 | Node A 비동기 웹 서버 + WebSocket 구현 |
| Day 7 | 대시보드 HTML/CSS 레이아웃 구현 (다크 모드, 모니터링 패널) |
| Day 8 | 대시보드 JS — WebSocket 연동, 수동 제어 패널 구현 |
| Day 9 | 이벤트 로그 기능 구현, 응급 점멸 효과 완성 |
| Day 10 | 전체 통합 테스트, 버그 수정, 시연 리허설 |

---

## 13. 용어 정의

| 용어 | 정의 |
|------|------|
| **ESP-NOW** | ESP32 전용 피어-투-피어 무선 프로토콜 (Wi-Fi Layer 2) |
| **Node** | 본 시스템에서 독립적인 역할을 수행하는 ESP32 단말 |
| **SSR 릴레이** | Solid State Relay. 고전압/대전류 기기를 저전압 GPIO 신호로 제어하는 반도체 스위치 |
| **리니어 액추에이터** | 전기 모터의 회전 운동을 직선 운동으로 변환하는 장치 (창문 개폐에 사용) |
| **LittleFS** | ESP32의 플래시 메모리를 파일 시스템으로 사용하는 경량 라이브러리 |
| **WebSocket** | HTTP를 업그레이드하여 서버-클라이언트 간 양방향 실시간 통신을 지원하는 프로토콜 |
| **Keep-alive** | 노드가 정상 작동 중임을 주기적으로 알리는 생존 확인 메시지 |
| **디바운싱** | 기계식 버튼의 접촉 불안정으로 인한 중복 신호를 소프트웨어로 제거하는 기법 |
| **IR 캡처** | IR 리시버 모듈로 실제 리모컨 신호를 수신 및 분석하여 재사용 가능한 코드를 추출하는 작업 |

---

*본 명세서는 시스템 구현 과정에서 팀 합의에 따라 개정될 수 있습니다. 변경 시 버전 번호와 변경 이력을 본 문서 상단에 기재하세요.*
