/**
 * Node B — Actuator Node (병실 제어기)
 * ESP32-LoRa | ESP-IDF v5.x
 *
 * 역할:
 *   - ESP-NOW 수신: Node A 제어 명령 파싱
 *   - GPIO 25: SSR 릴레이 — 환풍기 ON/OFF
 *   - GPIO 23: 왼쪽 커튼 SG90 Continuous 모터
 *   - GPIO 27: 오른쪽 커튼 SG90 Continuous 모터
 *   - GPIO 36: 왼쪽 홀센서 (WSH135-XPAN2, ADC1_CH0, 아날로그 엣지 검출)
 *   - GPIO 39: 오른쪽 홀센서 (WSH135-XPAN2, ADC1_CH3, 아날로그 엣지 검출)
 *   - GPIO 17: 커튼 리밋 스위치 (Active LOW, 폴백 안전장치)
 *   - GPIO  2: 내장 LED — 상태 표시
 *   - OLED: 상태 표시 (I2C SSD1306)
 *   - UART 콘솔: 캘리브레이션 모드 (cal/jog/pos 명령)
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

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
#include "motor_ctrl.h"

static const char *TAG = "NODE_B";
static bool s_oled_ready = false;

/* ── 콘솔 초기화 (cal_console.c) ──────────────────────────────────────────── */
extern void cal_console_init(void);

/* ── UI 상태 ──────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t fan_state;
    uint8_t ac_temp;
    uint8_t curtain_act;
    uint32_t cmd_count;
    uint32_t keepalive_count;
    int64_t last_cmd_us;
    bool link_ok;
} node_b_ui_state_t;

static node_b_ui_state_t s_ui = {
    .fan_state = FAN_OFF,
    .ac_temp = AC_OFF,
    .curtain_act = WINDOW_STOP,
};
static SemaphoreHandle_t s_ui_mutex = NULL;

static const char *curtain_to_str(uint8_t action)
{
    switch (action) {
    case WINDOW_OPEN:  return "OPEN";
    case WINDOW_CLOSE: return "CLOSE";
    case WINDOW_STOP:
    default:           return "STOP";
    }
}

/* ── OLED 표시 ─────────────────────────────────────────────────────────────── */

static void oled_render(const node_b_ui_state_t *st)
{
    if (!s_oled_ready || st == NULL) return;

    char line[24];
    uint32_t up_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    oled_ssd1306_clear();
    oled_ssd1306_draw_text(0, 0, g_cal_mode ? "NODE B [CAL]" : "NODE B ACTUATOR");

    snprintf(line, sizeof(line), "FAN: %s",
             st->fan_state ? "ON" : "OFF");
    oled_ssd1306_draw_text(0, 1, line);

    snprintf(line, sizeof(line), "CURTAIN: %s", curtain_to_str(st->curtain_act));
    oled_ssd1306_draw_text(0, 2, line);

    {
        const char *sstr[] = {"???", "CLOSED", "OPEN", "MOVING"};
        int si = (g_curtain_state <= 3) ? g_curtain_state : 0;
        snprintf(line, sizeof(line), "POS: %s", sstr[si]);
    }
    oled_ssd1306_draw_text(0, 3, line);

    snprintf(line, sizeof(line), "C:%d O:%d",
             hall_read_close(), hall_read_open());
    oled_ssd1306_draw_text(0, 4, line);

    snprintf(line, sizeof(line), "LINK:%s CMD:%lu",
             st->link_ok ? "OK" : "--", (unsigned long)st->cmd_count);
    oled_ssd1306_draw_text(0, 5, line);

    snprintf(line, sizeof(line), "UP:%lus", (unsigned long)up_s);
    oled_ssd1306_draw_text(0, 6, line);

    oled_ssd1306_refresh();
}

static void oled_render_from_state(void)
{
    if (s_ui_mutex == NULL) return;
    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        node_b_ui_state_t snap = s_ui;
        xSemaphoreGive(s_ui_mutex);
        oled_render(&snap);
    }
}

