#include "../common/wifi_board.h"
#include "../common/system_reset.h"
#include "ap5056.h"
#include "audio_codecs/no_audio_codec.h"
#include "display/st7789_display.h"
#include "button.h"
#include "led.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "tf_card.h"
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
    TfCard tf_card_;
    static St7789Display* display_;

    void InitializeButtons() {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetChatState() == kChatStateUnknown && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
        });
        boot_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        boot_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
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

public:
    NomiBoard() : 
        boot_button_(BOOT_BUTTON_GPIO),
        system_reset_(RESET_BUTTON_GPIO, GPIO_NUM_NC) {
        
        InitializeButtons();
        InitializeIot();
        InitializePowerManagement();

        // 初始化TF卡
        tf_card_.Initialize();

        // 如果有卡检测引脚，配置它
        if (TF_DETECT_PIN != GPIO_NUM_NC) {
            gpio_config_t io_conf = {
                .pin_bit_mask = (1ULL << TF_DETECT_PIN),
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
        if (display_ == nullptr) {
            spi_bus_config_t buscfg = {
                .mosi_io_num = DISPLAY_SDA_PIN,
                .miso_io_num = GPIO_NUM_NC,
                .sclk_io_num = DISPLAY_SCL_PIN,
                .quadwp_io_num = GPIO_NUM_NC,
                .quadhd_io_num = GPIO_NUM_NC,
                .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t)
            };
            ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

            esp_lcd_panel_io_handle_t io_handle = nullptr;
            esp_lcd_panel_io_spi_config_t io_config = {
                .cs_gpio_num = DISPLAY_CS_PIN,
                .dc_gpio_num = DISPLAY_RS_PIN,
                .spi_mode = LCD_SPI_MODE,
                .pclk_hz = LCD_PIXEL_CLOCK_HZ,
                .trans_queue_depth = 10,
                .lcd_cmd_bits = LCD_CMD_BITS,
                .lcd_param_bits = LCD_PARAM_BITS,
            };
            ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io_handle));

            esp_lcd_panel_handle_t panel_handle = nullptr;
            esp_lcd_panel_dev_config_t panel_config = {
                .reset_gpio_num = DISPLAY_RST_PIN,
                .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                .bits_per_pixel = 16,
            };
            ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

            esp_lcd_panel_reset(panel_handle);
            esp_lcd_panel_init(panel_handle);
            esp_lcd_panel_invert_color(panel_handle, true);
            esp_lcd_panel_swap_xy(panel_handle, DISPLAY_SWAP_XY);
            esp_lcd_panel_mirror(panel_handle, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

            display_ = new St7789Display(io_handle, panel_handle, DISPLAY_BLK_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT,
                DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        }
        return display_;
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
        return tf_card_.ReadFile(filename, data, size);
    }

    bool GetFileList(const char* dir_path, std::vector<std::string>& files) {
        return tf_card_.ListFiles(dir_path, files);
    }

    bool SaveFile(const char* filename, const uint8_t* data, size_t size) {
        return tf_card_.WriteFile(filename, data, size);
    }

    bool IsTfCardReady() const {
        return tf_card_.IsMounted();
    }
};

St7789Display* NomiBoard::display_ = nullptr;

DECLARE_BOARD(NomiBoard); 