/**
 * Node B — Actuator Node (병실 제어기)
 * ESP32-LoRa | ESP-IDF v5.x
 *
 * 역할:
 *   - ESP-NOW 수신: Node A 제어 명령 파싱
 *   - GPIO 25: SSR 릴레이 — 환풍기 ON/OFF
 *   - GPIO 26: 왼쪽 커튼 SG90 Continuous 모터
 *   - GPIO 27: 오른쪽 커튼 SG90 Continuous 모터
 *   - GPIO 17: 커튼 리밋 스위치 (Active LOW)
 *   - GPIO  2: 내장 LED — 상태 표시
 *   - OLED: 상태 표시 (I2C SSD1306)
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
#include "driver/i2c.h"

#include "struct_message.h"
#include "config.h"
#include "oled_ssd1306.h"

static const char *TAG = "NODE_B";
static bool s_oled_ready = false;

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

/* ── OLED 표시 ─────────────────────────────────────────────────────────────*/

static void oled_render(const node_b_ui_state_t *st)
{
    if (!s_oled_ready || st == NULL) return;

    char line[24];
    uint32_t up_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    oled_ssd1306_clear();
    oled_ssd1306_draw_text(0, 0, "NODE B ACTUATOR");

    snprintf(line, sizeof(line), "FAN: %s",
             st->fan_state ? "ON" : "OFF");
    oled_ssd1306_draw_text(0, 1, line);

    snprintf(line, sizeof(line), "CURTAIN: %s", curtain_to_str(st->curtain_act));
    oled_ssd1306_draw_text(0, 2, line);

    snprintf(line, sizeof(line), "LIMIT: %s",
             gpio_get_level(PIN_CURTAIN_LIMIT) == 0 ? "HIT" : "---");
    oled_ssd1306_draw_text(0, 3, line);

    snprintf(line, sizeof(line), "CMD:%lu KA:%lu",
             (unsigned long)st->cmd_count, (unsigned long)st->keepalive_count);
    oled_ssd1306_draw_text(0, 4, line);

    snprintf(line, sizeof(line), "LINK:%s CH:%d",
             st->link_ok ? "OK" : "--", ESPNOW_CHANNEL_DEFAULT);
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

/* ── GPIO 초기화 ───────────────────────────────────────────────────────────*/

static void gpio_init_outputs(void)
{
    /* 출력 핀: 환풍기 릴레이 + 내장 LED */
    gpio_config_t out_cfg = {
        .pin_bit_mask = ((1ULL << PIN_FAN_RELAY) |
                         (1ULL << PIN_BUILTIN_LED)),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));
    gpio_set_level(PIN_FAN_RELAY, 0);
    gpio_set_level(PIN_BUILTIN_LED, 0);

    /* 입력 핀: 커튼 리밋 스위치 (Active LOW, 내부 풀업) */
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << PIN_CURTAIN_LIMIT),
        .mode         = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_cfg));

    ESP_LOGI(TAG, "GPIO 초기화 완료 (릴레이:%d LED:%d 리밋SW:%d)",
             PIN_FAN_RELAY, PIN_BUILTIN_LED, PIN_CURTAIN_LIMIT);
}

/* ── 내장 LED 상태 표시 ───────────────────────────────────────────────────*/

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

/* ── SG90 Continuous 듀얼 서보 (커튼 좌/우) ────────────────────────────────*/

#define SERVO_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER     LEDC_TIMER_0
#define SERVO_LEDC_CH_L      LEDC_CHANNEL_0   /* 왼쪽 모터 */
#define SERVO_LEDC_CH_R      LEDC_CHANNEL_1   /* 오른쪽 모터 */
#define SERVO_LEDC_RES       LEDC_TIMER_16_BIT

static void servo_set_pulse_us(ledc_channel_t ch, uint32_t pulse_us)
{
    const uint32_t period_us = 1000000UL / SERVO_PWM_HZ;
    const uint32_t max_duty  = (1UL << SERVO_LEDC_RES) - 1UL;
    const uint32_t duty      = (pulse_us * max_duty) / period_us;

    ESP_ERROR_CHECK(ledc_set_duty(SERVO_LEDC_MODE, ch, duty));
    ESP_ERROR_CHECK(ledc_update_duty(SERVO_LEDC_MODE, ch));
}

