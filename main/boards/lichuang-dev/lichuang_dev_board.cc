#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "iot/thing_manager.h"
#include "muyuPic.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>
#include <esp_timer.h>

#define TAG "LichuangDevBoard"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

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

class Ft6336 : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };
    
    Ft6336(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        uint8_t chip_id = ReadReg(0xA3);
        ESP_LOGI(TAG, "Get chip ID: 0x%02X", chip_id);
        read_buffer_ = new uint8_t[6];
    }

    ~Ft6336() {
        delete[] read_buffer_;
    }

    void UpdateTouchPoint() {
        ReadRegs(0x02, read_buffer_, 6);
        tp_.num = read_buffer_[0] & 0x0F;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    inline const TouchPoint_t& GetTouchPoint() {
        return tp_;
    }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;
};

class MuYuDisplay {
public:
    MuYuDisplay(LcdDisplay* display) : display_(display) {
        InitializeMuYuUI();
    }

    ~MuYuDisplay() {
        // 不需要手动释放LVGL对象，LVGL会自动管理
    }

    void InitializeMuYuUI() {
        auto* screen = lv_disp_get_scr_act(lv_disp_get_default());
        
        // 创建木鱼图像
        if (m_pMuYuImg == nullptr) {
            m_pMuYuImg = lv_img_create(screen);
            lv_img_set_src(m_pMuYuImg, &muyu_Pic);
            lv_obj_set_pos(m_pMuYuImg, 0, 60); // 调整位置适合lichuang屏幕
            // 初始隐藏木鱼图像
            lv_obj_add_flag(m_pMuYuImg, LV_OBJ_FLAG_HIDDEN);
        }

        // 初始化 "+1" 标签
        if (plus_one_label == nullptr) {
            plus_one_label = lv_label_create(screen);
            lv_label_set_text(plus_one_label, "功德+1");
            lv_obj_set_style_text_color(plus_one_label, lv_color_hex(0xFFD700), 0); // 金色文字
            lv_obj_align(plus_one_label, LV_ALIGN_CENTER, 30, -25);
            lv_obj_add_flag(plus_one_label, LV_OBJ_FLAG_HIDDEN); // 初始隐藏
        }
        
        m_bInit = true;
    }

