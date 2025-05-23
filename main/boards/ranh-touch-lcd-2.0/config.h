#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_INPUT_REFERENCE    true

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_38
#define AUDIO_I2S_GPIO_WS GPIO_NUM_13
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_14
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_12
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_45

#define AUDIO_CODEC_USE_PCA9557
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_1
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_2
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR  0x82

// AXP2101 Power Management IC (PMIC)
#define AXP2101_I2C_SDA_PIN   GPIO_NUM_1
#define AXP2101_I2C_SCL_PIN   GPIO_NUM_2
#define AXP2101_I2C_ADDR      0x34        // Common AXP2101 I2C Address

// RTC (Real-Time Clock)
// Assuming RTC also shares the same I2C bus
#define RTC_I2C_SDA_PIN     GPIO_NUM_1
#define RTC_I2C_SCL_PIN     GPIO_NUM_2
#define RTC_I2C_ADDR      0x68        // Common DS1307/DS3231 I2C Address, confirm from schematic/datasheet

#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

// ML307 4G Module
#define ML307_TX_PIN        GPIO_NUM_11 // ESP32S3's TX <-> ML307's RX
#define ML307_RX_PIN        GPIO_NUM_10 // ESP32S3's RX <-> ML307's TX
// Add other ML307 related pins if any (e.g., power key, status) based on schematic
#define ML307_PWR_KEY_PIN   GPIO_NUM_NC // Placeholder
#define ML307_STATUS_PIN    GPIO_NUM_NC // Placeholder

#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY true

#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_42
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT true


#endif // _BOARD_CONFIG_H_
