/**
 * Node B — Actuator Node (병실 제어기)
 * ESP32-LoRa | ESP-IDF v5.x
 *
 * 역할:
 *   - ESP-NOW 수신: Node A 제어 명령 파싱
 *   - GPIO 25: SSR 릴레이 — 환풍기 ON/OFF
 *   - GPIO 26: RMT(IR) — 에어컨 제어 (NPN 증폭 회로 연결)
 *   - GPIO 27: SG90-HV Continuous Servo — 창문 개폐 구동
 *   - GPIO 13: AC 상태 표시 LED (IR 제어와 동기화)
 *
 * 사전 작업:
 *   - IR 리시버로 에어컨 리모컨 신호 캡처 후 ir_send_ac_on/off() 구현
 *   - MOTOR_RUN_MS 및 SERVO_PULSE_*_US를 실제 구동 방향/속도에 맞게 캘리브레이션
 */

#include <string.h>
#include <stdio.h>

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
#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "driver/i2c.h"

#include "struct_message.h"
#include "config.h"
#include "oled_ssd1306.h"

static const char *TAG = "NODE_B";
static bool s_oled_ready = false;

typedef struct {
    uint8_t fan_state;
    uint8_t ac_temp;
    uint8_t window_act;
    uint32_t cmd_count;
    uint32_t keepalive_count;
    int64_t last_cmd_us;
    bool link_ok;
} node_b_ui_state_t;

static node_b_ui_state_t s_ui = {
    .fan_state = FAN_OFF,
    .ac_temp = AC_OFF,
    .window_act = WINDOW_STOP,
};
static SemaphoreHandle_t s_ui_mutex = NULL;

static const char *window_to_str(uint8_t action)
{
    switch (action) {
    case WINDOW_OPEN:  return "OPEN";
    case WINDOW_CLOSE: return "CLOSE";
    case WINDOW_STOP:
    default:           return "STOP";
    }
}

static uint8_t ac_temp_to_percent(uint8_t ac_temp)
{
    if (ac_temp == AC_OFF) return 0;
    if (ac_temp <= 18) return 0;
    if (ac_temp >= 30) return 100;
    return (uint8_t)(((ac_temp - 18) * 100) / 12);
}

static void oled_render(const node_b_ui_state_t *st)
{
    if (!s_oled_ready || st == NULL) return;

    char line[24];
    uint32_t up_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    oled_ssd1306_clear();
    oled_ssd1306_draw_text(0, 0, "NODE B ACTUATOR");

    snprintf(line, sizeof(line), "FAN:%s AC:%s",
             st->fan_state ? "ON " : "OFF",
             st->ac_temp == AC_OFF ? "OFF" : "ON ");
    oled_ssd1306_draw_text(0, 1, line);

    if (st->ac_temp == AC_OFF) {
        oled_ssd1306_draw_text(0, 2, "AC TEMP: OFF");
    } else {
        snprintf(line, sizeof(line), "AC TEMP: %uC", st->ac_temp);
        oled_ssd1306_draw_text(0, 2, line);
    }

    snprintf(line, sizeof(line), "WIN: %s", window_to_str(st->window_act));
    oled_ssd1306_draw_text(0, 3, line);

    snprintf(line, sizeof(line), "CMD:%lu KA:%lu",
             (unsigned long)st->cmd_count, (unsigned long)st->keepalive_count);
    oled_ssd1306_draw_text(0, 4, line);

    snprintf(line, sizeof(line), "LINK:%s CH:%d",
             st->link_ok ? "OK" : "--", ESPNOW_CHANNEL);
    oled_ssd1306_draw_text(0, 5, line);

    snprintf(line, sizeof(line), "UP:%lus", (unsigned long)up_s);
    oled_ssd1306_draw_text(0, 6, line);

    oled_ssd1306_draw_hbar(0, 56, 128, 8, ac_temp_to_percent(st->ac_temp));
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

/* ── RMT (IR LED) ──────────────────────────────────────────────────────────*/
static rmt_channel_handle_t s_ir_chan    = NULL;
static rmt_encoder_handle_t s_ir_copy_enc = NULL;

/**
 * RMT 초기화 — 1MHz 해상도(1tick = 1µs), copy encoder 사용
 * copy encoder는 rmt_symbol_word_t 배열을 그대로 전송.
 * AC 프로토콜에 맞는 심볼 배열을 ir_send_ac_on/off()에 채워 넣을 것.
 */
static void ir_init(void)
{
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num          = PIN_IR_LED,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 1000000, /* 1MHz → 1tick = 1µs */
        .mem_block_symbols = 128,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &s_ir_chan));

    rmt_copy_encoder_config_t copy_cfg = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_cfg, &s_ir_copy_enc));

    ESP_ERROR_CHECK(rmt_enable(s_ir_chan));
    ESP_LOGI(TAG, "RMT IR 초기화 완료 (GPIO %d)", PIN_IR_LED);
}