    void MuYuUIShowOrHide(bool bShow) {
        if (!m_bInit || display_ == nullptr) return;
        
        // 使用DisplayLockGuard替代直接Lock/Unlock调用
        DisplayLockGuard lock(display_);
        if (bShow) {
            // 显示木鱼图片
            lv_obj_clear_flag(m_pMuYuImg, LV_OBJ_FLAG_HIDDEN);
        } else {
            // 隐藏木鱼图片和功德+1
            lv_obj_add_flag(m_pMuYuImg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(plus_one_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void MuYuGDPlusOne(bool bShow) {
        if (!m_bInit || display_ == nullptr) return;
        
        // 使用DisplayLockGuard替代直接Lock/Unlock调用
        DisplayLockGuard lock(display_);
        if (bShow) {
            // 显示功德+1
            lv_obj_clear_flag(plus_one_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            // 隐藏功德+1
            lv_obj_add_flag(plus_one_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    bool GetInitStatus() {
        return m_bInit;
    }

private:
    LcdDisplay* display_ = nullptr;
    lv_obj_t* m_pMuYuImg = nullptr;
    lv_obj_t* plus_one_label = nullptr;
    bool m_bInit = false;
};

class LichuangDevBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t pca9557_handle_;
    Button boot_button_;
    LcdDisplay* display_;
    Pca9557* pca9557_;
    Ft6336* ft6336_;
    esp_timer_handle_t touchpad_timer_;
    
    // 木鱼相关变量
    std::atomic<bool> m_bMuYuMode = false;
    MuYuDisplay* muyu_display_ = nullptr;
    int64_t boot_press_time_ = 0; // 用于记录BOOT按钮按下的时间

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
        boot_button_.OnPressDown([this]() {
            // 记录按下时间点
            boot_press_time_ = esp_timer_get_time() / 1000; // 毫秒
        });
        
        boot_button_.OnPressUp([this]() {
            int64_t now = esp_timer_get_time() / 1000;
            int64_t press_duration = now - boot_press_time_;
            
            // 长按3秒以上，切换木鱼模式
            if (press_duration >= 3000) {
                ToggleMuYuMode();
            } else {
                // 短按保持原来的功能
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                    ResetWifiConfiguration();
                }
                app.ToggleChatState();
            }
        });
    }

    void ToggleMuYuMode() {
        if (!m_bMuYuMode) {
            // 进入木鱼模式
            ESP_LOGI(TAG, "进入木鱼模式");
            if (display_ && muyu_display_) {
                // 隐藏常规UI，显示木鱼
                muyu_display_->MuYuUIShowOrHide(true);
                m_bMuYuMode = true;
            }
        } else {
            // 退出木鱼模式
            ESP_LOGI(TAG, "退出木鱼模式");
            if (display_ && muyu_display_) {
                // 隐藏木鱼，显示常规UI
                muyu_display_->MuYuUIShowOrHide(false);
                m_bMuYuMode = false;
                
                // 显示欢迎界面 - 使用正确的方法替代ShowWelcome
                GetDisplay()->SetStatus("欢迎使用");
                GetDisplay()->SetEmotion("smile");
            }
        }
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
    }

    void PollTouchpad() {
        static bool was_touched = false;
        static int64_t touch_start_time = 0;
        const int64_t TOUCH_THRESHOLD_MS = 500;  // 触摸时长阈值，超过500ms视为长按
        
        ft6336_->UpdateTouchPoint();
        auto& touch_point = ft6336_->GetTouchPoint();
        
        // 木鱼模式下的触摸处理
        if (m_bMuYuMode) {
            // 检测触摸开始 - 木鱼模式
            if (touch_point.num > 0 && !was_touched) {
                was_touched = true;
                if (muyu_display_) {
                    muyu_display_->MuYuGDPlusOne(true);
                    ESP_LOGI(TAG, "敲击木鱼 功德+1");
                    
                    // 播放木鱼敲击音效
                    // 目前使用vibration.p3作为木鱼音效（更接近敲击声）
                    // 未来可以转换真正的木鱼音效文件(muyu.wav)，需要安装完整的Opus库
                    extern const char vibration_p3[] asm("_binary_vibration_p3_start");
                    extern const char vibration_p3_end[] asm("_binary_vibration_p3_end");
                    
                    // 播放音效 - 修复字符串视图转换错误
                    std::string_view sound_view(vibration_p3, static_cast<size_t>(vibration_p3_end - vibration_p3));
                    Application::GetInstance().PlaySound(sound_view);
                }
            } 
            // 检测触摸释放 - 木鱼模式
            else if (touch_point.num == 0 && was_touched) {
                was_touched = false;
                if (muyu_display_) {
                    muyu_display_->MuYuGDPlusOne(false);
                }
            }
            return; // 木鱼模式下不执行原来的触摸处理
        }
        
        // 正常模式下的触摸处理（保留原代码功能）
        // 检测触摸开始
        if (touch_point.num > 0 && !was_touched) {
            was_touched = true;
            touch_start_time = esp_timer_get_time() / 1000; // 转换为毫秒
        } 
        // 检测触摸释放
        else if (touch_point.num == 0 && was_touched) {
            was_touched = false;
            int64_t touch_duration = (esp_timer_get_time() / 1000) - touch_start_time;
            
            // 只有短触才触发
            if (touch_duration < TOUCH_THRESHOLD_MS) {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting && 
                    !WifiStation::GetInstance().IsConnected()) {
                    ResetWifiConfiguration();
                }
                app.ToggleChatState();
            }
        }
    }

    void InitializeFt6336TouchPad() {
        ESP_LOGI(TAG, "Init FT6336");
        ft6336_ = new Ft6336(i2c_bus_, TOUCH_I2C_ADDR);
        
        // 创建定时器，20ms 间隔
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                LichuangDevBoard* board = (LichuangDevBoard*)arg;
                board->PollTouchpad();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "touchpad_timer",
            .skip_unhandled_events = true,
        };
        
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &touchpad_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(touchpad_timer_, 20 * 1000));
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
		thing_manager.AddThing(iot::CreateThing("Wallpaper"));
    }

    // 初始化木鱼显示
    void InitializeMuYuDisplay() {
        if (display_) {
            muyu_display_ = new MuYuDisplay(display_);
        }
    }

public:
    LichuangDevBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeFt6336TouchPad();
        InitializeButtons();
        InitializeIot();
        InitializeMuYuDisplay(); // 初始化木鱼显示
        GetBacklight()->RestoreBrightness();
    }

    ~LichuangDevBoard() {
        if (touchpad_timer_) {
            esp_timer_stop(touchpad_timer_);
            esp_timer_delete(touchpad_timer_);
        }
        if (muyu_display_) {
            delete muyu_display_;
        }
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

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    Ft6336* GetTouchpad() {
        return ft6336_;
    }
};

DECLARE_BOARD(LichuangDevBoard);
