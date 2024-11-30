#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 1 (1 byte per pixel), 8 (RGB332), 16 (RGB565), 32 (ARGB8888) */
#define LV_COLOR_DEPTH 16

/* Maximal horizontal and vertical resolution to support by the library.*/
#define LV_HOR_RES_MAX          320
#define LV_VER_RES_MAX          240

/* Image decoder and cache */
#define LV_USE_IMG_DECODER      1
#define LV_IMG_CACHE_DEF_SIZE   1
#define LV_USE_GIF              1

/* Animation support */
#define LV_USE_ANIMATION        1

/* Memory manager settings */
#define LV_MEM_CUSTOM      0
#define LV_MEM_SIZE    (128U * 1024U)     // 增加内存大小，原来是 48K

/* File system settings */
#define LV_USE_FS_STDIO 0
#define LV_FS_STDIO_LETTER '\0'

/* Enable GPU */
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_NXP_PXP     0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL         0

/* Default display refresh period */
#define LV_DISP_DEF_REFR_PERIOD 30

#endif /* LV_CONF_H */ 