/**
 * NEC 38kHz 반송파 심볼 헬퍼
 *   duration0/1: µs 단위 (1tick = 1µs)
 *   level0/1: 반송파 ON=1 / OFF=0
 *
 * TODO: 실제 에어컨 브랜드 프로토콜에 맞는 심볼 배열로 교체.
 *       IR 리시버(TSOP38238 등) + ESP32 RMT 수신으로 신호를 먼저 캡처할 것.
 *       ESP-IDF 예제: examples/peripherals/rmt/ir_nec_transceiver
 */
static void ir_send_raw(const rmt_symbol_word_t *symbols, size_t count)
{
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    ESP_ERROR_CHECK(rmt_transmit(s_ir_chan, s_ir_copy_enc,
                                 symbols, count * sizeof(rmt_symbol_word_t),
                                 &tx_cfg));
    rmt_tx_wait_all_done(s_ir_chan, pdMS_TO_TICKS(200));
}

static void ir_send_ac_on(uint8_t temp_c)
{
    /*
     * TODO: 에어컨 브랜드별 IR 심볼 배열 삽입.
     *
     * 예시 (NEC 프레임 — 실제 에어컨 코드로 교체 필수):
     *   rmt_symbol_word_t symbols[] = {
     *       {.level0=1, .duration0=9000, .level1=0, .duration1=4500}, // Leader
     *       {.level0=1, .duration0=560,  .level1=0, .duration1=560 }, // 0-bit
     *       // ... 32-bit 데이터
     *       {.level0=1, .duration0=560,  .level1=0, .duration1=0   }, // End
     *   };
     *   ir_send_raw(symbols, sizeof(symbols)/sizeof(symbols[0]));
     */
    ESP_LOGI(TAG, "IR: 에어컨 ON — %d°C (TODO: 실제 코드 삽입)", temp_c);
}

static void ir_send_ac_off(void)
{
    /* TODO: 에어컨 OFF IR 심볼 배열 삽입 */
    ESP_LOGI(TAG, "IR: 에어컨 OFF (TODO: 실제 코드 삽입)");
}

/* ── GPIO (릴레이) ─────────────────────────────────────────────────────────*/

static void gpio_init_outputs(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = ((1ULL << PIN_FAN_RELAY) |
                         (1ULL << PIN_AC_STATUS_LED)),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, /* 외부 10kΩ 풀다운 사용 */
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    /* 초기값: LOW (안전 상태) */
    gpio_set_level(PIN_FAN_RELAY,  0);
    gpio_set_level(PIN_AC_STATUS_LED, 0);

    ESP_LOGI(TAG, "GPIO 출력 초기화 완료");
}

static void set_ac_status_led(bool ac_on)
{
    gpio_set_level(PIN_AC_STATUS_LED, ac_on ? 1 : 0);
    ESP_LOGI(TAG, "AC 상태 LED: %s", ac_on ? "ON" : "OFF");
}

/* ── SG90-HV Continuous Servo ──────────────────────────────────────────────*/

#define SERVO_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER     LEDC_TIMER_0
#define SERVO_LEDC_CHANNEL   LEDC_CHANNEL_0
#define SERVO_LEDC_RES       LEDC_TIMER_16_BIT

static void servo_set_pulse_us(uint32_t pulse_us)
{
    const uint32_t period_us = 1000000UL / SERVO_PWM_HZ;
    const uint32_t max_duty  = (1UL << SERVO_LEDC_RES) - 1UL;
    const uint32_t duty      = (pulse_us * max_duty) / period_us;

    ESP_ERROR_CHECK(ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL));
}

static void servo_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode       = SERVO_LEDC_MODE,
        .timer_num        = SERVO_LEDC_TIMER,
        .duty_resolution  = SERVO_LEDC_RES,
        .freq_hz          = SERVO_PWM_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = PIN_WINDOW_SERVO,
        .speed_mode = SERVO_LEDC_MODE,
        .channel    = SERVO_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = SERVO_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    servo_set_pulse_us(SERVO_PULSE_STOP_US);
    ESP_LOGI(TAG, "SG90-HV Continuous 초기화 완료 (GPIO %d)", PIN_WINDOW_SERVO);
}

/** 창문 SG90-HV Continuous 제어 */
static void control_window(uint8_t action)
{
    switch (action) {
    case WINDOW_OPEN:
        ESP_LOGI(TAG, "창문 열기 시작");
        servo_set_pulse_us(SERVO_PULSE_OPEN_US);
        vTaskDelay(pdMS_TO_TICKS(MOTOR_RUN_MS));
        servo_set_pulse_us(SERVO_PULSE_STOP_US); /* 자동 정지 */
        ESP_LOGI(TAG, "창문 열기 완료");
        break;

    case WINDOW_CLOSE:
        ESP_LOGI(TAG, "창문 닫기 시작");
        servo_set_pulse_us(SERVO_PULSE_CLOSE_US);
        vTaskDelay(pdMS_TO_TICKS(MOTOR_RUN_MS));
        servo_set_pulse_us(SERVO_PULSE_STOP_US);
        ESP_LOGI(TAG, "창문 닫기 완료");
        break;

    case WINDOW_STOP:
    default:
        servo_set_pulse_us(SERVO_PULSE_STOP_US);
        ESP_LOGI(TAG, "창문 정지");
        break;
    }
}