static void servo_both_stop(void)
{
    ESP_LOGI(TAG, "MOTOR L(GPIO%d)=STOP  R(GPIO%d)=STOP",
             PIN_CURTAIN_MOTOR_L, PIN_CURTAIN_MOTOR_R);
    servo_set_pulse_us(SERVO_LEDC_CH_L, SERVO_PULSE_STOP_US);
    servo_set_pulse_us(SERVO_LEDC_CH_R, SERVO_PULSE_STOP_US);
}

static void servo_both_open(void)
{
    ESP_LOGI(TAG, "MOTOR L(GPIO%d)=%luus  R(GPIO%d)=%luus  [OPEN]",
             PIN_CURTAIN_MOTOR_L, (unsigned long)SERVO_L_OPEN_US,
             PIN_CURTAIN_MOTOR_R, (unsigned long)SERVO_R_OPEN_US);
    servo_set_pulse_us(SERVO_LEDC_CH_L, SERVO_L_OPEN_US);
    servo_set_pulse_us(SERVO_LEDC_CH_R, SERVO_R_OPEN_US);
}

static void servo_both_close(void)
{
    ESP_LOGI(TAG, "MOTOR L(GPIO%d)=%luus  R(GPIO%d)=%luus  [CLOSE]",
             PIN_CURTAIN_MOTOR_L, (unsigned long)SERVO_L_CLOSE_US,
             PIN_CURTAIN_MOTOR_R, (unsigned long)SERVO_R_CLOSE_US);
    servo_set_pulse_us(SERVO_LEDC_CH_L, SERVO_L_CLOSE_US);
    servo_set_pulse_us(SERVO_LEDC_CH_R, SERVO_R_CLOSE_US);
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

    /* 왼쪽 모터 채널 */
    ledc_channel_config_t ch_l = {
        .gpio_num   = PIN_CURTAIN_MOTOR_L,
        .speed_mode = SERVO_LEDC_MODE,
        .channel    = SERVO_LEDC_CH_L,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = SERVO_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_l));

    /* 오른쪽 모터 채널 */
    ledc_channel_config_t ch_r = {
        .gpio_num   = PIN_CURTAIN_MOTOR_R,
        .speed_mode = SERVO_LEDC_MODE,
        .channel    = SERVO_LEDC_CH_R,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = SERVO_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_r));

    servo_both_stop();
    ESP_LOGI(TAG, "듀얼 SG90 Continuous 초기화 완료 (L:%d R:%d)",
             PIN_CURTAIN_MOTOR_L, PIN_CURTAIN_MOTOR_R);
}

/** 커튼 제어 — 좌/우 반대 방향 구동, 리밋 스위치 감지 시 조기 정지 */
static void control_curtain(uint8_t action)
{
    switch (action) {
    case WINDOW_OPEN:
        ESP_LOGI(TAG, "커튼 열기 시작");
        builtin_led_set(true);
        servo_both_open();
        for (int elapsed = 0; elapsed < MOTOR_RUN_MS; elapsed += 50) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(PIN_CURTAIN_LIMIT) == 0) {
                ESP_LOGI(TAG, "리밋 스위치 감지 — 커튼 열기 조기 정지");
                break;
            }
        }
        servo_both_stop();
        builtin_led_set(false);
        ESP_LOGI(TAG, "커튼 열기 완료");
        break;

    case WINDOW_CLOSE:
        ESP_LOGI(TAG, "커튼 닫기 시작");
        builtin_led_set(true);
        servo_both_close();
        for (int elapsed = 0; elapsed < MOTOR_RUN_MS; elapsed += 50) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(PIN_CURTAIN_LIMIT) == 0) {
                ESP_LOGI(TAG, "리밋 스위치 감지 — 커튼 닫기 조기 정지");
                break;
            }
        }
        servo_both_stop();
        builtin_led_set(false);
        ESP_LOGI(TAG, "커튼 닫기 완료");
        break;

    case WINDOW_STOP:
    default:
        servo_both_stop();
        builtin_led_set(false);
        ESP_LOGI(TAG, "커튼 정지");
        break;
    }
}

