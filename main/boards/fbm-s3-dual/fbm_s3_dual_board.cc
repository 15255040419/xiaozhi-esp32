#include "dual_network_board.h" 
#include "codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "display/emote_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "axp2101.h"
#include "power_save_timer.h"
#include "sdcard.h"
#include "esp32_camera.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_timer.h>
#include <wifi_station.h> 
#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>

extern "C" bool clock_face_is_active();


#define TAG "FbmS3DualBoard"

class Pmic : public Axp2101 {
public:
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
        ESP_LOGI(TAG, "Init AXP2101 PMIC");
        
        WriteReg(0x22, 0b110); // PWRON > OFFLEVEL as POWEROFF Source enable
        WriteReg(0x27, 0x21);  // hold 4s to power off

        WriteReg(0x92, 0x1C); // 配置 aldo1 输出为 3.3V
        WriteReg(0x93, 0x17); // 配置 aldo2 输出为 2.8V
        WriteReg(0x94, 0x17); // 新增: 配置 aldo3 输出为 2.8V
        WriteReg(0x95, 0x0D); // 新增: 配置 aldo4 输出为 1.8V
    
        uint8_t value = ReadReg(0x90); // XPOWERS_AXP2101_LDO_ONOFF_CTRL0
        value = value | 0x0E; // 修改: 同时使能 ALDO2, ALDO3, ALDO4 (bit 1, 2, 3)
        WriteReg(0x90, value);  // and power channels now enabled
    
        WriteReg(0x64, 0x03); // CV charger voltage setting to 4.2V
        
        WriteReg(0x61, 0x05); // set Main battery precharge current to 125mA
        WriteReg(0x62, 0x0A); // set Main battery charger current to 400mA ( 0x08-200mA, 0x09-300mA, 0x0A-400mA )
        WriteReg(0x63, 0x15); // set Main battery term charge current to 125mA
    
        WriteReg(0x14, 0x00); // set minimum system voltage to 4.1V (default 4.7V), for poor USB cables
        WriteReg(0x15, 0x00); // set input voltage limit to 3.88v, for poor USB cables
        WriteReg(0x16, 0x05); // set input current limit to 2000mA
    
        WriteReg(0x24, 0x01); // set Vsys for PWROFF threshold to 3.2V (default - 2.6V and kill battery)
        WriteReg(0x50, 0x14); // set TS pin to EXTERNAL input (not temperature)
    }

    void SetBacklightRegValue(uint8_t reg_val) {
        if (reg_val > 0x1F) { // 0x1F 是 31，ALDO的最大设置值
            reg_val = 0x1F;
        }
        WriteReg(0x93, reg_val);
    }

    void EnableBacklight(bool enable) {
        uint8_t ldo_en_ctrl = ReadReg(0x90);
        if (enable) {
            ldo_en_ctrl |= (1 << 1);
        } else {
            ldo_en_ctrl &= ~(1 << 1);
        }
        WriteReg(0x90, ldo_en_ctrl);
    }
};

class Aw9523b : public I2cDevice {
public:
    Aw9523b(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x7F, 0x00); vTaskDelay(pdMS_TO_TICKS(10));
        WriteReg(0x11, 0x01); 
        vTaskDelay(pdMS_TO_TICKS(10));
        WriteReg(0x02, 0x00);  // P0 初始输出状态
        WriteReg(0x03, 0x00);  // P1 初始输出状态  
        WriteReg(0x04, 0x00);  // P0 配置为输出
        WriteReg(0x05, 0x00);  // P1 配置为输出
        WriteReg(0x12, 0x05);  // P0_0,P0_2 为GPIO模式（只使用这两个引脚）
        WriteReg(0x13, 0x00);  // P1 全部为GPIO模式
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint8_t reg = (bit < 8) ? 0x02 : 0x03;
        uint8_t pin = (bit < 8) ? bit : bit - 8;
        uint8_t data = ReadReg(reg);
        data = (data & ~(1 << pin)) | (level << pin);
        WriteReg(reg, data);
    }
};


class PmicBacklight : public Backlight {
private: 
    Pmic* pmic_;
    static constexpr const char* BL_TAG = "PmicBacklight"; 

public:
    PmicBacklight(Pmic* pmic) : pmic_(pmic) {}

