#include "boards/common/dual_network_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "iot/thing_manager.h"
#include "pcf85063a.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>


#define TAG "RanhLcdTouch20Board"
#define TAG_AXP "Axp2101"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class Axp2101 : public I2cDevice {
public:
    Axp2101(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
    }

    int GetBatteryCurrentDirection() {
        return (ReadReg(0x01) & 0b01100000) >> 5;
    }

    bool IsCharging() {
        return GetBatteryCurrentDirection() == 1;
    }

    bool IsDischarging() {
        return ((ReadReg(0x01) & 0b01100000) >> 5) == 0b10;
    }

    bool IsChargingDone() {
        return (ReadReg(0x01) & 0b00000111) == 0b00000100;
    }

    int GetBatteryLevel() {
        return ReadReg(0xA4);
    }

    float GetTemperature() {
        uint8_t raw_temp = ReadReg(0xA5);
        return (float)raw_temp * 0.1f - 14.47f;
    }

    void PowerOff() {
        ESP_LOGI(TAG_AXP, "AXP2101 Powering off");
        uint8_t value = ReadReg(0x10);
        value = value | 0x01;
        WriteReg(0x10, value);
    }

    void EnableBatteryAdc() {
        uint8_t value = ReadReg(0x30);
        value |= (1 << 7);
        value |= (1 << 6);
        WriteReg(0x30, value);
    }
};

class Pmic : public Axp2101 {
public:
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
        ESP_LOGI(TAG_AXP, "Initializing AXP2101 PMIC");

        uint8_t val_90 = ReadReg(0x90);
        val_90 |= (1 << 1);
        WriteReg(0x90, val_90);
        WriteReg(0x96, 0b00011100);

        val_90 = ReadReg(0x90);
        val_90 |= (1 << 0);
        WriteReg(0x90, val_90);
        WriteReg(0x92, 0b00011100);

        uint8_t val_91 = ReadReg(0x91);
        val_91 |= (1 << 0);
        WriteReg(0x91, val_91);
        WriteReg(0x94, 0b00011100);

        val_91 = ReadReg(0x91);
        val_91 |= (1 << 2);
        WriteReg(0x91, val_91);
        WriteReg(0x9A, 0b00001101);
        
        WriteReg(0x22, 0b00000111);

        WriteReg(0x27, (0b00 << 4) | 0b00);

        WriteReg(0x60, 0b00000011);
        WriteReg(0x64, 0x03);
        WriteReg(0x61, 0x05);
        WriteReg(0x62, 0x0A);
        WriteReg(0x63, 0x05);

        uint8_t adc_en1 = ReadReg(0x30);
        adc_en1 |= (1 << 7) | (1 << 6) | (1 << 5);
        WriteReg(0x30, adc_en1);

        uint8_t adc_en2 = ReadReg(0x31);
        adc_en2 |= (1 << 0);
        WriteReg(0x31, adc_en2);
        
        WriteReg(0x50, 0x14);

        WriteReg(0x14, 0x06);

        WriteReg(0x15, 0x00);

        WriteReg(0x16, 0x05);

        WriteReg(0x24, 0x01);
        WriteReg(0x24, 0x03);

        ESP_LOGI(TAG_AXP, "AXP2101 PMIC Initialized");
    }
};

class Pca9557 : public I2cDevice {
public:
    Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x03, 0xF0);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        if (bit > 7) return;
        uint8_t currentState = ReadReg(0x01);
        if (level) {
            currentState |= (1 << bit);
        } else {
            currentState &= ~(1 << bit);
        }
        WriteReg(0x01, currentState);
    }

    uint8_t ReadInputBitState(uint8_t bit) {
        if (bit > 7) return 0;
        uint8_t inputState = ReadReg(0x00);
        return (inputState >> bit) & 0x01;
    }
};

