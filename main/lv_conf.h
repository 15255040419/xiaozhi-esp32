#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DPI_DEF 130                 // Dots per inch

/* Double Buffering */
#define LV_USE_GPU_ESP32_DRAW_BUF 1    // 使用ESP32 GPU加速
#define LV_BUF_ALLOC_PSRAM 1           // 从PSRAM分配缓冲区
#define LV_DISP_DEF_REFR_PERIOD 10     // 10ms刷新周期

/*====================
   FEATURE CONFIGURATION
 *====================*/

/* Image decoder and cache configuration */
#define LV_USE_IMG_DECODER 1
#define LV_IMG_CACHE_DEF_SIZE 5        // 增加图像缓存大小
#define LV_IMG_CACHE_DEF_SIZE_PSRAM 10 // PSRAM中的图像缓存大小

/* GIF decoder configuration */
#define LV_USE_GIF 1                   // 启用GIF支持
#define LV_GIF_CACHE_DECODING_DATA 1   // 缓存GIF解码数据以提高性能

/*====================
   Memory settings
 *====================*/
/* Memory manager settings */
#define LV_MEM_CUSTOM 0
#if LV_MEM_CUSTOM == 0
    #define LV_MEM_SIZE (512U * 1024U)          /* 增加内存池大小 */
    #define LV_MEM_ATTR                         /* 普通内存属性 */
    #define LV_MEM_ADR 0                        /* 0表示动态分配 */
    #define LV_MEM_AUTO_DEFRAG 1                /* 自动内存碎片整理 */
#else
    #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
    #define LV_MEM_CUSTOM_ALLOC   malloc
    #define LV_MEM_CUSTOM_FREE    free
    #define LV_MEM_CUSTOM_REALLOC realloc
#endif

/* PSRAM support */
#define LV_USE_PSRAM 1                          // 启用PSRAM支持
#define LV_PSRAM_CACHE_SIZE (4U * 1024U * 1024U) // 4MB PSRAM缓存

/* Enable DMA for image drawing */
#define LV_USE_GPU_DMA 1                        // 启用DMA传输
#define LV_GPU_DMA_TRANSFER_SIZE 64             // DMA传输大小

/* Enable hardware acceleration */
#define LV_USE_GPU 1                            // 启用GPU加速
#define LV_USE_GPU_ESP32 1                      // 使用ESP32的GPU功能

#endif 