static void oled_init_if_enabled(void)
{
#if USE_ONBOARD_OLED_B
    oled_ssd1306_cfg_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_I2C_SDA,
        .scl_io_num = OLED_I2C_SCL,
        .rst_io_num = OLED_RST_PIN,
        .i2c_addr = OLED_I2C_ADDR,
        .i2c_clk_hz = OLED_I2C_CLK_HZ,
        .flip_vertical = false,
    };
    esp_err_t ret = oled_ssd1306_init(&cfg);
    if (ret == ESP_OK) {
        s_oled_ready = true;
        oled_ssd1306_clear();
        oled_ssd1306_draw_text(0, 1, "NODE B BOOTING...");
        oled_ssd1306_draw_text(0, 3, "WAIT ESPNOW CMD");
        oled_ssd1306_refresh();
    } else {
        ESP_LOGW(TAG, "OLED init 실패: %s", esp_err_to_name(ret));
    }
#endif
}

/* ── GPIO 초기화 (릴레이, LED, 리밋 스위치) ────────────────────────────────── */

static void gpio_init_outputs(void)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = ((1ULL << PIN_FAN_RELAY) | (1ULL << PIN_BUILTIN_LED)),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));
    gpio_set_level(PIN_FAN_RELAY, 0);
    gpio_set_level(PIN_BUILTIN_LED, 0);

    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << PIN_CURTAIN_LIMIT),
        .mode         = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_cfg));
}

/* ── 내장 LED ─────────────────────────────────────────────────────────────── */

static void builtin_led_set(bool on)
{
    gpio_set_level(PIN_BUILTIN_LED, on ? 1 : 0);
}

static void builtin_led_blink(int count, int on_ms, int off_ms)
{
    for (int i = 0; i < count; i++) {
        builtin_led_set(true);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        builtin_led_set(false);
        if (i < count - 1) vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

/* ── 액추에이터 제어 태스크 ────────────────────────────────────────────────── */

typedef struct {
    uint8_t fan_state;
    uint8_t ac_temp;
    uint8_t curtain_act;
} actuator_cmd_t;

static QueueHandle_t s_act_queue;

static void actuator_task(void *arg)
{
    actuator_cmd_t cmd;
    while (1) {
        if (xQueueReceive(s_act_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            gpio_set_level(PIN_FAN_RELAY, cmd.fan_state);
            ESP_LOGI(TAG, "환풍기: %s", cmd.fan_state ? "ON" : "OFF");
            builtin_led_blink(2, 50, 50);

            if (g_cal_mode) {
                ESP_LOGW(TAG, "캘리브레이션 모드 — 커튼 명령 무시");
            } else if (cmd.curtain_act >= MOTOR_JOG_L_CW) {
                /* 단일 모터 jog — CW/CCW */
                bool fwd = (cmd.curtain_act == MOTOR_JOG_L_CW ||
                            cmd.curtain_act == MOTOR_JOG_R_CW ||
                            cmd.curtain_act == MOTOR_JOG_BOTH_CW);
                motor_jog_timed(fwd, 1000);
            } else {
                control_curtain(cmd.curtain_act);
            }

            if (s_ui_mutex && xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                s_ui.fan_state = cmd.fan_state;
                s_ui.ac_temp = cmd.ac_temp;
                s_ui.curtain_act = cmd.curtain_act;
                s_ui.cmd_count++;
                s_ui.last_cmd_us = esp_timer_get_time();
                s_ui.link_ok = true;
                xSemaphoreGive(s_ui_mutex);
            }

            builtin_led_set(cmd.fan_state == FAN_ON);
            oled_render_from_state();
        }
    }
}

/* ── ESP-NOW ──────────────────────────────────────────────────────────────── */

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                           const uint8_t *data, int data_len)
{
    if (data_len != sizeof(struct_message_t)) return;
    struct_message_t msg;
    memcpy(&msg, data, sizeof(msg));
    if (msg.msg_type != MSG_COMMAND) return;

    ESP_LOGI(TAG, "명령 수신 — fan:%d ac:%d curtain:%d",
             msg.fan_state, msg.ac_temp, msg.window_act);

    actuator_cmd_t cmd = {
        .fan_state   = msg.fan_state,
        .ac_temp     = msg.ac_temp,
        .curtain_act = msg.window_act,
    };
    if (xQueueSend(s_act_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "명령 큐 가득 참");
    }
}

static esp_err_t espnow_init(void)
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    uint8_t mac_node_a[] = MAC_NODE_A;
    esp_now_peer_info_t peer = {
        .channel = ESPNOW_CHANNEL_DEFAULT,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, mac_node_a, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    return ESP_OK;
}

/* ── Wi-Fi ────────────────────────────────────────────────────────────────── */

static esp_err_t wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL_DEFAULT, WIFI_SECOND_CHAN_NONE));

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "MAC: " MACSTR, MAC2STR(mac));
    return ESP_OK;
}

