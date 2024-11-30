#include "st7789_display.h"
#include "font_awesome_symbols.h"
#include <esp_random.h>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <driver/ledc.h>
#include <vector>

#define TAG "St7789Display"
#define LCD_LEDC_CH LEDC_CHANNEL_0

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_30_1);
LV_FONT_DECLARE(font_awesome_14_1);

St7789Display::St7789Display(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           gpio_num_t backlight_pin, bool backlight_output_invert,
                           int width, int height, bool mirror_x, bool mirror_y, bool swap_xy)
    : panel_io_(panel_io), panel_(panel), backlight_pin_(backlight_pin), backlight_output_invert_(backlight_output_invert),
      mirror_x_(mirror_x), mirror_y_(mirror_y), swap_xy_(swap_xy) {
    width_ = width;
    height_ = height;

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&port_cfg);

    InitializeBacklight(backlight_pin);

    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "Adding LCD screen");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy_,
            .mirror_x = mirror_x_,
            .mirror_y = mirror_y_,
        },
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    disp_ = lvgl_port_add_disp(&display_cfg);

    SetBacklight(100);

    SetupUI();
}

St7789Display::~St7789Display() {
    if (left_eye_ != nullptr) {
        lv_obj_del(left_eye_);
    }
    if (right_eye_ != nullptr) {
        lv_obj_del(right_eye_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    lvgl_port_deinit();
}

void St7789Display::InitializeBacklight(gpio_num_t backlight_pin) {
    if (backlight_pin == GPIO_NUM_NC) {
        return;
    }

    // Setup LEDC peripheral for PWM backlight control
    const ledc_channel_config_t backlight_channel = {
        .gpio_num = backlight_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = backlight_output_invert_,
        }
    };
    const ledc_timer_config_t backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };

    ESP_ERROR_CHECK(ledc_timer_config(&backlight_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&backlight_channel));

    // 初始化时绘制黑色背景
    std::vector<uint16_t> buffer(width_, 0x0000);  // 改为黑色 0x0000
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }
}

void St7789Display::SetBacklight(uint8_t brightness) {
    if (backlight_pin_ == GPIO_NUM_NC) {
        return;
    }

    if (brightness > 100) {
        brightness = 100;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness);
    // LEDC resolution set to 10bits, thus: 100% = 1023
    uint32_t duty_cycle = (1023 * brightness) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));
}

void St7789Display::Lock() {
    lvgl_port_lock(0);
}

void St7789Display::Unlock() {
    lvgl_port_unlock();
}

void St7789Display::SetupUI() {
    DisplayLockGuard lock(this);

    auto screen = lv_disp_get_scr_act(disp_);
    
    // 设置屏幕背景为黑色
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // 直接在屏幕上创建眼睛,不使用容器
    SetupEyes();
}

void St7789Display::SetupEyes() {
    auto screen = lv_disp_get_scr_act(disp_);
    
    // 计算眼睛尺寸和位置
    int eye_size = height_ * EYE_SIZE_RATIO;  // 使用统一的尺寸比例
    
    // 计算居中位置
    int total_width = eye_size * 2 + EYE_SPACING;
    int start_x = (width_ - total_width) / 2;
    int y_pos = (height_ - eye_size) / 2;
    
    // 创建左眼
    left_eye_ = lv_obj_create(screen);
    lv_obj_set_size(left_eye_, eye_size, eye_size);
    lv_obj_set_pos(left_eye_, start_x, y_pos);
    lv_obj_set_style_bg_color(left_eye_, lv_color_white(), 0);
    lv_obj_set_style_border_width(left_eye_, 0, 0);  // 移除边框
    lv_obj_set_style_radius(left_eye_, EYE_CORNER_RADIUS, 0);
    lv_obj_set_style_pad_all(left_eye_, 0, 0);  // 移除内边距
    
    // 创建右眼
    right_eye_ = lv_obj_create(screen);
    lv_obj_set_size(right_eye_, eye_size, eye_size);
    lv_obj_set_pos(right_eye_, start_x + eye_size + EYE_SPACING, y_pos);
    lv_obj_set_style_bg_color(right_eye_, lv_color_white(), 0);
    lv_obj_set_style_border_width(right_eye_, 0, 0);  // 移除边框
    lv_obj_set_style_radius(right_eye_, EYE_CORNER_RADIUS, 0);
    lv_obj_set_style_pad_all(right_eye_, 0, 0);  // 移除内边距

    // 开始动作序列
    StartActionSequence();
}

