#include "ml307_board.h"
#include "audio_codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "power_save_timer.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "../xingzhi-cube-1.54tft-wifi/power_manager.h"
#include "font_awesome_symbols.h"
#include "system_info.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>

#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>

#define TAG "RAN_LCD_4G"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);


class RAN_LCD_4G : public Ml307Board {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    SpiLcdDisplay* display_;
    PowerSaveTimer* power_save_timer_;
    PowerManager* power_manager_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    bool use_4g_ = true; // 默认使用4G网络
    bool wifi_config_mode_ = false;

    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_38);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            }
        });
    }

    void InitializePowerSaveTimer() {
        rtc_gpio_init(GPIO_NUM_21);
        rtc_gpio_set_direction(GPIO_NUM_21, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(GPIO_NUM_21, 1);

        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling sleep mode");
            display_->SetChatMessage("system", "");
            display_->SetEmotion("sleepy");
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            display_->SetChatMessage("system", "");
            display_->SetEmotion("neutral");
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            rtc_gpio_set_level(GPIO_NUM_21, 0);
            // 启用保持功能，确保睡眠期间电平不变
            rtc_gpio_hold_en(GPIO_NUM_21);
            esp_lcd_panel_disp_on_off(panel_, false); //关闭显示
            esp_deep_sleep_start();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SDA;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SCL;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    // 从NVS中加载网络模式设置
    void LoadNetworkMode() {
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("network", NVS_READONLY, &nvs_handle);
        if (err == ESP_OK) {
            uint8_t mode = 1; // 默认使用4G
            err = nvs_get_u8(nvs_handle, "mode", &mode);
            if (err == ESP_OK) {
                use_4g_ = (mode == 1);
                ESP_LOGI(TAG, "加载网络模式: %s", use_4g_ ? "4G" : "Wi-Fi");
            }
            nvs_close(nvs_handle);
        }
    }

    // 保存网络模式设置到NVS
    void SaveNetworkMode() {
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("network", NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            err = nvs_set_u8(nvs_handle, "mode", use_4g_ ? 1 : 0);
            if (err == ESP_OK) {
                nvs_commit(nvs_handle);
                ESP_LOGI(TAG, "保存网络模式: %s", use_4g_ ? "4G" : "Wi-Fi");
            }
            nvs_close(nvs_handle);
        }
    }

    // 切换网络模式
    void ToggleNetworkMode() {
        power_save_timer_->WakeUp();
        use_4g_ = !use_4g_;
        SaveNetworkMode();
        
        // 显示切换提示
        std::string network_mode = use_4g_ ? "4G模式" : "Wi-Fi模式";
        std::string message = "正在切换到" + network_mode + "\n设备将重启...";
        display_->SetChatMessage("system", message.c_str());
        display_->SetEmotion("thinking");
        
        // 延迟2秒后重启
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

    // 进入Wi-Fi配置模式
    void EnterWifiConfigMode() {
        auto& application = Application::GetInstance();
        application.SetDeviceState(kDeviceStateWifiConfiguring);

        auto& wifi_ap = WifiConfigurationAp::GetInstance();
        wifi_ap.SetLanguage(Lang::CODE);
        wifi_ap.SetSsidPrefix("Xiaozhi");
        wifi_ap.Start();

        // 显示 WiFi 配置 AP 的 SSID 和 Web 服务器 URL
        std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
        hint += wifi_ap.GetSsid();
        hint += Lang::Strings::ACCESS_VIA_BROWSER;
        hint += wifi_ap.GetWebServerUrl();
        hint += "\n\n";
        
        // 播报配置 WiFi 的提示
        application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);
        
        // Wait forever until reset after configuration
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }

    // 启动Wi-Fi连接
    void StartWifiNetwork() {
        // 检查是否需要进入Wi-Fi配置模式
        if (wifi_config_mode_) {
            EnterWifiConfigMode();
            return;
        }

        // 检查是否有保存的Wi-Fi配置
        auto& ssid_manager = SsidManager::GetInstance();
        auto ssid_list = ssid_manager.GetSsidList();
        if (ssid_list.empty()) {
            wifi_config_mode_ = true;
            EnterWifiConfigMode();
            return;
        }

        // 连接Wi-Fi
        auto& wifi_station = WifiStation::GetInstance();
        wifi_station.OnScanBegin([this]() {
            display_->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
        });
        wifi_station.OnConnect([this](const std::string& ssid) {
            std::string notification = Lang::Strings::CONNECT_TO;
            notification += ssid;
            notification += "...";
            display_->ShowNotification(notification.c_str(), 30000);
        });
        wifi_station.OnConnected([this](const std::string& ssid) {
            std::string notification = Lang::Strings::CONNECTED_TO;
            notification += ssid;
            display_->ShowNotification(notification.c_str(), 30000);
            display_->SetStatus("Wi-Fi");
        });
        wifi_station.Start();

        // 等待Wi-Fi连接成功，如果失败则进入配置模式
        if (!wifi_station.WaitForConnected(60 * 1000)) {
            wifi_station.Stop();
            wifi_config_mode_ = true;
            EnterWifiConfigMode();
            return;
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto& app = Application::GetInstance();
            app.ToggleChatState();
        });

        // 添加按下和松开事件，用于语音唤醒
        boot_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        
        boot_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });

        // 添加长按切换网络模式
        boot_button_.OnLongPress([this]() {
            ToggleNetworkMode();
        });

        volume_up_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            if (display_) {
                ESP_LOGI(TAG, "Switching to next wallpaper");
                display_->ChangeWallpaper("next");
                GetDisplay()->ShowNotification("下一张壁纸");
            }
        });

        volume_down_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            if (display_) {
                ESP_LOGI(TAG, "Switching to previous wallpaper");
                display_->ChangeWallpaper("previous");
                GetDisplay()->ShowNotification("上一张壁纸");
            }
        });
    }

    void InitializeSt7789Display() {
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS;
        io_config.dc_gpio_num = DISPLAY_DC;
        io_config.spi_mode = 3;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RES;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));

        display_ = new SpiLcdDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY, 
        {
            .text_font = &font_puhui_20_4,
            .icon_font = &font_awesome_20_4,
            .emoji_font = font_emoji_64_init(),
        });
    }

    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
        thing_manager.AddThing(iot::CreateThing("Battery"));
    }