class RanhLcdTouch20Board : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_ = nullptr;
    Pca9557* pca9557_ = nullptr;
    Pmic* pmic_ = nullptr;
    Pcf85063a* rtc_ = nullptr;

    void InitializeBoardHardware() {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeTouch();
        UpdateAudioPath();
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {.enable_internal_pullup = 1,},
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
        
        pca9557_ = new Pca9557(i2c_bus_, PCA9557_I2C_ADDR);
        pmic_ = new Pmic(i2c_bus_, AXP2101_I2C_ADDR);

        rtc_ = new Pcf85063a(i2c_bus_);
        if (rtc_) {
            if (!rtc_->Initialize()) {
                ESP_LOGE(TAG, "Failed to initialize PCF85063A RTC. RTC functionality will be unavailable.");
                delete rtc_;
                rtc_ = nullptr;
            } else {
                ESP_LOGI(TAG, "PCF85063A RTC initialized successfully.");
                struct tm currentTime;
                if (rtc_->GetTime(&currentTime)) {
                   if (currentTime.tm_year + 1900 < 2023) {
                       ESP_LOGW(TAG, "RTC time seems invalid (year %d), setting to compile time.", currentTime.tm_year + 1900);
                       struct tm defaultTime;
                       char s_month[5];
                       int day, year_val;
                       sscanf(__DATE__, "%s %d %d", s_month, &day, &year_val);
                       sscanf(__TIME__, "%d:%d:%d", &defaultTime.tm_hour, &defaultTime.tm_min, &defaultTime.tm_sec);
                       defaultTime.tm_mday = day;
                       defaultTime.tm_year = year_val - 1900;
                       const char* month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
                       for (int i = 0; i < 12; i++) {
                           if (strcmp(s_month, month_names[i]) == 0) {
                               defaultTime.tm_mon = i;
                               break;
                           }
                       }
                       defaultTime.tm_wday = -1;
                       defaultTime.tm_isdst = -1;
                       rtc_->SetTime(&defaultTime);
                   }
                }
            }
        } else {
             ESP_LOGE(TAG, "Failed to allocate memory for PCF85063A RTC.");
        }
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_40;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_41;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (GetNetworkType() == NetworkType::WIFI) {
                if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                    // cast to WifiBoard
                    auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                    wifi_board.ResetWifiConfiguration();
                }
            }
            app.ToggleChatState();
        });
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting || app.GetDeviceState() == kDeviceStateWifiConfiguring) {
                SwitchNetworkType();
            }
        });
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_NC;
        io_config.dc_gpio_num = GPIO_NUM_39;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片ST7789
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        if (pca9557_) {
        } else {
            esp_lcd_panel_reset(panel);
        }
        if(pca9557_) pca9557_->SetOutputState(0, 0); 
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                      .emoji_font = font_emoji_32_init(),
#else
                                      .emoji_font = font_emoji_64_init(),
#endif
                                    });
        if(pca9557_) pca9557_->SetOutputState(0, 1);
    }

    void InitializeTouch() {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH, .y_max = DISPLAY_HEIGHT, .rst_gpio_num = GPIO_NUM_NC, .int_gpio_num = GPIO_NUM_NC,
            .levels = {.reset = 0, .interrupt = 0,},
            .flags = {.swap_xy = 1, .mirror_x = 1, .mirror_y = 0,},
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
        tp_io_config.scl_speed_hz = 400000;
        esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle);
        esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp);
        assert(tp);
        const lvgl_port_touch_cfg_t touch_cfg = { .disp = lv_display_get_default(), .handle = tp,};
        lvgl_port_add_touch(&touch_cfg);
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
        thing_manager.AddThing(iot::CreateThing("Battery"));
    }

    bool IsHeadphoneInserted() {
        if (pca9557_) {
            uint8_t sp_set_val = pca9557_->ReadInputBitState(4);
            return sp_set_val == 1;
        }
        ESP_LOGW(TAG, "PCA9557 未初始化，无法检测耳机。");
        return false;
    }

    void SetSpeakerPaEnable(bool enable) {
        if (pca9557_) {
            ESP_LOGI(TAG, "设置扬声器 PA (PCA9557 IO1) 为 %s", enable ? "ON" : "OFF");
            pca9557_->SetOutputState(1, enable ? 1 : 0);
        }
    }
    
    void UpdateAudioPath() {
        bool headphone_inserted = IsHeadphoneInserted();
        ESP_LOGI(TAG, "音频路径更新: 耳机插入: %s", headphone_inserted ? "是" : "否");
        auto codec = static_cast<BoxAudioCodec*>(GetAudioCodec());
        if (headphone_inserted) {
            SetSpeakerPaEnable(false);
            if (codec) { ESP_LOGI(TAG, "BoxAudioCodec: 尝试路由到耳机。"); }
            ESP_LOGI(TAG, "音频输出切换到耳机。");
        } else {
            SetSpeakerPaEnable(true);
            if (codec) { ESP_LOGI(TAG, "BoxAudioCodec: 尝试路由到扬声器。"); }
            ESP_LOGI(TAG, "音频输出切换到扬声器。");
        }
    }

public:
    RanhLcdTouch20Board() :
        DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, 4096),
        boot_button_(BOOT_BUTTON_GPIO)
    {
        InitializeBoardHardware();
        InitializeButtons();
        InitializeIot();
        GetBacklight()->RestoreBrightness();
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            GPIO_NUM_NC,
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging) override {
        if (!pmic_) return false;
        level = pmic_->GetBatteryLevel();
        charging = pmic_->IsCharging();

        if (pca9557_) {
            constexpr uint8_t BAT_LED_PIN = 3; 
            if (charging) {
                pca9557_->SetOutputState(BAT_LED_PIN, 1);
            } else {
                if (level < 20) {
                    pca9557_->SetOutputState(BAT_LED_PIN, 1); 
                } else {
                    pca9557_->SetOutputState(BAT_LED_PIN, 0); 
                }
            }
        }
        return true;
    }

    void TriggerPowerOff() {
        if (pmic_) {
            pmic_->PowerOff();
        }
    }

    Pcf85063a* GetRtc() const {
        return rtc_;
    }
};

DECLARE_BOARD(RanhLcdTouch20Board); 