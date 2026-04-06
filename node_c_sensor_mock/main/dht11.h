#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * DHT11 온습도센서 드라이버 (단선 GPIO 방식)
 * - 최소 읽기 간격: 2초
 * - 습도 범위: 20~90% RH, 온도 범위: 0~50°C
 */

typedef struct {
    uint8_t humidity;       /* 습도 정수부 (%) */
    uint8_t humidity_dec;   /* 습도 소수부     */
    uint8_t temperature;    /* 온도 정수부 (°C) */
    uint8_t temperature_dec;/* 온도 소수부     */
} dht11_data_t;

/**
 * DHT11 센서에서 데이터 읽기
 * @param gpio_num  연결된 GPIO 핀 번호
 * @param out       결과 저장 구조체
 * @return ESP_OK 성공, ESP_ERR_TIMEOUT 응답 없음, ESP_ERR_INVALID_CRC 체크섬 불일치
 */
esp_err_t dht11_read(int gpio_num, dht11_data_t *out);