public:
    RAN_LCD_4G() :
        Ml307Board(ML307_TX_PIN, ML307_RX_PIN, 4096),
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        // 加载网络模式设置
        LoadNetworkMode();
        
        InitializePowerManager();
        InitializePowerSaveTimer();
        InitializeSpi();
        InitializeButtons();
        InitializeSt7789Display();  
        InitializeIot();
        GetBacklight()->RestoreBrightness();
        
        // 显示当前网络模式
        std::string network_mode = use_4g_ ? "4G模式" : "Wi-Fi模式";
        std::string message = "当前网络: " + network_mode;
        display_->SetChatMessage("system", message.c_str());
        vTaskDelay(pdMS_TO_TICKS(2000));
        display_->SetChatMessage("system", "");
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveMode(bool enabled) override {
        if (!enabled) {
            power_save_timer_->WakeUp();
        }
        Ml307Board::SetPowerSaveMode(enabled);
    }
    
    // 重写网络连接方法，根据当前模式决定使用4G还是Wi-Fi
    virtual void StartNetwork() override {
        if (use_4g_) {
            // 使用4G连接
            ESP_LOGI(TAG, "使用4G连接");
            display_->SetStatus("4G");
            // 调用Ml307Board的StartNetwork方法
            Ml307Board::StartNetwork();
        } else {
            // 使用Wi-Fi连接
            ESP_LOGI(TAG, "使用Wi-Fi连接");
            display_->SetStatus("Wi-Fi");
            // 启动Wi-Fi连接
            StartWifiNetwork();
        }
    }
    
    // 重写获取网络状态图标的方法
    virtual const char* GetNetworkStateIcon() override {
        if (use_4g_) {
            // 使用4G模式，调用Ml307Board的方法
            return Ml307Board::GetNetworkStateIcon();
        } else {
            // 使用Wi-Fi模式，返回Wi-Fi图标
            if (wifi_config_mode_) {
                return FONT_AWESOME_WIFI;
            }
            auto& wifi_station = WifiStation::GetInstance();
            if (!wifi_station.IsConnected()) {
                return FONT_AWESOME_WIFI_OFF;
            }
            int8_t rssi = wifi_station.GetRssi();
            if (rssi >= -60) {
                return FONT_AWESOME_WIFI;
            } else if (rssi >= -70) {
                return FONT_AWESOME_WIFI_FAIR;
            } else {
                return FONT_AWESOME_WIFI_WEAK;
            }
        }
    }
    
    // 重写获取板子JSON信息的方法
    virtual std::string GetBoardJson() override {
        if (use_4g_) {
            // 使用4G模式，调用Ml307Board的方法
            return Ml307Board::GetBoardJson();
        } else {
            // 使用Wi-Fi模式，返回Wi-Fi信息
            auto& wifi_station = WifiStation::GetInstance();
            std::string board_json = std::string("{\"type\":\"" BOARD_TYPE "\",");
            board_json += "\"name\":\"" BOARD_NAME "\",";
            if (!wifi_config_mode_) {
                board_json += "\"ssid\":\"" + wifi_station.GetSsid() + "\",";
                board_json += "\"rssi\":" + std::to_string(wifi_station.GetRssi()) + ",";
                board_json += "\"channel\":" + std::to_string(wifi_station.GetChannel()) + ",";
                board_json += "\"ip\":\"" + wifi_station.GetIpAddress() + "\",";
            }
            board_json += "\"mac\":\"" + SystemInfo::GetMacAddress() + "\"}";
            return board_json;
        }
    }
    
    // 重置Wi-Fi配置
    void ResetWifiConfiguration() {
        // 设置标志并重启设备进入网络配置模式
        {
            Settings settings("wifi", true);
            settings.SetInt("force_ap", 1);
        }
        GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);
        vTaskDelay(pdMS_TO_TICKS(1000));
        // 重启设备
        esp_restart();
    }
};

DECLARE_BOARD(RAN_LCD_4G);