/* ── 액추에이터 제어 태스크 ─────────────────────────────────────────────────*/
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
            /* 환풍기 */
            gpio_set_level(PIN_FAN_RELAY, cmd.fan_state);
            ESP_LOGI(TAG, "환풍기: %s", cmd.fan_state ? "ON" : "OFF");

            /* 명령 수신 LED 깜빡임 */
            builtin_led_blink(2, 50, 50);

            /* 커튼 */
            control_curtain(cmd.curtain_act);

            if (s_ui_mutex && xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                s_ui.fan_state = cmd.fan_state;
                s_ui.ac_temp = cmd.ac_temp;
                s_ui.curtain_act = cmd.curtain_act;
                s_ui.cmd_count++;
                s_ui.last_cmd_us = esp_timer_get_time();
                s_ui.link_ok = true;
                xSemaphoreGive(s_ui_mutex);
            }

            /* 환풍기 ON이면 내장 LED 점등 유지 */
            builtin_led_set(cmd.fan_state == FAN_ON);
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

    ESP_LOGI(TAG, "명령 수신 — fan:%d ac:%d curtain:%d",
             msg.fan_state, msg.ac_temp, msg.window_act);

    actuator_cmd_t cmd = {
        .fan_state   = msg.fan_state,
        .ac_temp     = msg.ac_temp,
        .curtain_act = msg.window_act,
    };
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
    oled_init_if_enabled();

    /* 부팅 LED 깜빡임 */
    builtin_led_blink(3, 100, 100);

    /* ── 부팅 모터 개별 테스트 (각 1.5초씩) ──────────────────────────────── */
    ESP_LOGW(TAG, "=== 모터 테스트 시작 ===");

    ESP_LOGW(TAG, "[TEST] L(GPIO%d) CW  → %luus", PIN_CURTAIN_MOTOR_L, (unsigned long)SERVO_L_OPEN_US);
    servo_set_pulse_us(SERVO_LEDC_CH_L, SERVO_L_OPEN_US);
    servo_set_pulse_us(SERVO_LEDC_CH_R, SERVO_PULSE_STOP_US);
    vTaskDelay(pdMS_TO_TICKS(1500));
    servo_set_pulse_us(SERVO_LEDC_CH_L, SERVO_PULSE_STOP_US);
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGW(TAG, "[TEST] R(GPIO%d) CCW → %luus", PIN_CURTAIN_MOTOR_R, (unsigned long)SERVO_R_OPEN_US);
    servo_set_pulse_us(SERVO_LEDC_CH_L, SERVO_PULSE_STOP_US);
    servo_set_pulse_us(SERVO_LEDC_CH_R, SERVO_R_OPEN_US);
    vTaskDelay(pdMS_TO_TICKS(1500));
    servo_set_pulse_us(SERVO_LEDC_CH_R, SERVO_PULSE_STOP_US);
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGW(TAG, "[TEST] L(GPIO%d) CCW → %luus", PIN_CURTAIN_MOTOR_L, (unsigned long)SERVO_L_CLOSE_US);
    servo_set_pulse_us(SERVO_LEDC_CH_L, SERVO_L_CLOSE_US);
    servo_set_pulse_us(SERVO_LEDC_CH_R, SERVO_PULSE_STOP_US);
    vTaskDelay(pdMS_TO_TICKS(1500));
    servo_set_pulse_us(SERVO_LEDC_CH_L, SERVO_PULSE_STOP_US);
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGW(TAG, "[TEST] R(GPIO%d) CW  → %luus", PIN_CURTAIN_MOTOR_R, (unsigned long)SERVO_R_CLOSE_US);
    servo_set_pulse_us(SERVO_LEDC_CH_L, SERVO_PULSE_STOP_US);
    servo_set_pulse_us(SERVO_LEDC_CH_R, SERVO_R_CLOSE_US);
    vTaskDelay(pdMS_TO_TICKS(1500));
    servo_both_stop();

    ESP_LOGW(TAG, "=== 모터 테스트 완료 ===");

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

    /* 메인 루프: 2초마다 GPIO 상태 출력 (FAN|MOT_L|MOT_R|LIMIT|LED) */
    while (1) {
        int fan   = gpio_get_level(PIN_FAN_RELAY);
        int led   = gpio_get_level(PIN_BUILTIN_LED);
        int limit = gpio_get_level(PIN_CURTAIN_LIMIT);  /* 0=HIT, 1=OPEN */

        ESP_LOGI(TAG, "IO [FAN|LED|LIMIT] = [%d|%d|%d]  curtain=%s",
                 fan, led, limit,
                 curtain_to_str(s_ui.curtain_act));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
