/**
 * DHT11 단선 GPIO 드라이버 (ESP-IDF)
 *
 * 프로토콜:
 *   1) MCU → LOW 18ms+ → 해제(풀업 HIGH) 20~40µs
 *   2) DHT → LOW 80µs → HIGH 80µs (응답)
 *   3) 40bit: 각 bit = LOW 50µs + HIGH (26µs=0, 70µs=1)
 *   4) 바이트: 습도정수, 습도소수, 온도정수, 온도소수, 체크섬
 *
 * 핵심: GPIO_MODE_INPUT_OUTPUT_OD (오픈 드레인) 사용
 *   - 방향 전환(OUTPUT→INPUT) 글리치 없음
 *   - 0 쓰기 = 능동 LOW, 1 쓰기 = 해제(풀업이 HIGH 유지)
 *   - 항상 읽기 가능 → 타이밍 안정
 */

#include "dht11.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

static const char *TAG = "DHT11";

#define DHT_TIMEOUT_US  1000  /* 개별 phase 타임아웃 (µs) */
#define DHT_RETRY_COUNT    3  /* 읽기 실패 시 재시도 */

static portMUX_TYPE s_dht_mux = portMUX_INITIALIZER_UNLOCKED;

/**
 * GPIO 레벨이 level인 동안 대기, 경과 µs 반환. 타임아웃 시 -1.
 */
static inline int wait_while(int gpio, int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(gpio) == level) {
        if ((esp_timer_get_time() - start) > timeout_us) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - start);
}

/**
 * 크리티컬 섹션 내부: DHT 응답 + 40bit 수신
 */
static esp_err_t dht11_read_bits(int gpio, uint8_t data[5])
{
    /*  라인 상태: HIGH (MCU가 1을 써서 해제, 풀업이 HIGH 유지)
     *  1) wait while HIGH → DHT가 LOW로 당길 때까지
     *  2) wait while LOW  → DHT 응답 LOW 80µs 끝
     *  3) wait while HIGH → DHT 응답 HIGH 80µs 끝
     */
    if (wait_while(gpio, 1, DHT_TIMEOUT_US) < 0) return ESP_ERR_TIMEOUT;
    if (wait_while(gpio, 0, DHT_TIMEOUT_US) < 0) return ESP_ERR_TIMEOUT;
    if (wait_while(gpio, 1, DHT_TIMEOUT_US) < 0) return ESP_ERR_TIMEOUT;

    /* 40bit 데이터 */
    for (int i = 0; i < 40; i++) {
        if (wait_while(gpio, 0, DHT_TIMEOUT_US) < 0) return ESP_ERR_TIMEOUT;
        int high_us = wait_while(gpio, 1, DHT_TIMEOUT_US);
        if (high_us < 0) return ESP_ERR_TIMEOUT;
        data[i / 8] <<= 1;
        if (high_us > 40) {
            data[i / 8] |= 1;
        }
    }
    return ESP_OK;
}

esp_err_t dht11_read(int gpio_num, dht11_data_t *out)
{
    esp_err_t ret = ESP_ERR_TIMEOUT;

    /* 최초 1회: 오픈 드레인 + 내부 풀업 설정 (이후 유지됨) */
    static bool s_gpio_configured = false;
    if (!s_gpio_configured) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << gpio_num),
            .mode         = GPIO_MODE_INPUT_OUTPUT_OD,  /* 오픈 드레인 */
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(gpio_num, 1);   /* 해제 상태 (풀업 HIGH) */
        ets_delay_us(1000);
        s_gpio_configured = true;
        ESP_LOGI(TAG, "GPIO%d 오픈드레인 설정 완료", gpio_num);
    }

    for (int attempt = 0; attempt < DHT_RETRY_COUNT; attempt++) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(200));  /* 재시도 간격 */
        }

        uint8_t data[5] = {0};

        /* ── 시작 신호 ─────────────────────────────────────────────
         * 오픈드레인이므로 0 쓰기=LOW 드라이브, 1 쓰기=해제(풀업 HIGH)
         * 방향 전환 없음 → 글리치 없음 */
        gpio_set_level(gpio_num, 1);     /* 해제 — 라인 HIGH 확인 */
        ets_delay_us(2000);              /* 2ms 안정화 */
        gpio_set_level(gpio_num, 0);     /* LOW 드라이브 — 시작 신호 */
        ets_delay_us(20000);             /* 20ms LOW (DHT11 최소 18ms) */
        gpio_set_level(gpio_num, 1);     /* 해제 — 풀업이 HIGH로 복귀 */
        ets_delay_us(30);                /* 30µs — DHT 응답 대기 */

        /* ── 인터럽트 차단 후 타이밍 크리티컬 구간 ─────────────── */
        portENTER_CRITICAL(&s_dht_mux);
        esp_err_t read_ret = dht11_read_bits(gpio_num, data);
        portEXIT_CRITICAL(&s_dht_mux);

        if (read_ret != ESP_OK) {
            ESP_LOGW(TAG, "타임아웃 (시도 %d/%d) — GPIO%d 레벨=%d",
                     attempt + 1, DHT_RETRY_COUNT,
                     gpio_num, gpio_get_level(gpio_num));
            continue;
        }

        /* 체크섬 */
        uint8_t sum = data[0] + data[1] + data[2] + data[3];
        if (sum != data[4]) {
            ESP_LOGW(TAG, "체크섬 불일치: [%u,%u,%u,%u,%u] 계산=%u (시도 %d/%d)",
                     data[0], data[1], data[2], data[3], data[4],
                     sum, attempt + 1, DHT_RETRY_COUNT);
            ret = ESP_ERR_INVALID_CRC;
            continue;
        }

        out->humidity        = data[0];
        out->humidity_dec    = data[1];
        out->temperature     = data[2];
        out->temperature_dec = data[3];
        return ESP_OK;
    }

    return ret;
}
