/**
 * Node C — Sensor Node
 * ESP32-LoRa | ESP-IDF v5.x
 *
 * 역할:
 *   - GPIO 32: 정상(Normal) 버튼
 *   - GPIO 33: 응급(Emergency) 버튼
 *   - GPIO 25: DHT11 온습도센서 — 습도 80% 이상 시 자동 응급 트리거
 *   - 버튼/센서 → 디바운싱 → ESP-NOW 트리거 전송 → Node A
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

#include "struct_message.h"
#include "config.h"
#include "oled_ssd1306.h"
#include "dht11.h"

static const char *TAG = "NODE_C";
static bool s_oled_ready = false;
static portMUX_TYPE s_ui_lock = portMUX_INITIALIZER_UNLOCKED;

static volatile uint32_t s_tx_ok = 0;
static volatile uint32_t s_tx_fail = 0;
static volatile uint32_t s_btn_normal_count = 0;
static volatile uint32_t s_btn_emergency_count = 0;
static volatile uint8_t  s_last_sent_stat = 0xFF;
static volatile esp_now_send_status_t s_last_send_status = ESP_NOW_SEND_FAIL;

/* ── DHT11 센서 상태 ──────────────────────────────────────────────────────*/
static volatile uint8_t  s_dht_humidity    = 0;   /* 마지막 읽은 습도 (%) */
static volatile uint8_t  s_dht_temperature = 0;   /* 마지막 읽은 온도 (°C) */
static volatile bool     s_dht_valid       = false; /* 최소 1회 읽기 성공 */
static volatile bool     s_humidity_emergency = false; /* 습도 기반 응급 활성 */

static const char *stat_to_str(uint8_t stat)
{
    if (stat == PATIENT_EMERGENCY) return "EMERGENCY";
    if (stat == PATIENT_NORMAL) return "NORMAL";
    return "--";
}

static void oled_render(void)
{
    if (!s_oled_ready) return;

    uint32_t tx_ok, tx_fail, n_cnt, e_cnt;
    uint8_t last_stat;
    esp_now_send_status_t last_status;
    taskENTER_CRITICAL(&s_ui_lock);
    tx_ok = s_tx_ok;
    tx_fail = s_tx_fail;
    n_cnt = s_btn_normal_count;
    e_cnt = s_btn_emergency_count;
    last_stat = s_last_sent_stat;
    last_status = s_last_send_status;
    taskEXIT_CRITICAL(&s_ui_lock);

    char line[24];
    uint32_t total = tx_ok + tx_fail;
    uint8_t ok_ratio = (total == 0) ? 0 : (uint8_t)((tx_ok * 100) / total);

    oled_ssd1306_clear();
    oled_ssd1306_draw_text(0, 0, "NODE C SENSOR");

    snprintf(line, sizeof(line), "LAST:%s", stat_to_str(last_stat));
    oled_ssd1306_draw_text(0, 1, line);

    snprintf(line, sizeof(line), "SEND:%s",
             last_status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
    oled_ssd1306_draw_text(0, 2, line);

    snprintf(line, sizeof(line), "BTN N:%lu E:%lu",
             (unsigned long)n_cnt, (unsigned long)e_cnt);
    oled_ssd1306_draw_text(0, 3, line);

    snprintf(line, sizeof(line), "TX OK:%lu", (unsigned long)tx_ok);
    oled_ssd1306_draw_text(0, 4, line);

    snprintf(line, sizeof(line), "TX NG:%lu CH:%d",
             (unsigned long)tx_fail, ESPNOW_CHANNEL_DEFAULT);
    oled_ssd1306_draw_text(0, 5, line);

    if (s_dht_valid) {
        snprintf(line, sizeof(line), "H:%u%% T:%uC %s",
                 s_dht_humidity, s_dht_temperature,
                 s_humidity_emergency ? "!EMG" : "");
        oled_ssd1306_draw_text(0, 6, line);
    } else {
#if TEST_AUTO_TRIGGER
        oled_ssd1306_draw_text(0, 6, "MODE: AUTO TEST");
#else
        oled_ssd1306_draw_text(0, 6, "MODE: BUTTON");
#endif
    }

    oled_ssd1306_draw_hbar(0, 56, 128, 8, ok_ratio);
    oled_ssd1306_refresh();
}

static void oled_init_if_enabled(void)
{
#if USE_ONBOARD_OLED_C
    oled_ssd1306_cfg_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_I2C_SDA,
        .scl_io_num = OLED_I2C_SCL,
        .rst_io_num = OLED_RST_PIN,
        .i2c_addr = OLED_I2C_ADDR,
        .i2c_clk_hz = OLED_I2C_CLK_HZ,
        .flip_vertical = true,
    };
    esp_err_t ret = oled_ssd1306_init(&cfg);
    if (ret == ESP_OK) {
        s_oled_ready = true;
        oled_ssd1306_clear();
        oled_ssd1306_draw_text(0, 1, "NODE C BOOTING...");
        oled_ssd1306_draw_text(0, 3, "WAIT BUTTON EVENT");
        oled_ssd1306_refresh();
    } else {
        ESP_LOGW(TAG, "OLED init 실패: %s", esp_err_to_name(ret));
    }
#endif
}