    void SetBrightnessImpl(uint8_t brightness) override {
        if (!pmic_) {
            ESP_LOGE(BL_TAG, "PMIC not initialized!");
            return;
        }

        if (brightness == 0) {
            pmic_->EnableBacklight(false);
            ESP_LOGI(BL_TAG, "Backlight OFF");
            return;
        }
        
        pmic_->EnableBacklight(true);
        uint8_t reg_val = (brightness >> 5) + 20; // 计算寄存器值，范围从20到27
        pmic_->SetBacklightRegValue(reg_val); //计算出的值写入ALDO2的电压控制寄存器 (0x93)

        ESP_LOGI(BL_TAG, "Set brightness to %u", brightness);
    }
};


class FbmS3DualAudioCodec : public BoxAudioCodec {
private:
    Aw9523b* aw9523b_;

public:
    FbmS3DualAudioCodec(i2c_master_bus_handle_t i2c_bus, Aw9523b* aw9523b) 
        : BoxAudioCodec(i2c_bus, 
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
                       AUDIO_INPUT_REFERENCE),
          aw9523b_(aw9523b) {
    }

    virtual void EnableOutput(bool enable) override {
        BoxAudioCodec::EnableOutput(enable);
        if (enable) {
            aw9523b_->SetOutputState(0, 1);
        } else {
            aw9523b_->SetOutputState(0, 0);
        }
    }
};


