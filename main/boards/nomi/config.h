#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// 音频配置(与bread-compact-wifi相同)
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4    // MSM261 WS
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5    // MSM261 SCK
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6    // MSM261 SD
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7    // MAX98357 DIN
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15   // MAX98357 BCLK
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16   // MAX98357 LRCLK
// MAX98357的SD_MODE#已经在硬件上接地，不需要软件控制
#endif

// 显示屏配置
#define DISPLAY_WIDTH   320          // 横屏宽度
#define DISPLAY_HEIGHT  240          // 横屏高度
#define DISPLAY_MIRROR_X true       // 左右镜像
#define DISPLAY_MIRROR_Y false         // 上下镜像
#define DISPLAY_SWAP_XY  true       // 交换XY，使用横屏

// LCD时序配置
#define LCD_PIXEL_CLOCK_HZ  (80 * 1000 * 1000)  // 80MHz
#define LCD_CMD_BITS        8
#define LCD_PARAM_BITS      8
#define LCD_SPI_MODE        0       // SPI模式0

// 显示屏引脚
#define DISPLAY_RS_PIN      GPIO_NUM_17   // DC
#define DISPLAY_CS_PIN      GPIO_NUM_39   // CS
#define DISPLAY_SCL_PIN     GPIO_NUM_40   // SCK
#define DISPLAY_SDA_PIN     GPIO_NUM_41   // MOSI
#define DISPLAY_RST_PIN     GPIO_NUM_38   // RST
#define DISPLAY_BLK_PIN     GPIO_NUM_42   // LEDK

// 显示屏特殊配置
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false   // 背光控制需要反转（高电平点亮）
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

// TF卡模块引脚定义
#define TF_MOSI_PIN       GPIO_NUM_47   // CMD (Pin 3)
#define TF_MISO_PIN       GPIO_NUM_48   // DAT0 (Pin 7)
#define TF_SCK_PIN        GPIO_NUM_19   // CLK (Pin 5)
#define TF_CS_PIN         GPIO_NUM_NC   // 不需要 CS 引脚
#define TF_DETECT_PIN     GPIO_NUM_NC   // 不使用检测引脚

// TF卡挂载点
#define TF_MOUNT_POINT    "/sdcard"

// 按键配置
#define BOOT_BUTTON_GPIO    GPIO_NUM_0
#define RESET_BUTTON_GPIO   GPIO_NUM_3

// 电池管理
#define BAT_DETECT_PIN     GPIO_NUM_8

// LED配置
#define BUILTIN_LED_GPIO        GPIO_NUM_18  // 改为使用 GPIO18

#endif // _BOARD_CONFIG_H_