/* ── 버튼 디바운싱 상태 (IRAM_ATTR ISR 에서 접근) ─────────────────────────*/
static volatile uint8_t  s_pending_status = 0xFF; /* 0xFF = 없음 */
static volatile uint64_t s_last_press_us  = 0;    /* 마지막 누름 시각(µs) */
static volatile bool     s_emergency_active = false; /* 응급 상태 활성 여부 */
static volatile int64_t  s_emergency_start_us = 0;   /* 응급 시작 시각 */

/* ── ESP-NOW 전송 함수 ──────────────────────────────────────────────────────*/

static void send_trigger(uint8_t patient_stat)
{
    uint8_t mac_node_a[] = MAC_NODE_A;

    struct_message_t msg = {
        .msg_type    = MSG_TRIGGER,
        .node_id     = NODE_C,
        .patient_stat = patient_stat,
    };

    esp_err_t ret = esp_now_send(mac_node_a, (uint8_t *)&msg, sizeof(msg));
    taskENTER_CRITICAL(&s_ui_lock);
    s_last_sent_stat = patient_stat;
    taskEXIT_CRITICAL(&s_ui_lock);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "트리거 전송 — %s",
                 patient_stat == PATIENT_EMERGENCY ? "응급" : "정상");
    } else {
        ESP_LOGE(TAG, "esp_now_send 실패: %s", esp_err_to_name(ret));
    }
}

/* ── GPIO ISR (IRAM_ATTR — Flash 캐시 안전) ────────────────────────────────
 *
 * 디바운싱 방법:
 *   현재 시각(esp_timer_get_time)과 마지막 누름 시각의 차이가
 *   DEBOUNCE_MS 이상일 때만 pending_status를 갱신한다.
 *   실제 전송은 loop에서 ISR 바깥에서 처리.
 * ────────────────────────────────────────────────────────────────────────── */
/** 정상 버튼: 응급 상태를 즉시 수동 해제 */
static void IRAM_ATTR isr_normal_btn(void *arg)
{
    uint64_t now = esp_timer_get_time();
    if (now - s_last_press_us >= (uint64_t)DEBOUNCE_MS * 1000) {
        s_last_press_us  = now;
        s_pending_status = PATIENT_NORMAL;
        s_btn_normal_count++;
    }
}

/** 응급 버튼: EMERGENCY 발송 → 쿨다운 후 자동 NORMAL 복귀 */
static void IRAM_ATTR isr_emergency_btn(void *arg)
{
    uint64_t now = esp_timer_get_time();
    if (now - s_last_press_us >= (uint64_t)DEBOUNCE_MS * 1000) {
        s_last_press_us  = now;
        s_pending_status = PATIENT_EMERGENCY;
        s_btn_emergency_count++;
    }
}

/* ── GPIO 초기화 ────────────────────────────────────────────────────────────*/

