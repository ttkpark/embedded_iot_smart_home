#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int i2c_port;           /* I2C_NUM_0 or I2C_NUM_1 */
    int sda_io_num;         /* I2C SDA pin */
    int scl_io_num;         /* I2C SCL pin */
    int rst_io_num;         /* OLED reset pin, -1 to skip */
    uint8_t i2c_addr;       /* Usually 0x3C */
    uint32_t i2c_clk_hz;    /* Usually 400000 */
    bool flip_vertical;     /* true for upside-down mount */
} oled_ssd1306_cfg_t;

esp_err_t oled_ssd1306_init(const oled_ssd1306_cfg_t *cfg);
void oled_ssd1306_clear(void);
void oled_ssd1306_draw_text(int col, int row, const char *text);
void oled_ssd1306_draw_hbar(int x, int y, int width, int height, uint8_t percent);
void oled_ssd1306_refresh(void);

#ifdef __cplusplus
}
#endif