/* ── Keep-alive 타이머 ─────────────────────────────────────────────────────── */

static void keepalive_timer_cb(void *arg)
{
    uint8_t mac_node_a[] = MAC_NODE_A;
    struct_message_t ka = { .msg_type = MSG_KEEPALIVE, .node_id = NODE_B };
    esp_now_send(mac_node_a, (uint8_t *)&ka, sizeof(ka));

    if (s_ui_mutex && xSemaphoreTake(s_ui_mutex, 0) == pdTRUE) {
        s_ui.keepalive_count++;
        xSemaphoreGive(s_ui_mutex);
    }
    oled_render_from_state();
}

/* ── app_main ──────────────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "Node B 부팅");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_ui_mutex = xSemaphoreCreateMutex();

    gpio_init_outputs();
    motor_ctrl_init();       /* ADC + LEDC + NVS 로드 */
    oled_init_if_enabled();

    builtin_led_blink(3, 100, 100);

#if BOOT_MOTOR_TEST
    ESP_LOGW(TAG, "=== 모터 테스트 시작 ===");
    servo_set_pulse(SERVO_OPEN_US);
    vTaskDelay(pdMS_TO_TICKS(1500));
    servo_stop();
    vTaskDelay(pdMS_TO_TICKS(500));
    servo_set_pulse(SERVO_CLOSE_US);
    vTaskDelay(pdMS_TO_TICKS(1500));
    servo_stop();
    ESP_LOGW(TAG, "=== 모터 테스트 완료 ===");
#endif

    s_act_queue = xQueueCreate(4, sizeof(actuator_cmd_t));
    xTaskCreate(actuator_task, "actuator", 8192, NULL, 5, NULL);

    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(espnow_init());

    /* UART 콘솔 (캘리브레이션 명령) */
    cal_console_init();

    /* Keep-alive 타이머 */
    esp_timer_handle_t ka_timer;
    esp_timer_create_args_t ka_args = {
        .callback = keepalive_timer_cb,
        .name     = "keepalive_b",
    };
    esp_timer_create(&ka_args, &ka_timer);
    esp_timer_start_periodic(ka_timer, 5000000);

    oled_render_from_state();
    ESP_LOGI(TAG, "초기화 완료 — 명령 대기 중 (콘솔: cal/jog/pos)");

    /* 메인 루프: 홀센서 ADC + 상태 출력 */
    while (1) {
        int hc = hall_read_close();
        int ho = hall_read_open();
        const char *ss[] = {"???","CLOSED","OPEN","MOVING"};
        int si = (g_curtain_state <= 3) ? g_curtain_state : 0;

        /* 감지 이벤트 로그: 연속 감지 시 10초마다, 새 감지 시 즉시 */
        static bool prev_close_det = false, prev_open_det = false;
        static int close_det_sec = 0, open_det_sec = 0;
        static int det_log_cnt = 0;

        bool close_det = (hc >= HALL_THRESH_CLOSE);
        bool open_det  = (ho >= HALL_THRESH_OPEN);

        if (close_det) {
            close_det_sec++;
            if (!prev_close_det) {
                ESP_LOGW(TAG, "*** CLOSE DETECTED *** C:%d", hc);
                close_det_sec = 0;
            } else if (close_det_sec >= 200) {  /* 50ms × 200 = 10초 */
                ESP_LOGW(TAG, "CLOSE held for %ds C:%d", close_det_sec / 20, hc);
                close_det_sec = 0;
            }
        } else {
            close_det_sec = 0;
        }

        if (open_det) {
            open_det_sec++;
            if (!prev_open_det) {
                ESP_LOGW(TAG, "*** OPEN DETECTED *** O:%d", ho);
                open_det_sec = 0;
            } else if (open_det_sec >= 200) {
                ESP_LOGW(TAG, "OPEN held for %ds O:%d", open_det_sec / 20, ho);
                open_det_sec = 0;
            }
        } else {
            open_det_sec = 0;
        }

        prev_close_det = close_det;
        prev_open_det  = open_det;

        /* 1초 주기 상태 로그 */
        static int log_cnt = 0;
        if (++log_cnt >= 20) {
            log_cnt = 0;
            ESP_LOGI(TAG, "C:%d O:%d [%s]",
                     hc, ho, ss[si]);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