static void gpio_init_buttons(void)
{
    gpio_config_t btn_cfg = {
        .pin_bit_mask = ((1ULL << PIN_BTN_NORMAL) |
                         (1ULL << PIN_BTN_EMERGENCY)),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   /* 내부 풀업 활성화 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,    /* FALLING: 누를 때(HIGH→LOW) */
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    /* ISR 서비스 및 핸들러 등록 */
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BTN_NORMAL,
                                         isr_normal_btn, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BTN_EMERGENCY,
                                         isr_emergency_btn, NULL));

    ESP_LOGI(TAG, "버튼 GPIO 초기화 완료 (정상:%d, 응급:%d)",
             PIN_BTN_NORMAL, PIN_BTN_EMERGENCY);
}

/* ── ESP-NOW ────────────────────────────────────────────────────────────────*/

static void espnow_send_cb(const uint8_t *mac_addr,
                           esp_now_send_status_t status)
{
    taskENTER_CRITICAL(&s_ui_lock);
    s_last_send_status = status;
    if (status == ESP_NOW_SEND_SUCCESS) s_tx_ok++;
    else s_tx_fail++;
    taskEXIT_CRITICAL(&s_ui_lock);

    ESP_LOGD(TAG, "전송 결과 " MACSTR ": %s",
             MAC2STR(mac_addr),
             status == ESP_NOW_SEND_SUCCESS ? "성공" : "실패");
}

static esp_err_t espnow_init(void)
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    uint8_t mac_node_a[] = MAC_NODE_A;
    esp_now_peer_info_t peer = {
        .channel = ESPNOW_CHANNEL_DEFAULT,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, mac_node_a, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "ESP-NOW 초기화 완료");
    return ESP_OK;
}

/* ── Wi-Fi (ESP-NOW 전용 채널 고정) ─────────────────────────────────────────*/

static esp_err_t wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL_DEFAULT, WIFI_SECOND_CHAN_NONE));

    /* 본인 MAC 출력 */
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "MAC: " MACSTR, MAC2STR(mac));

    return ESP_OK;
}

/* ── Keep-alive 타이머 ──────────────────────────────────────────────────────*/

static void keepalive_timer_cb(void *arg)
{
    uint8_t mac_node_a[] = MAC_NODE_A;
    struct_message_t ka  = { .msg_type = MSG_KEEPALIVE, .node_id = NODE_C };
    esp_now_send(mac_node_a, (uint8_t *)&ka, sizeof(ka));
}

#if TEST_AUTO_TRIGGER
/* ── 테스트: 주기적 자동 트리거 (버튼 없이 동작 확인용) ──────────────────────*/
static uint8_t s_test_toggle = 0;  /* 0=정상, 1=응급 교대 */

static void test_auto_trigger_cb(void *arg)
{
    uint8_t stat = s_test_toggle ? PATIENT_EMERGENCY : PATIENT_NORMAL;
    s_test_toggle = !s_test_toggle;
    send_trigger(stat);
    ESP_LOGI(TAG, "[TEST] auto trigger -> %s",
             stat == PATIENT_EMERGENCY ? "EMERGENCY" : "NORMAL");
}
#endif

/* ── app_main ───────────────────────────────────────────────────────────────*/

