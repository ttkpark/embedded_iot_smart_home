/**
 * Node B — Actuator Node (병실 제어기)
 * ESP32-LoRa | ESP-IDF v5.x
 *
 * 역할:
 *   - ESP-NOW 수신: Node A 제어 명령 파싱
 *   - GPIO 25: SSR 릴레이 — 환풍기 ON/OFF
 *   - GPIO 26: RMT(IR) — 에어컨 제어 (NPN 증폭 회로 연결)
 *   - GPIO 27/14: L293D 모터 드라이버 — 창문 리니어 액추에이터
 *
 * 사전 작업:
 *   - IR 리시버로 에어컨 리모컨 신호 캡처 후 ir_send_ac_on/off() 구현
 *   - MOTOR_RUN_MS 값을 실제 창문 이동 거리에 맞게 캘리브레이션
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"

#include "struct_message.h"
#include "config.h"

static const char *TAG = "NODE_B";

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

/* ── GPIO (릴레이 / 모터) ──────────────────────────────────────────────────*/

static void gpio_init_outputs(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = ((1ULL << PIN_FAN_RELAY)  |
                         (1ULL << PIN_MOTOR_IN1)   |
                         (1ULL << PIN_MOTOR_IN2)),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, /* 외부 10kΩ 풀다운 사용 */
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    /* 초기값: 모두 LOW (안전 상태) */
    gpio_set_level(PIN_FAN_RELAY,  0);
    gpio_set_level(PIN_MOTOR_IN1,  0);
    gpio_set_level(PIN_MOTOR_IN2,  0);

    ESP_LOGI(TAG, "GPIO 출력 초기화 완료");
}

/** 창문 리니어 액추에이터 제어 */
static void control_window(uint8_t action)
{
    switch (action) {
    case WINDOW_OPEN:
        ESP_LOGI(TAG, "창문 열기 시작");
        gpio_set_level(PIN_MOTOR_IN1, 1);
        gpio_set_level(PIN_MOTOR_IN2, 0);
        vTaskDelay(pdMS_TO_TICKS(MOTOR_RUN_MS));
        gpio_set_level(PIN_MOTOR_IN1, 0); /* 자동 정지 */
        ESP_LOGI(TAG, "창문 열기 완료");
        break;

    case WINDOW_CLOSE:
        ESP_LOGI(TAG, "창문 닫기 시작");
        gpio_set_level(PIN_MOTOR_IN1, 0);
        gpio_set_level(PIN_MOTOR_IN2, 1);
        vTaskDelay(pdMS_TO_TICKS(MOTOR_RUN_MS));
        gpio_set_level(PIN_MOTOR_IN2, 0);
        ESP_LOGI(TAG, "창문 닫기 완료");
        break;

    case WINDOW_STOP:
    default:
        gpio_set_level(PIN_MOTOR_IN1, 0);
        gpio_set_level(PIN_MOTOR_IN2, 0);
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
            } else {
                ir_send_ac_on(cmd.ac_temp);
            }

            /* 창문 */
            control_window(cmd.window_act);
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
    xQueueSendFromISR(s_act_queue, &cmd, NULL);
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

    gpio_init_outputs();
    ir_init();

    /* 액추에이터 명령 큐 & 태스크 */
    s_act_queue = xQueueCreate(4, sizeof(actuator_cmd_t));
    xTaskCreate(actuator_task, "actuator", 4096, NULL, 5, NULL);

    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(espnow_init());

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
