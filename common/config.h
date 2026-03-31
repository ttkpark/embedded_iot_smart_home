#pragma once

/* ── Wi-Fi 목록 ───────────────────────────────────────────────────────────
 * Node A가 연결을 시도할 공유기 SSID/PW 목록.
 * 부팅 시 Wi-Fi 스캔 → 목록에 있는 AP 중 신호가 가장 강한 것에 자동 연결.
 * ESPNOW_CHANNEL: 연결된 공유기 채널을 자동으로 따라감.
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct {
    const char *ssid;
    const char *password;
} wifi_ap_entry_t;

#define WIFI_AP_LIST { \
    { "Smart Meeting",    "12345678" }, \
    { "junespark-zone2",  "moon6412" }, \
}
#define WIFI_AP_COUNT  2

/* 스캔 실패 시 폴백으로 사용할 기본 채널 */
#define ESPNOW_CHANNEL_DEFAULT  1

/* ── MAC 주소 ──────────────────────────────────────────────────────────────
 * 최초 부팅 시 시리얼 모니터(115200 baud)에서 출력되는 값을 확인 후 기입.
 * 예: I (xxx) NODE_A: MAC: AA:BB:CC:DD:EE:FF
 * ────────────────────────────────────────────────────────────────────────── */
#define MAC_NODE_A  {0x30, 0xae, 0xa4, 0x84, 0x0d, 0x0c}   /* COM6  */
#define MAC_NODE_B  {0x24, 0x6f, 0x28, 0x22, 0x37, 0xd8}   /* COM24 */
#define MAC_NODE_C  {0x24, 0x6f, 0x28, 0x22, 0x39, 0x68}   /* COM9  */

/* ── Node A GPIO ───────────────────────────────────────────────────────────*/
#define PIN_STATUS_LED   2    /* On-board LED */
#define PIN_OLED_SDA     21   /* Optional I2C OLED */
#define PIN_OLED_SCL     22

/* ── Node B GPIO ───────────────────────────────────────────────────────────
 * 커튼 제어: SG90-HV Continuous 서보 2개 (좌/우 끈) + 리밋 스위치
 * GPIO 25: SSR 릴레이 (환풍기)
 * GPIO 23: 왼쪽 커튼 모터 (GPIO26=LoRa DIO, GPIO13=CCW 불가)
 * GPIO 27: 오른쪽 커튼 모터
 * GPIO 17: 커튼 리밋 스위치 (Active LOW, 내부 풀업 사용)
 * GPIO  2: 내장 LED — 상태 표시
 * ────────────────────────────────────────────────────────────────────────── */
#define PIN_FAN_RELAY        25  /* SSR 릴레이 — 환풍기 Active High */
#define PIN_CURTAIN_MOTOR_L  23  /* 왼쪽 커튼 SG90-HV Continuous PWM */
#define PIN_CURTAIN_MOTOR_R  27  /* 오른쪽 커튼 SG90 Continuous PWM */
#define PIN_CURTAIN_LIMIT    17  /* 커튼 리밋 스위치 (Active LOW) — 홀센서 폴백 */
#define PIN_BUILTIN_LED       2  /* 내장 LED — 상태 표시 */
#define MOTOR_RUN_MS       3000  /* 커튼 완전 개폐 시간(ms) — 홀센서 실패 시 폴백 타임아웃 */

/* ── 홀센서 (WSH135-XPAN2) — ADC 아날로그 회전 감지 & 동기 제어 ──────────
 * 모터 축에 자석 1개 → 1회전당 1펄스 (전압 하강 엣지 검출)
 * 비활성(자석 없음): ~1.5V,  활성(자석 감지): ~0.8V
 * GPIO 36(VP)=ADC1_CH0: 왼쪽 모터 (GPIO23 서보와 페어)
 * GPIO 39(VN)=ADC1_CH3: 오른쪽 모터 (GPIO27 서보와 페어)
 * ────────────────────────────────────────────────────────────────────────── */