void app_main(void)
{
    ESP_LOGI(TAG, "Node C 부팅");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES
        || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_init_buttons();
    oled_init_if_enabled();
    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(espnow_init());
    oled_render();

    /* Keep-alive 타이머 (5초 주기) */
    esp_timer_handle_t ka_timer;
    esp_timer_create_args_t ka_args = {
        .callback = keepalive_timer_cb,
        .name     = "keepalive_c",
    };
    esp_timer_create(&ka_args, &ka_timer);
    esp_timer_start_periodic(ka_timer, 5000000);

#if TEST_AUTO_TRIGGER
    /* 테스트: 주기적 자동 트리거 (정상↔응급 8초마다 교대) */
    esp_timer_handle_t test_timer;
    esp_timer_create_args_t test_args = {
        .callback = test_auto_trigger_cb,
        .name     = "test_auto_trigger",
    };
    esp_timer_create(&test_args, &test_timer);
    esp_timer_start_periodic(test_timer, TEST_TRIGGER_INTERVAL_MS * 1000);
    ESP_LOGI(TAG, "Init OK [TEST MODE] auto trigger every %ds (NORMAL<->EMERGENCY)",
             TEST_TRIGGER_INTERVAL_MS / 1000);
#else
    ESP_LOGI(TAG, "초기화 완료 — 버튼 입력 대기 중");
#endif

    /* 메인 루프: ISR 플래그 처리 + DHT11 습도 감시 + 응급 쿨다운 자동 해제 */
    static int64_t s_last_gpio_log_us = 0;
    static int64_t s_last_dht_read_us = 0;
    while (1) {
        /* ── DHT11 주기적 읽기 ─────────────────────────────────────────── */
        int64_t now_dht = esp_timer_get_time();
        if (now_dht - s_last_dht_read_us >= (int64_t)DHT11_READ_INTERVAL_MS * 1000) {
            s_last_dht_read_us = now_dht;
            dht11_data_t dht;
            esp_err_t dht_ret = dht11_read(PIN_DHT11, &dht);
            if (dht_ret == ESP_OK) {
                s_dht_humidity    = dht.humidity;
                s_dht_temperature = dht.temperature;
                s_dht_valid       = true;
                oled_render();

                /* 습도 임계값 초과 → 응급 트리거 (이미 응급 상태가 아닐 때만) */
                if (dht.humidity >= HUMIDITY_EMERGENCY_THRESH
                    && !s_emergency_active && !s_humidity_emergency) {
                    s_humidity_emergency = true;
                    s_pending_status     = PATIENT_EMERGENCY;
                    ESP_LOGW(TAG, "습도 %u%% >= %d%% — 응급 트리거!",
                             dht.humidity, HUMIDITY_EMERGENCY_THRESH);
                }
                /* 습도 정상 복귀 → 자동 해제 */
                if (dht.humidity < HUMIDITY_EMERGENCY_THRESH && s_humidity_emergency) {
                    s_humidity_emergency = false;
                    ESP_LOGI(TAG, "습도 %u%% 정상 복귀", dht.humidity);
                }
            } else {
                ESP_LOGW(TAG, "DHT11 읽기 실패: %s", esp_err_to_name(dht_ret));
            }
        }

        if (s_pending_status != 0xFF) {
            uint8_t stat     = s_pending_status;
            s_pending_status = 0xFF;
            send_trigger(stat);

            if (stat == PATIENT_EMERGENCY) {
                s_emergency_active   = true;
                s_emergency_start_us = esp_timer_get_time();
                ESP_LOGW(TAG, "응급 활성화 — %d초 후 자동 해제",
                         EMERGENCY_COOLDOWN_MS / 1000);
            } else {
                if (s_emergency_active) {
                    ESP_LOGI(TAG, "응급 수동 해제 (정상 버튼)");
                }
                s_emergency_active = false;
            }
            oled_render();
        }

        /* 응급 쿨다운 자동 해제 */
        if (s_emergency_active) {
            int64_t elapsed_us = esp_timer_get_time() - s_emergency_start_us;
            if (elapsed_us >= (int64_t)EMERGENCY_COOLDOWN_MS * 1000) {
                s_emergency_active = false;
                send_trigger(PATIENT_NORMAL);
                ESP_LOGI(TAG, "응급 자동 해제 (쿨다운 %d초 경과)",
                         EMERGENCY_COOLDOWN_MS / 1000);
                oled_render();
            }
        }

        int64_t now = esp_timer_get_time();

        /* 10초마다 상태 로그 출력 */
        if (now - s_last_gpio_log_us >= 10000000) {
            s_last_gpio_log_us = now;
            int btn_n = gpio_get_level(PIN_BTN_NORMAL);
            int btn_e = gpio_get_level(PIN_BTN_EMERGENCY);
            ESP_LOGI(TAG, "IO [BTN_N|BTN_E]=[%d|%d] emg=%s last=%s tx_ok=%lu tx_ng=%lu",
                     btn_n, btn_e,
                     s_emergency_active ? "ACTIVE" : "idle",
                     stat_to_str(s_last_sent_stat),
                     (unsigned long)s_tx_ok, (unsigned long)s_tx_fail);
            if (s_dht_valid) {
                ESP_LOGI(TAG, "DHT11: H=%u%% T=%u°C  hum_emg=%s",
                         s_dht_humidity, s_dht_temperature,
                         s_humidity_emergency ? "YES" : "no");
            } else {
                ESP_LOGW(TAG, "DHT11: 데이터 없음");
            }
            oled_render();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