void St7789Display::StartActionSequence() {
    if (sequence_running_) return;
    
    sequence_running_ = true;
    current_step_ = 0;
    
    // 创建定时器来执行动作序列
    sequence_timer_ = lv_timer_create([](lv_timer_t* t) {
        St7789Display* display = (St7789Display*)t->user_data;
        display->NextActionStep();
    }, IDLE_SEQUENCE[0].duration, this);
}

void St7789Display::StopActionSequence() {
    if (!sequence_running_) return;
    
    sequence_running_ = false;
    if (sequence_timer_) {
        lv_timer_del(sequence_timer_);
        sequence_timer_ = nullptr;
    }
}

void St7789Display::NextActionStep() {
    if (!sequence_running_) return;
    
    // 如果当前有动画在进行，等待完成
    if (is_blinking_ || is_looking_ || is_dizzy_) {
        // 重新调度当前步骤
        if (sequence_timer_) {
            lv_timer_set_period(sequence_timer_, 100);  // 100ms后再次检查
        }
        return;
    }
    
    // 执行当前动作
    SetEyeAction(IDLE_SEQUENCE[current_step_].action);
    
    // 移动到下一个动作
    current_step_ = (current_step_ + 1) % IDLE_SEQUENCE_SIZE;
    
    // 更新定时器间隔
    if (sequence_timer_) {
        lv_timer_set_period(sequence_timer_, IDLE_SEQUENCE[current_step_].duration);
    }
}

void St7789Display::SetEyeAction(EyeAction action) {
    // 如果正在眨眼，等待眨眼完成
    if (is_blinking_) return;
    
    // 如果正在移动，等待移动完成
    if (is_looking_) return;
    
    current_action_ = action;
    
    switch(action) {
        case ACTION_BLINK:
            StartBlinking();
            break;
        case ACTION_LOOK_LEFT:
            StartLookAnimation(-LOOK_OFFSET, 0);
            break;
        case ACTION_LOOK_RIGHT:
            StartLookAnimation(LOOK_OFFSET, 0);
            break;
        case ACTION_LOOK_UP:
            StartLookAnimation(0, -LOOK_OFFSET);
            break;
        case ACTION_LOOK_DOWN:
            StartLookAnimation(0, LOOK_OFFSET);
            break;
        case ACTION_LOOK_CENTER:
            StartLookAnimation(0, 0);  // 平滑回到中间
            break;
        case ACTION_DIZZY:
            StartDizzyAnimation();
            break;
        case ACTION_NORMAL:
            UpdateEyeAction();  // 直接更新状态
            break;
        default:
            UpdateEyeAction();
            break;
    }
}

void St7789Display::StartBlinking() {
    if (!is_blinking_) {
        is_blinking_ = true;
        
        int eye_size = height_ * EYE_SIZE_RATIO;
        
        lv_anim_init(&blink_anim_);
        lv_anim_set_exec_cb(&blink_anim_, BlinkAnimCallback);
        lv_anim_set_values(&blink_anim_, 0, eye_size);
        lv_anim_set_time(&blink_anim_, 200);          // 减少到200ms
        lv_anim_set_playback_time(&blink_anim_, 200); // 减少到200ms
        lv_anim_set_ready_cb(&blink_anim_, OnBlinkAnimComplete);
        lv_anim_set_var(&blink_anim_, this);
        lv_anim_set_user_data(&blink_anim_, this);
        
        lv_anim_start(&blink_anim_);
    }
}

