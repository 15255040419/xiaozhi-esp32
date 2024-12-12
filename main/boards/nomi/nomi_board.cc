#include "../common/wifi_board.h"
#include "../common/system_reset.h"
#include "ap5056.h"
#include "audio_codecs/no_audio_codec.h"
#include "display/st7789_display.h"
#include "button.h"
#include "led.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "sd_card.h"
#include "application.h"
#include "wifi_station.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_vfs.h>
#include <esp_spiffs.h>
#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/spi_master.h"

#define TAG "NomiBoard"

class NomiBoard : public WifiBoard {
private:
    Button boot_button_;
    SystemReset system_reset_;
    Ap5056* ap5056_;
    esp_timer_handle_t power_save_timer_;
    SdCard sd_card_;

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
        });
        boot_button_.OnLongPress([this]() {
        });
    }

    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Lamp"));
    }

    void InitializePowerManagement() {
        ap5056_ = new Ap5056(BAT_DETECT_PIN);
        ap5056_->Initialize();
        
        // 创建电源管理定时器
        esp_timer_create_args_t power_save_timer_args = {
            .callback = [](void* arg) {
                static_cast<NomiBoard*>(arg)->PowerSaveCheck();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "power_save",
            .skip_unhandled_events = false,
        };
        ESP_ERROR_CHECK(esp_timer_create(&power_save_timer_args, &power_save_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(power_save_timer_, 1000000));  // 1秒检查一次
    }

    void PowerSaveCheck() {
        // 电池放电模式下，如果待机超过一定时间，则自动关机
        const int seconds_to_shutdown = 600;
        static int seconds = 0;
        if (Application::GetInstance().GetChatState() != kChatStateIdle) {
            seconds = 0;
            return;
        }
        if (!ap5056_->IsDischarging()) {
            seconds = 0;
            return;
        }
        
        seconds++;
        if (seconds >= seconds_to_shutdown) {
            ap5056_->PowerOff();
        }
    }

    void InitializeSpiAndSd() {
        spi_bus_config_t bus_cfg = {
            .mosi_io_num = TFT_MOSI_PIN,
            .miso_io_num = TFT_MISO_PIN,
            .sclk_io_num = TFT_SCK_PIN,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2,
        };
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
        
        // TFT和SD卡会共用这个SPI总线
        // 在初始化Display和SD卡时使用相同的SPI_HOST
    }

public:
    NomiBoard() : 
        boot_button_(BOOT_BUTTON_GPIO),
        system_reset_(RESET_BUTTON_GPIO, GPIO_NUM_NC) {
        
        InitializeSpiAndSd();  // 先初始化SPI总线
        InitializeButtons();
        InitializeIot();
        InitializePowerManagement();

        // 初始化SD卡
        sd_card_.Initialize();

        // 如果有卡检测引脚，配置它
        if (SD_DETECT_PIN != GPIO_NUM_NC) {
            gpio_config_t io_conf = {
                .pin_bit_mask = (1ULL << SD_DETECT_PIN),
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            gpio_config(&io_conf);
        }
    }

    ~NomiBoard() {
    }

    virtual Led* GetBuiltinLed() override {
        static Led led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodec audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, 
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodec audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        static St7789Display* display = nullptr;
        if (display == nullptr) {
            esp_lcd_panel_io_handle_t panel_io = nullptr;
            esp_lcd_panel_handle_t panel = nullptr;

            esp_lcd_panel_io_spi_config_t io_config = {
                .cs_gpio_num = DISPLAY_CS_PIN,
                .dc_gpio_num = DISPLAY_RS_PIN,
                .spi_mode = 0,
                .pclk_hz = LCD_PIXEL_CLOCK_HZ,
                .trans_queue_depth = 10,
                .on_color_trans_done = nullptr,
                .user_ctx = nullptr,
                .lcd_cmd_bits = LCD_CMD_BITS,
                .lcd_param_bits = LCD_PARAM_BITS,
                .flags = {
                    .dc_low_on_data = 0,
                    .octal_mode = 0,
                    .sio_mode = 0,
                    .lsb_first = 0,
                    .cs_high_active = 0
                }
            };

            ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &panel_io));

            esp_lcd_panel_dev_config_t panel_config = {
                .reset_gpio_num = DISPLAY_RST_PIN,
                .rgb_endian = LCD_RGB_ELEMENT_ORDER_RGB,
                .bits_per_pixel = 16,
                .flags = {
                    .reset_active_high = 0
                }
            };

            ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
            
            display = new St7789Display(panel_io, panel,
                DISPLAY_BLK_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT,
                DISPLAY_WIDTH, DISPLAY_HEIGHT,
                DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        }
        return display;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging) override {
        if (ap5056_ == nullptr) {
            return false;
        }
        level = ap5056_->GetBatteryLevel();
        charging = ap5056_->IsCharging();
        return true;
    }

    bool LoadGifFile(const char* filename, uint8_t** data, size_t* size) {
        return sd_card_.ReadFile(filename, data, size);
    }

    bool GetFileList(const char* dir_path, std::vector<std::string>& files) {
        return sd_card_.ListFiles(dir_path, files);
    }

    bool SaveFile(const char* filename, const uint8_t* data, size_t size) {
        return sd_card_.WriteFile(filename, data, size);
    }

    bool IsSdCardReady() const {
        return sd_card_.IsMounted();
    }
};

DECLARE_BOARD(NomiBoard); 