class FbmS3DualBoard : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Pmic* pmic_;
    Button boot_button_;
    Display* display_;
    Aw9523b* aw9523b_;
    // 音量手势控制
    lv_obj_t* volume_gesture_obj_ = nullptr;
    lv_obj_t* volume_bar_obj_ = nullptr;
    Esp32Camera* camera_;
    PowerSaveTimer* power_save_timer_;
    SdCard sdcard_;

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling sleep mode");
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("sleepy");
            GetBacklight()->SetBrightness(30); });
        power_save_timer_->OnExitSleepMode([this]() {
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("neutral");
            GetBacklight()->RestoreBrightness(); });
        power_save_timer_->OnShutdownRequest([this](){ 
            pmic_->PowerOff(); });
        power_save_timer_->SetEnabled(true);
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
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        // Initialize Aw9523b
        aw9523b_ = new Aw9523b(i2c_bus_, 0x58);
    }

    void InitializePmic() { 
        pmic_ = new Pmic(i2c_bus_, 0x34); 
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SCLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
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
        
        esp_lcd_panel_reset(panel);
        aw9523b_->SetOutputState(0, 0); // 关闭背光

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

#if CONFIG_USE_EMOTE_MESSAGE_STYLE
        display_ = new emote::EmoteDisplay(panel, panel_io, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#else
        display_ = new SpiLcdDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
#endif
    }

    void InitializeTouch()
    {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_HEIGHT,
            .y_max = DISPLAY_WIDTH,
            .rst_gpio_num = GPIO_NUM_NC, // Shared with LCD reset
            .int_gpio_num = GPIO_NUM_NC, 
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = DISPLAY_SWAP_XY,
                .mirror_x = DISPLAY_MIRROR_X,
                .mirror_y = DISPLAY_MIRROR_Y,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
        tp_io_config.scl_speed_hz = 400000;

        esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle);
        esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp);
        assert(tp);

        /* Add touch input (for selected screen) */
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(), 
            .handle = tp,
        };

        if(touch_cfg.disp) {
            lvgl_port_add_touch(&touch_cfg);
        } else {
            ESP_LOGE(TAG, "Touch display is not initialized");
        }
          // Setup volume control gesture
        SetupVolumeGesture();
    }
    
    void SetupVolumeGesture() {
        // 创建一个透明的对象来捕获触摸事件
        volume_gesture_obj_ = lv_obj_create(lv_screen_active());
        lv_obj_set_size(volume_gesture_obj_, 60, DISPLAY_HEIGHT); // 右侧60像素宽度
        lv_obj_set_pos(volume_gesture_obj_, DISPLAY_WIDTH - 60, 0); // 位置在右侧
        lv_obj_set_style_bg_opa(volume_gesture_obj_, LV_OPA_TRANSP, 0); // 透明背景
        lv_obj_set_style_border_opa(volume_gesture_obj_, LV_OPA_TRANSP, 0); // 透明边框
        lv_obj_set_style_outline_opa(volume_gesture_obj_, LV_OPA_TRANSP, 0); // 透明轮廓
        lv_obj_add_flag(volume_gesture_obj_, LV_OBJ_FLAG_CLICKABLE); // 可点击
        
        // 添加触摸事件处理
        lv_obj_add_event_cb(volume_gesture_obj_, VolumeGestureEventCb, LV_EVENT_ALL, this);
        
        // 创建音量指示条（初始隐藏）
        volume_bar_obj_ = lv_bar_create(lv_screen_active());
        lv_obj_set_size(volume_bar_obj_, 20, 200);
        lv_obj_set_pos(volume_bar_obj_, DISPLAY_WIDTH - 40, (DISPLAY_HEIGHT - 200) / 2);
        lv_bar_set_range(volume_bar_obj_, 0, 100);
        lv_obj_add_flag(volume_bar_obj_, LV_OBJ_FLAG_HIDDEN); // 初始隐藏
        
        // 设置进度条样式
        lv_obj_set_style_bg_color(volume_bar_obj_, lv_color_hex(0x404040), LV_PART_MAIN);
        lv_obj_set_style_bg_color(volume_bar_obj_, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
        lv_obj_set_style_radius(volume_bar_obj_, 10, LV_PART_MAIN);
        lv_obj_set_style_radius(volume_bar_obj_, 10, LV_PART_INDICATOR);
    }
    
    static void VolumeGestureEventCb(lv_event_t* e) {
        FbmS3DualBoard* board = (FbmS3DualBoard*)lv_event_get_user_data(e);
        board->HandleVolumeGesture(e);
    }
    
    
    void HandleVolumeGesture(lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        static int16_t start_y = 0;
        static int start_volume = 0;
        static bool gesture_active = false;

        // 在时钟界面（含切换模式）激活时，彻底忽略音量手势
        if (clock_face_is_active()) {
            return;
        }
        
        if (code == LV_EVENT_PRESSED) {
            lv_indev_t* indev = lv_indev_get_act();
            lv_point_t point;
            lv_indev_get_point(indev, &point);
            start_y = point.y;
            start_volume = GetAudioCodec()->output_volume();
            gesture_active = true;
            
            // 显示音量条
            lv_obj_clear_flag(volume_bar_obj_, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(volume_bar_obj_, start_volume, LV_ANIM_OFF);
            
            // 唤醒省电定时器
            power_save_timer_->WakeUp();
            
        } else if (code == LV_EVENT_PRESSING && gesture_active) {
            lv_indev_t* indev = lv_indev_get_act();
            lv_point_t point;
            lv_indev_get_point(indev, &point);
            
            // 计算音量变化（向上滑动增加音量，向下滑动减少音量）
            int16_t delta_y = start_y - point.y; // 注意方向：向上为正
            int volume_change = delta_y / 3; // 每3像素改变1%音量
            int new_volume = start_volume + volume_change;
            
            // 限制音量范围
            if (new_volume < 0) new_volume = 0;
            if (new_volume > 100) new_volume = 100;
            
            // 只有音量真的变化了才更新（避免过于频繁的调用）
            static int last_volume = -1;
            if (new_volume != last_volume) {
                last_volume = new_volume;
                
                // 更新音量
                GetAudioCodec()->SetOutputVolume(new_volume);
                lv_bar_set_value(volume_bar_obj_, new_volume, LV_ANIM_OFF);
                
                // 实时显示音量通知（每隔5%才显示，减少频繁更新）
                if (new_volume % 5 == 0 || new_volume == 0 || new_volume == 100) {
                    GetDisplay()->ShowNotification(("音量: " + std::to_string(new_volume) + "%").c_str());
                }
            }
            
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            if (gesture_active) {
                gesture_active = false;
                
                // 显示最终音量通知
                int current_volume = GetAudioCodec()->output_volume();
                GetDisplay()->ShowNotification(("音量设置: " + std::to_string(current_volume) + "%").c_str());
                
                // 立即隐藏音量条（不使用定时器，避免死锁）
                lv_obj_add_flag(volume_bar_obj_, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // 启用/禁用触摸音量手势
    void SetVolumeGestureEnabled(bool enabled) {
        if (!volume_gesture_obj_) return;
        if (enabled) {
            lv_obj_clear_flag(volume_gesture_obj_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(volume_gesture_obj_, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_add_flag(volume_gesture_obj_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(volume_gesture_obj_, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto& app = Application::GetInstance();
            if (GetNetworkType() == NetworkType::WIFI) {
                if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                    auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                    wifi_board.ResetWifiConfiguration();
                }
            }
            app.ToggleChatState();
        });

        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
#if CONFIG_USE_DEVICE_AEC
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
                return;
            }
#endif
            if (app.GetDeviceState() == kDeviceStateStarting || app.GetDeviceState() == kDeviceStateWifiConfiguring) {
                SwitchNetworkType();
            }
        });
    }

    void InitializeSdCard() {
        // 1-bit SDMMC: CLK/CMD/D0 引脚见 config.h
        esp_err_t err = sdcard_.Mount1Bit(SDMMC_CLK_GPIO, SDMMC_CMD_GPIO, SDMMC_D0_GPIO, "/sdcard");
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "SD card ready at %s", sdcard_.mount_point().c_str());
        } else {
            ESP_LOGW(TAG, "SD card not mounted: %s", esp_err_to_name(err));
        }
    }

    void InitializeCamera() {
        // Open camera power
        aw9523b_->SetOutputState(2, 0);

        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                [0] = CAMERA_PIN_D0,
                [1] = CAMERA_PIN_D1,
                [2] = CAMERA_PIN_D2,
                [3] = CAMERA_PIN_D3,
                [4] = CAMERA_PIN_D4,
                [5] = CAMERA_PIN_D5,
                [6] = CAMERA_PIN_D6,
                [7] = CAMERA_PIN_D7,
            },
            .vsync_io = CAMERA_PIN_VSYNC,
            .de_io = CAMERA_PIN_HREF,
            .pclk_io = CAMERA_PIN_PCLK,
            .xclk_io = CAMERA_PIN_XCLK,
        };

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = false,
            .i2c_handle = i2c_bus_,
            .freq = 100000,
        };

        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin = CAMERA_PIN_RESET,
            .pwdn_pin = CAMERA_PIN_PWDN,
            .dvp_pin = dvp_pin_config,
            .xclk_freq = XCLK_FREQ_HZ,
        };

        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };

        camera_ = new Esp32Camera(video_config);
        
        // 设置摄像头翻转，解决图像倒立问题
        //camera_->SetVFlip(true);  // 垂直翻转
        // camera_->SetHMirror(true);  // 如需水平镜像，取消此行注释
    }

