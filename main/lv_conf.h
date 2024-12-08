#pragma once

#ifndef LV_CONF_H
#define LV_CONF_H

/* Basic rendering settings */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Default colors */
#define LV_COLOR_BACKGROUND lv_color_hex(0x000000)  // Black background
#define LV_COLOR_TEXT lv_color_hex(0xFFFFFF)  // White text

/* Memory management */
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "esp_heap_caps.h"
#define LV_MEM_CUSTOM_ALLOC(size)       heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
#define LV_MEM_CUSTOM_FREE              heap_caps_free
#define LV_MEM_CUSTOM_REALLOC(p, size)  heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM)

/* Display Buffer Size */
#define LV_DISP_DEF_REFR_PERIOD 33    /* 30 FPS */
#define LV_DISP_DRAW_BUF_SIZE   (120 * 40)  /* Reduced buffer size */

/* Double Buffering */
#define LV_USE_GPU_ESP32_DMA2D 0  /* Disable DMA2D GPU */
#define LV_USE_DOUBLE_BUFFERING 1  /* Enable double buffering for smoother display */

/* Memory allocator settings */
#define LV_MEM_SIZE    (128 * 1024)  /* Use larger pool since we have PSRAM */
#define LV_MEM_POOL_ALLOC   1       /* Use pool allocator */

/* Drawing configurations */
#define LV_USE_DRAW_MASKS 1
#define LV_USE_DRAW_SW  1

/* Font usage */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Widget usage */
#define LV_USE_CANVAS 1

/* Logging */
#define LV_USE_LOG 0

/* Enable anti-aliasing */
#define LV_ANTIALIAS 1

/* Draw context */
#define LV_USE_DRAW_CTX 1

/* Reduce LVGL features to save memory */
#define LV_USE_ANIMATION 1   /* Keep animations enabled */
#define LV_USE_SHADOW 0      /* Disable shadows */
#define LV_USE_BLEND_MODES 0 /* Disable blend modes */
#define LV_USE_OPACITY 1     /* Keep opacity enabled for animations */
#define LV_USE_GROUP 1
#define LV_USE_GPU 0
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL 0
#define LV_USE_EXTERNAL_RENDERER 0

/* Reduce max number of objects */
#define LV_MAX_SCREENS 3
#define LV_MAX_GROUPS 4
#define LV_MAX_STYLES 8

#endif /*LV_CONF_H*/