void St7789Display::BlinkAnimCallback(void* obj, int32_t value) {
    if (!obj) return;
    
    St7789Display* display = (St7789Display*)obj;
    if (!display->left_eye_ || !display->right_eye_) return;
    
    int eye_size = display->height_ * EYE_SIZE_RATIO;
    
    // 通过改变高度和位置来实现从上到下眨眼
    int new_height = eye_size - value;
    int y_pos = display->height_ / 2 - eye_size/2 + value/2;
    
    // 更新左眼
    lv_obj_set_height(display->left_eye_, new_height);
    lv_obj_set_y(display->left_eye_, y_pos);
    
    // 更新右眼
    lv_obj_set_height(display->right_eye_, new_height);
    lv_obj_set_y(display->right_eye_, y_pos);
}

void St7789Display::OnBlinkAnimComplete(lv_anim_t* anim) {
    if (!anim || !anim->user_data) return;
    
    St7789Display* display = (St7789Display*)anim->user_data;
    display->is_blinking_ = false;
    
    // 只在正常状态下才继续眨眼
    if (display->current_action_ == ACTION_NORMAL) {
        // 增加随机延时范围
        uint32_t delay = esp_random() % 4000 + 3000;  // 3-7秒随机间隔
        lv_timer_create([](lv_timer_t* t) {
            if (!t || !t->user_data) return;
            St7789Display* display = (St7789Display*)t->user_data;
            // 再次检查是否处于正常状态
            if (display->current_action_ == ACTION_NORMAL) {
                display->StartBlinking();
            }
            lv_timer_del(t);
        }, delay, display);
    }
}

void St7789Display::StartLookAnimation(float x_offset, float y_offset) {
    if (is_looking_) return;
    is_looking_ = true;
    
    // 停止当前的眨眼动画
    if (is_blinking_) {
        lv_anim_del(this, BlinkAnimCallback);
        is_blinking_ = false;
    }
    
    int eye_size = height_ * EYE_SIZE_RATIO;
    
    // 计算居中位置
    int total_width = eye_size * 2 + EYE_SPACING;
    int center_x = (width_ - total_width) / 2;
    int y_pos = (height_ - eye_size) / 2;
    
    // 获取当前位置
    lv_coord_t curr_x_left = lv_obj_get_x(left_eye_);
    lv_coord_t curr_y_left = lv_obj_get_y(left_eye_);
    
    // 计算目标位置
    int target_x_left = center_x + static_cast<int>(x_offset * eye_size);
    int target_y = y_pos + static_cast<int>(y_offset * eye_size);
    
    // 如果是回到正常位置，使用当前位置计算过渡
    if (x_offset == 0 && y_offset == 0) {
        // 使用更长的时间回到中间
        lv_anim_set_time(&look_anim_, 1000);  // 1秒
    } else {
        lv_anim_set_time(&look_anim_, 800);   // 800ms
    }
    
    // 初始化视线移动动画
    lv_anim_init(&look_anim_);
    lv_anim_set_exec_cb(&look_anim_, LookAnimCallback);
    lv_anim_set_values(&look_anim_, 0, 1000);
    lv_anim_set_path_cb(&look_anim_, lv_anim_path_ease_in_out);
    
    auto* data = new LookAnimData {
        curr_x_left, curr_x_left + eye_size + EYE_SPACING, curr_y_left,
        target_x_left, target_x_left + eye_size + EYE_SPACING, target_y
    };
    
    lv_anim_set_user_data(&look_anim_, data);
    lv_anim_set_ready_cb(&look_anim_, [](lv_anim_t* a) {
        auto* display = (St7789Display*)a->var;
        auto* data = (LookAnimData*)a->user_data;
        display->is_looking_ = false;
        delete data;
    });
    
    lv_anim_set_var(&look_anim_, this);
    lv_anim_start(&look_anim_);
}