/* ── 액추에이터 제어 태스크 ─────────────────────────────────────────────────
 * 모터 delay()가 길기 때문에 별도 태스크에서 처리하여
 * ESP-NOW 수신 콜백이 블로킹되지 않도록 한다.
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct {
    uint8_t fan_state;
    uint8_t ac_temp;
    uint8_t window_act;
} actuator_cmd_t;

static QueueHandle_t s_act_queue;

static void actuator_task(void *arg)
{
    actuator_cmd_t cmd;
    while (1) {
        if (xQueueReceive(s_act_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            /* 환풍기 */
            gpio_set_level(PIN_FAN_RELAY, cmd.fan_state);
            ESP_LOGI(TAG, "환풍기: %s", cmd.fan_state ? "ON" : "OFF");

            /* 에어컨 */
            if (cmd.ac_temp == AC_OFF) {
                ir_send_ac_off();
                set_ac_status_led(false);
            } else {
                ir_send_ac_on(cmd.ac_temp);
                set_ac_status_led(true);
            }

            /* 창문 */
            control_window(cmd.window_act);

            if (s_ui_mutex && xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                s_ui.fan_state = cmd.fan_state;
                s_ui.ac_temp = cmd.ac_temp;
                s_ui.window_act = cmd.window_act;
                s_ui.cmd_count++;
                s_ui.last_cmd_us = esp_timer_get_time();
                s_ui.link_ok = true;
                xSemaphoreGive(s_ui_mutex);
            }
            oled_render_from_state();
        }
    }
}

/* ── ESP-NOW ────────────────────────────────────────────────────────────────*/

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                           const uint8_t *data, int data_len)
{
    if (data_len != sizeof(struct_message_t)) return;

    struct_message_t msg;
    memcpy(&msg, data, sizeof(msg));

    if (msg.msg_type != MSG_COMMAND) return;

    ESP_LOGI(TAG, "명령 수신 — fan:%d ac:%d window:%d",
             msg.fan_state, msg.ac_temp, msg.window_act);

    actuator_cmd_t cmd = {
        .fan_state  = msg.fan_state,
        .ac_temp    = msg.ac_temp,
        .window_act = msg.window_act,
    };
    /* 콜백에서 직접 처리하지 않고 큐에 넣어 태스크가 처리 */
    if (xQueueSend(s_act_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "명령 큐 가득 참, 최신 명령 유실");
    }
}

static esp_err_t espnow_init(void)
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    uint8_t mac_node_a[] = MAC_NODE_A;
    esp_now_peer_info_t peer = {
        .channel = ESPNOW_CHANNEL,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, mac_node_a, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "ESP-NOW 초기화 완료");
    return ESP_OK;
}

/* ── Wi-Fi (ESP-NOW 전용 채널 고정 — AP 연결 없음) ─────────────────────────*/

static esp_err_t wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Node A가 연결된 AP 채널과 동일하게 설정 */
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

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
    struct_message_t ka  = { .msg_type = MSG_KEEPALIVE, .node_id = NODE_B };
    esp_now_send(mac_node_a, (uint8_t *)&ka, sizeof(ka));

    if (s_ui_mutex && xSemaphoreTake(s_ui_mutex, 0) == pdTRUE) {
        s_ui.keepalive_count++;
        xSemaphoreGive(s_ui_mutex);
    }
    oled_render_from_state();
}

/* ── app_main ───────────────────────────────────────────────────────────────*/

void app_main(void)
{
    ESP_LOGI(TAG, "Node B 부팅");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES
        || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_ui_mutex = xSemaphoreCreateMutex();

    gpio_init_outputs();
    servo_init();
    ir_init();
    oled_init_if_enabled();

    /* 액추에이터 명령 큐 & 태스크 */
    s_act_queue = xQueueCreate(4, sizeof(actuator_cmd_t));
    xTaskCreate(actuator_task, "actuator", 4096, NULL, 5, NULL);

    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(espnow_init());
    oled_render_from_state();

    /* Keep-alive 타이머 (5초 주기) */
    esp_timer_handle_t ka_timer;
    esp_timer_create_args_t ka_args = {
        .callback = keepalive_timer_cb,
        .name     = "keepalive_b",
    };
    esp_timer_create(&ka_args, &ka_timer);
    esp_timer_start_periodic(ka_timer, 5000000);

    ESP_LOGI(TAG, "초기화 완료 — 명령 대기 중");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
