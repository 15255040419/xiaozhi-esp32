#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// 音频配置(与bread-compact-wifi相同)
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16
#endif

// 显示屏配置
#define DISPLAY_WIDTH   320          // 横屏宽度
#define DISPLAY_HEIGHT  240          // 横屏高度
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY  true       // 需要交换XY以支持横屏

// 显示屏引脚
#define DISPLAY_RS_PIN      GPIO_NUM_17   // DC
#define DISPLAY_CS_PIN      GPIO_NUM_39   // CS
#define DISPLAY_SCL_PIN     GPIO_NUM_40   // SCK
#define DISPLAY_SDA_PIN     GPIO_NUM_41   // MOSI
#define DISPLAY_RST_PIN     GPIO_NUM_38   // RST
#define DISPLAY_BLK_PIN     GPIO_NUM_42   // LEDK

// 显示屏特殊配置
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false  // 背光控制不需要反转（高电平点亮）
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

// TFT卡模块 (SPI接口)
#define TFT_MOSI_PIN       GPIO_NUM_47   // CMD
#define TFT_MISO_PIN       GPIO_NUM_48   // DAT0
#define TFT_SCK_PIN        GPIO_NUM_19   // CLK

// 按键配置
#define BOOT_BUTTON_GPIO    GPIO_NUM_0
#define RESET_BUTTON_GPIO   GPIO_NUM_3

// 电池管理
#define BAT_DETECT_PIN     GPIO_NUM_8

// 添加以下定义
#define BUILTIN_LED_GPIO        GPIO_NUM_48

// SD卡配置 (SPI模式)
#define SD_MOSI_PIN       TFT_MOSI_PIN
#define SD_MISO_PIN       TFT_MISO_PIN
#define SD_SCK_PIN        TFT_SCK_PIN
#define SD_MOUNT_POINT    "/sdcard"     // 挂载点
#define SD_DETECT_PIN     GPIO_NUM_NC   // 如果不使用检测引脚

// LCD配置
#define LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)
#define LCD_CMD_BITS        8
#define LCD_PARAM_BITS      8

#endif // _BOARD_CONFIG_H_ 