#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "display/emote_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "esp32_camera.h"
#include "sdcard.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_timer.h>
#include <wifi_station.h> 
#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>

#define TAG "LichuangDevBoard"

extern "C" bool clock_face_is_active();

class Pca9557 : public I2cDevice {
public:
    Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x01, 0x03);
        WriteReg(0x03, 0xf8);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint8_t data = ReadReg(0x01);
        data = (data & ~(1 << bit)) | (level << bit);
        WriteReg(0x01, data);
    }
};

class CustomAudioCodec : public BoxAudioCodec {
private:
    Pca9557* pca9557_;

public:
    CustomAudioCodec(i2c_master_bus_handle_t i2c_bus, Pca9557* pca9557) 
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
          pca9557_(pca9557) {
    }

    virtual void EnableOutput(bool enable) override {
        BoxAudioCodec::EnableOutput(enable);
        if (enable) {
            pca9557_->SetOutputState(1, 1);
        } else {
            pca9557_->SetOutputState(1, 0);
        }
    }
};

class LichuangDevBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t pca9557_handle_;
    Button boot_button_;
    Display* display_;
    Pca9557* pca9557_;
    // 音量手势控制
    lv_obj_t* volume_gesture_obj_ = nullptr;
    lv_obj_t* volume_bar_obj_ = nullptr;
    Esp32Camera* camera_;
    SdCard sdcard_;

    void InitializeI2c() {
        // Initialize I2C peripheral
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

        // Initialize PCA9557
        pca9557_ = new Pca9557(i2c_bus_, 0x19);
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
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

#if CONFIG_USE_DEVICE_AEC
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            }
        });
#endif
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
        
        esp_lcd_panel_reset(panel);
        pca9557_->SetOutputState(0, 0);

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
                .swap_xy = 1,
                .mirror_x = 1,
                .mirror_y = 0,
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
        LichuangDevBoard* board = (LichuangDevBoard*)lv_event_get_user_data(e);
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
        pca9557_->SetOutputState(2, 0);

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
    }

public:
    LichuangDevBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeTouch();
        InitializeButtons();
        InitializeCamera();
        InitializeSdCard();

        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static CustomAudioCodec audio_codec(
            i2c_bus_, 
            pca9557_);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(LichuangDevBoard);