#define PIN_HALL_L           36  /* 왼쪽 홀센서 — ADC1_CH0 (VP) */
#define PIN_HALL_R           39  /* 오른쪽 홀센서 — ADC1_CH3 (VN) */
#define HALL_ADC_CH_L        ADC_CHANNEL_0  /* GPIO36 = ADC1 채널 0 */
#define HALL_ADC_CH_R        ADC_CHANNEL_3  /* GPIO39 = ADC1 채널 3 */
/* 히스테리시스 기반 엣지 감지 (노이즈 면역)
 * idle ~1650 → 자석 감지 ~800. 단일 threshold 대신 enter/exit 분리.
 * ENTER: ADC < threshold_lo → 자석 진입
 * EXIT:  ADC > threshold_hi → 자석 이탈   */
#define HALL_THRESH_LO_L    1400  /* L: 이보다 낮으면 자석 진입 확정 */
#define HALL_THRESH_HI_L    1600  /* L: 이보다 높으면 자석 이탈 확정 */
#define HALL_THRESH_LO_R    1250  /* R: 자석 진입 */
#define HALL_THRESH_HI_R    1500  /* R: 자석 이탈 */
#define HALL_DEB_ENTER         4  /* 연속 N회 lo 이하 → 진입 확정 */
#define HALL_DEB_EXIT          4  /* 연속 N회 hi 이상 → 이탈 확정 */
#define HALL_EDGE_LOCKOUT_MS  80  /* 같은 센서에서 연속 엣지 최소 간격 (ms) */

#define HALL_SYNC_CHECK_MS    5   /* ADC 폴링 주기 (ms) */
#define HALL_REVS_FULL_DEFAULT 34 /* 기본 커튼 길이 = 3.4바퀴 (x10) */
#define SPEED_MATCH_GAIN    0.5f  /* PWM 보정 강도 (0~1) */
#define SPEED_MATCH_MS       50   /* 속도 보정 주기 (ms) */
#define RPM_TIMEOUT_US   2000000  /* 2초 무엣지 → RPM=0 */
#define NVS_NS_CURTAIN   "curtain" /* NVS 네임스페이스 */
#define BOOT_MOTOR_TEST      0   /* 부팅 시 모터 테스트 (0=비활성) */
#define SERVO_PWM_HZ          50   /* SG90 계열 표준 제어 주파수 */
#define SERVO_PULSE_STOP_US   1500 /* 정지(중립) */

/* 왼쪽 모터 (GPIO23) — 열림=CW, 닫힘=CCW */
#define SERVO_L_OPEN_US       1700
#define SERVO_L_CLOSE_US      1300

/* 오른쪽 모터 (GPIO27) — 감는 방향 반전 */
#define SERVO_R_OPEN_US       1300
#define SERVO_R_CLOSE_US      1700

/* ── Node C GPIO ───────────────────────────────────────────────────────────*/
#define PIN_BTN_NORMAL    32  /* 정상 버튼 — INPUT_PULLUP, FALLING 인터럽트 */
#define PIN_BTN_EMERGENCY 33  /* 응급 버튼 — INPUT_PULLUP, FALLING 인터럽트 */
#define DEBOUNCE_MS       200 /* 디바운스 간격(ms) */
#define EMERGENCY_COOLDOWN_MS 30000 /* 응급 자동 해제 쿨다운(ms) — 30초 */

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
#define TEST_AUTO_TRIGGER        0
#define TEST_TRIGGER_INTERVAL_MS 8000  /* 8초마다 정상↔응급 교대 전송 */

/* ── Node A 테스트: Wi-Fi 없이 ESP-NOW만 동작 (1=활성) ──────────────────────*/
#define NODE_A_TEST_SKIP_WIFI    0

/* ── Node A SoftAP 설정 ───────────────────────────────────────────────────
 * 공유기 연결 실패 / ESP-NOW 전용 모드에서도 Flutter 앱 연결 가능.
 * 폰에서 이 AP에 연결 → 192.168.4.1 로 WebSocket 접속.
 * ────────────────────────────────────────────────────────────────────────── */
#define SOFTAP_SSID         "SmartHome-ESP32"
#define SOFTAP_PASS         "12345678"
#define SOFTAP_MAX_CONN     4
#define SOFTAP_CHANNEL      1   /* ESP-NOW 채널과 동일하게 유지 */

/* ── 응급 자동 제어 기본값 ─────────────────────────────────────────────────*/
#define AUTO_FAN_STATE    FAN_ON
#define AUTO_AC_TEMP      18
#define AUTO_WINDOW_ACT   WINDOW_OPEN
