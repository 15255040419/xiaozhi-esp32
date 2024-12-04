#include "st7789_display.h"
#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lvgl_port.h>

#define TAG "St7789Display"

St7789Display::St7789Display(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           gpio_num_t backlight_pin, bool backlight_output_invert,
                           int width, int height, int offset_x, int offset_y,
                           bool mirror_x, bool mirror_y, bool swap_xy)
    : panel_io_(panel_io), panel_(panel),
      backlight_pin_(backlight_pin), backlight_output_invert_(backlight_output_invert),
      mirror_x_(mirror_x), mirror_y_(mirror_y), swap_xy_(swap_xy),
      offset_x_(offset_x), offset_y_(offset_y),
      width_(width), height_(height) {
    
    InitializeBacklight(backlight_pin);
    SetBacklight(100);  // 设置背光亮度为100%

    // 初始化显示参数
    esp_lcd_panel_reset(panel_);
    esp_lcd_panel_init(panel_);
    esp_lcd_panel_invert_color(panel_, true);
    esp_lcd_panel_swap_xy(panel_, swap_xy_);
    esp_lcd_panel_mirror(panel_, mirror_x_, mirror_y_);
    esp_lcd_panel_disp_on_off(panel_, true);

    // 创建眼睛动画控制器
    eyes_animation_ = std::make_unique<EyesAnimation>(panel_, width_, height_);
    eyes_animation_->Start();
}

St7789Display::~St7789Display() {
    eyes_animation_.reset();
    
    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
}

void St7789Display::InitializeBacklight(gpio_num_t backlight_pin) {
    if (backlight_pin != GPIO_NUM_NC) {
        gpio_config_t bk_gpio_config = {
            .pin_bit_mask = 1ULL << backlight_pin,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    }
}

void St7789Display::SetBacklight(uint8_t brightness) {
    if (backlight_pin_ != GPIO_NUM_NC) {
        bool level = brightness > 0;
        if (backlight_output_invert_) {
            level = !level;
        }
        gpio_set_level(backlight_pin_, level);
    }
}

void St7789Display::SetEmotion(EyesAnimation::State state) {
    if (eyes_animation_) {
        eyes_animation_->SetState(state);
    }
}

bool St7789Display::Lock(int timeout_ms) {
    return true;
}

void St7789Display::Unlock() {
}