void St7789Display::LookAnimCallback(void* obj, int32_t value) {
    auto* display = (St7789Display*)obj;
    auto* data = (LookAnimData*)display->look_anim_.user_data;
    
    float progress = value / 1000.0f;
    float eased_progress = 1.0f - (1.0f - progress) * (1.0f - progress);  // 二次缓动
    
    // 计算左眼位置
    int curr_x_left = data->start_x_left + (data->target_x_left - data->start_x_left) * eased_progress;
    int curr_y = data->start_y + (data->target_y - data->start_y) * eased_progress;
    
    // 根据移动方向调整眼睛大小
    float offset = (curr_x_left - data->start_x_left) / (float)display->width_;
    float scale_left = 1.0f + offset * 0.3f;   // 减小变形程度到30%
    float scale_right = 1.0f - offset * 0.3f;
    
    int eye_size = display->height_ * EYE_SIZE_RATIO;
    int left_width = eye_size * scale_left;
    int right_width = eye_size * scale_right;
    
    // 更新左眼
    lv_obj_set_size(display->left_eye_, left_width, eye_size * scale_left);
    lv_obj_set_pos(display->left_eye_, curr_x_left, curr_y);
    
    // 计算右眼位置 - 保持固定间距
    int curr_x_right = curr_x_left + left_width + EYE_SPACING;
    lv_obj_set_size(display->right_eye_, right_width, eye_size * scale_right);
    lv_obj_set_pos(display->right_eye_, curr_x_right, curr_y);
}

void St7789Display::UpdateEyeAction() {
    int eye_size = height_ * EYE_SIZE_RATIO;
    int total_width = eye_size * 2 + EYE_SPACING;  // 使用固定间距
    int start_x = (width_ - total_width) / 2;
    int y_pos = (height_ - eye_size) / 2;
    
    float scale = 1.0f;
    switch(current_action_) {
        case ACTION_HAPPY:
            scale = HAPPY_SCALE;
            y_pos += eye_size * 0.2f;
            break;
        case ACTION_ANGRY:
            scale = ANGRY_SCALE;
            y_pos -= eye_size * 0.2f;
            break;
        case ACTION_SURPRISED:
            scale = SURPRISED_SCALE;
            break;
        case ACTION_SLEEPY:
            scale = SLEEPY_SCALE;
            y_pos += eye_size * 0.1f;
            break;
        default:
            break;
    }
    
    // 更新左右眼
    int new_size = eye_size * scale;
    lv_obj_set_size(left_eye_, new_size, new_size);
    lv_obj_set_pos(left_eye_, start_x, y_pos);
    
    // 使用固定间距
    lv_obj_set_size(right_eye_, new_size, new_size);
    lv_obj_set_pos(right_eye_, start_x + new_size + EYE_SPACING, y_pos);
}

void St7789Display::StartDizzyAnimation() {
    if (is_dizzy_) return;
    is_dizzy_ = true;
    
    // 初始化眩晕动画
    lv_anim_init(&dizzy_anim_);
    lv_anim_set_exec_cb(&dizzy_anim_, DizzyAnimCallback);
    lv_anim_set_values(&dizzy_anim_, 0, 360 * 2);  // 旋转两圈
    lv_anim_set_time(&dizzy_anim_, DIZZY_TIME);
    lv_anim_set_path_cb(&dizzy_anim_, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&dizzy_anim_, [](lv_anim_t* a) {
        auto* display = (St7789Display*)a->var;
        display->is_dizzy_ = false;
        
        // 恢复正常状态
        lv_obj_set_style_transform_angle(display->left_eye_, 0, 0);
        lv_obj_set_style_transform_angle(display->right_eye_, 0, 0);
    });
    
    lv_anim_set_var(&dizzy_anim_, this);
    lv_anim_start(&dizzy_anim_);
}

void St7789Display::DizzyAnimCallback(void* obj, int32_t value) {
    auto* display = (St7789Display*)obj;
    
    // 设置眼睛旋转角度
    lv_obj_set_style_transform_angle(display->left_eye_, value * 10, 0);
    lv_obj_set_style_transform_angle(display->right_eye_, value * 10, 0);
    
    // 设置旋转中心点
    int eye_size = display->height_ * EYE_SIZE_RATIO;
    
    lv_obj_set_style_transform_pivot_x(display->left_eye_, eye_size/2, 0);
    lv_obj_set_style_transform_pivot_y(display->left_eye_, eye_size/2, 0);
    lv_obj_set_style_transform_pivot_x(display->right_eye_, eye_size/2, 0);
    lv_obj_set_style_transform_pivot_y(display->right_eye_, eye_size/2, 0);
}
