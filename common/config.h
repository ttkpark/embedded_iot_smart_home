#pragma once

/* ── Wi-Fi ─────────────────────────────────────────────────────────────────
 * Node A가 연결할 공유기 SSID/PW.
 * ESPNOW_CHANNEL: 공유기 채널과 반드시 일치해야 함 (공유기 설정에서 확인).
 * ────────────────────────────────────────────────────────────────────────── */
#define WIFI_SSID        "YOUR_SSID"
#define WIFI_PASSWORD    "YOUR_PASSWORD"
#define ESPNOW_CHANNEL   1   /* 1~13 — 공유기 고정 채널로 변경 */

/* ── MAC 주소 ──────────────────────────────────────────────────────────────
 * 최초 부팅 시 시리얼 모니터(115200 baud)에서 출력되는 값을 확인 후 기입.
 * 예: I (xxx) NODE_A: MAC: AA:BB:CC:DD:EE:FF
 * ────────────────────────────────────────────────────────────────────────── */
#define MAC_NODE_A  {0x24, 0x6f, 0x28, 0x22, 0x39, 0x68}
#define MAC_NODE_B  {0x24, 0x6f, 0x28, 0x22, 0x37, 0xd8}
#define MAC_NODE_C  {0x30, 0xae, 0xa4, 0x84, 0x0d, 0x0c}

/* ── Node A GPIO ───────────────────────────────────────────────────────────*/
#define PIN_STATUS_LED   2    /* On-board LED */
#define PIN_OLED_SDA     21   /* Optional I2C OLED */
#define PIN_OLED_SCL     22

/* ── Node B GPIO ───────────────────────────────────────────────────────────
 * GPIO 25/27: 외부 10kΩ 풀다운 저항 부착 권장 (부팅 Glitch 방지)
 * SG90-HV Continuous는 50Hz PWM 제어를 사용.
 * ────────────────────────────────────────────────────────────────────────── */
#define PIN_FAN_RELAY    25   /* SSR 릴레이 — 환풍기 Active High */
#define PIN_IR_LED       26   /* IR LED — 에어컨 (NPN 증폭 회로, PWM/RMT) */
#define PIN_WINDOW_SERVO 27   /* SG90-HV Continuous PWM 신호선 */
#define PIN_AC_STATUS_LED 13  /* AC 상태 표시 LED (ON=에어컨 ON) */
#define MOTOR_RUN_MS     3000 /* 창문 완전 개폐 시간(ms) — 캘리브레이션 필요 */
#define SERVO_PWM_HZ     50   /* SG90 계열 표준 제어 주파수 */
#define SERVO_PULSE_STOP_US 1500 /* 정지(중립) */
#define SERVO_PULSE_OPEN_US 1700 /* 열림 방향(시계/반시계는 설치 방향에 따라 조정) */
#define SERVO_PULSE_CLOSE_US 1300 /* 닫힘 방향 */

/* ── Node C GPIO ───────────────────────────────────────────────────────────*/
#define PIN_BTN_NORMAL    32  /* 정상 버튼 — INPUT_PULLUP, FALLING 인터럽트 */
#define PIN_BTN_EMERGENCY 33  /* 응급 버튼 — INPUT_PULLUP, FALLING 인터럽트 */
#define DEBOUNCE_MS       200 /* 디바운스 간격(ms) */

/* ── ESP32-LoRa 내장 OLED (Node B/C) ───────────────────────────────────────
 * 일반적인 Heltec 계열 보드 기본값: SDA=4, SCL=15, RST=16, Addr=0x3C
 * 사용 보드가 다르면 아래 값을 보드 핀맵에 맞게 수정하세요.
 * ────────────────────────────────────────────────────────────────────────── */
#define USE_ONBOARD_OLED_B  1
#define USE_ONBOARD_OLED_C  1
#define OLED_I2C_SDA        4
#define OLED_I2C_SCL        15
#define OLED_RST_PIN        16
#define OLED_I2C_ADDR       0x3C
#define OLED_I2C_CLK_HZ     400000

/* ── 테스트: 버튼 없이 주기적 트리거 (1=활성, 0=비활성) ─────────────────────*/
#define TEST_AUTO_TRIGGER        1
#define TEST_TRIGGER_INTERVAL_MS 8000  /* 8초마다 정상↔응급 교대 전송 */

/* ── Node A 테스트: Wi-Fi 없이 ESP-NOW만 동작 (1=활성) ──────────────────────*/
#define NODE_A_TEST_SKIP_WIFI    1

/* ── 응급 자동 제어 기본값 ─────────────────────────────────────────────────*/
#define AUTO_FAN_STATE    FAN_ON
#define AUTO_AC_TEMP      18
#define AUTO_WINDOW_ACT   WINDOW_OPEN
