/**
 * Node C — Sensor Mock Node (가상 카메라/센서)
 * ESP32-LoRa | ESP-IDF v5.x
 *
 * 역할:
 *   - GPIO 32: 정상(Normal) 버튼 — INPUT_PULLUP, FALLING 인터럽트
 *   - GPIO 33: 응급(Emergency) 버튼 — INPUT_PULLUP, FALLING 인터럽트
 *   - 버튼 누름 → 디바운싱 → ESP-NOW 트리거 전송 → Node A
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

#include "struct_message.h"
#include "config.h"

static const char *TAG = "NODE_C";

/* ── 버튼 디바운싱 상태 (IRAM_ATTR ISR 에서 접근) ─────────────────────────*/
static volatile uint8_t  s_pending_status = 0xFF; /* 0xFF = 없음 */
static volatile uint64_t s_last_press_us  = 0;    /* 마지막 누름 시각(µs) */

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
static void IRAM_ATTR isr_normal_btn(void *arg)
{
    uint64_t now = esp_timer_get_time(); /* µs */
    if (now - s_last_press_us >= (uint64_t)DEBOUNCE_MS * 1000) {
        s_last_press_us  = now;
        s_pending_status = PATIENT_NORMAL;
    }
}

static void IRAM_ATTR isr_emergency_btn(void *arg)
{
    uint64_t now = esp_timer_get_time();
    if (now - s_last_press_us >= (uint64_t)DEBOUNCE_MS * 1000) {
        s_last_press_us  = now;
        s_pending_status = PATIENT_EMERGENCY;
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
        .channel = ESPNOW_CHANNEL,
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
    struct_message_t ka  = { .msg_type = MSG_KEEPALIVE, .node_id = NODE_C };
    esp_now_send(mac_node_a, (uint8_t *)&ka, sizeof(ka));
}

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
    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(espnow_init());

    /* Keep-alive 타이머 (5초 주기) */
    esp_timer_handle_t ka_timer;
    esp_timer_create_args_t ka_args = {
        .callback = keepalive_timer_cb,
        .name     = "keepalive_c",
    };
    esp_timer_create(&ka_args, &ka_timer);
    esp_timer_start_periodic(ka_timer, 5000000);

    ESP_LOGI(TAG, "초기화 완료 — 버튼 입력 대기 중");

    /* 메인 루프: ISR 플래그를 루프에서 안전하게 처리 */
    while (1) {
        if (s_pending_status != 0xFF) {
            uint8_t stat     = s_pending_status;
            s_pending_status = 0xFF; /* 초기화 먼저 (재진입 방지) */
            send_trigger(stat);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
