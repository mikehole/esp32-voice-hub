/**
 * LVGL Configuration for ESP32 Voice Hub (ESP-IDF)
 * 360x360 round AMOLED display via QSPI
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 16 (RGB565) */
#define LV_COLOR_DEPTH 16

/* Swap bytes for SPI displays (big-endian) - CRITICAL for SH8601! */
#define LV_COLOR_16_SWAP 1

/* Display settings */
#define LV_HOR_RES_MAX 360
#define LV_VER_RES_MAX 360
#define LV_DPI_DEF 200

/* Memory - let ESP-IDF manage it */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64 * 1024)

/* Logging */
#define LV_USE_LOG 0

/* Drawing */
#define LV_DRAW_COMPLEX 1

/* Fonts - enable the ones we need */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0

#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Extra features */
#define LV_USE_SNAPSHOT   1
#define LV_USE_METER      1

/* Widgets */
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      1

/* Themes */
#define LV_USE_THEME_DEFAULT 1

#endif /* LV_CONF_H */