public:
    FbmS3DualBoard() : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, GPIO_NUM_NC, 0), boot_button_(BOOT_BUTTON_GPIO) {
        // default_net_type=0 表示默认使用 WiFi，节省内存并加快启动
        InitializePowerSaveTimer();
        InitializeI2c();
        vTaskDelay(pdMS_TO_TICKS(100));
        InitializePmic();
        GetBacklight()->RestoreBrightness();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();
        InitializeTouch();
        InitializeCamera();
        InitializeSdCard();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static FbmS3DualAudioCodec audio_codec(
            i2c_bus_,
            aw9523b_);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }

    virtual Backlight* GetBacklight() override {
        static PmicBacklight backlight(pmic_);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (!pmic_) return false;
        static bool last_discharging = false;
        static bool initialized = false;
        
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        
        // 放电状态变化时，控制PowerSaveTimer
        if (!initialized || discharging != last_discharging) {
            if (discharging) {
                // 放电时启用省电模式
                power_save_timer_->SetEnabled(true);
                ESP_LOGI(TAG, "Discharging detected, enabling power save timer");
            } else {
                // 不在放电时禁用省电模式（充电或充电完成但插着充电器）
                power_save_timer_->SetEnabled(false);
                ESP_LOGI(TAG, "Not discharging, disabling power save timer");
            }
            last_discharging = discharging;
            initialized = true;
        }
        
        level = pmic_->GetBatteryLevel();
        return true;
    }


    virtual void SetPowerSaveMode(bool enabled) override {
        if (!enabled) {
                power_save_timer_->WakeUp();
            }
        DualNetworkBoard::SetPowerSaveMode(enabled);
    }
};

DECLARE_BOARD(FbmS3DualBoard);
