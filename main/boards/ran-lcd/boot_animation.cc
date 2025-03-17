#include "boot_animation.h"
#include "lvgl.h"  // 确保包含完整的 LVGL 头文件

#define TAG "BootAnimation"

// 添加静态变量跟踪动画是否已播放
bool BootAnimation::animation_played_ = false;

BootAnimation::BootAnimation(const lv_font_t* font_large, const lv_font_t* font_small)
    : font_large_(font_large), font_small_(font_small) {
}

BootAnimation::~BootAnimation() {
    Hide();
}

void BootAnimation::Show(const char* logo_text, const char* subtitle_text, const char* version_text, uint32_t duration_ms) {
    // 检查动画是否已经播放过
    if (animation_played_) {
        ESP_LOGI(TAG, "Boot animation already played, skipping");
        return;
    }
    
    // 如果已经存在，先删除旧的
    if (splash_screen_ != nullptr) {
        Hide();
    }
    
    // 创建全屏容器
    splash_screen_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(splash_screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(splash_screen_, lv_color_black(), 0);
    lv_obj_set_style_border_width(splash_screen_, 0, 0);
    lv_obj_set_style_radius(splash_screen_, 0, 0);
    lv_obj_clear_flag(splash_screen_, LV_OBJ_FLAG_SCROLLABLE);
    
    // 创建标题标签
    logo_label_ = lv_label_create(splash_screen_);
    lv_obj_set_style_text_font(logo_label_, font_large_, 0);
    lv_obj_set_style_text_color(logo_label_, lv_color_white(), 0);
    lv_obj_set_style_text_align(logo_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(logo_label_, LV_ALIGN_CENTER, 0, -40);
    
    // 创建副标题标签
    lv_obj_t* subtitle_label = lv_label_create(splash_screen_);
    lv_obj_set_style_text_font(subtitle_label, font_small_, 0);
    lv_obj_set_style_text_color(subtitle_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(subtitle_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(subtitle_label, subtitle_text);
    lv_obj_align(subtitle_label, LV_ALIGN_CENTER, 0, 0);
    
    // 创建版本标签
    version_label_ = lv_label_create(splash_screen_);
    lv_obj_set_style_text_font(version_label_, font_small_, 0);
    lv_obj_set_style_text_color(version_label_, lv_color_white(), 0);
    lv_obj_align(version_label_, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    
    // 创建进度条
    progress_bar_ = lv_bar_create(splash_screen_);
    lv_obj_set_size(progress_bar_, 200, 10);
    lv_obj_align(progress_bar_, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_bar_set_range(progress_bar_, 0, 100);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
    
    // 设置文本
    lv_label_set_text(logo_label_, logo_text);
    lv_label_set_text(version_label_, version_text);
    
    // 显示启动画面
    lv_obj_clear_flag(splash_screen_, LV_OBJ_FLAG_HIDDEN);
    
    // 创建进度条动画
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, progress_bar_);
    lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
        lv_bar_set_value((lv_obj_t*)var, v, LV_ANIM_OFF);
    });
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, duration_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    
    // 设置动画完成回调
    lv_anim_set_user_data(&a, this);
    lv_anim_set_ready_cb(&a, AnimationFinished);
    
    // 启动动画
    lv_anim_start(&a);
    
    // 标记动画已播放
    animation_played_ = true;
    
    ESP_LOGI(TAG, "Boot animation started");
}

void BootAnimation::Hide() {
    if (splash_screen_ != nullptr) {
        lv_obj_del(splash_screen_);
        splash_screen_ = nullptr;
        logo_label_ = nullptr;
        version_label_ = nullptr;
        progress_bar_ = nullptr;
        ESP_LOGI(TAG, "Boot animation hidden");
    }
}

bool BootAnimation::IsVisible() const {
    return splash_screen_ != nullptr;
}

void BootAnimation::AnimationFinished(lv_anim_t* anim) {
    // 延迟500ms后隐藏启动画面
    BootAnimation* self = static_cast<BootAnimation*>(lv_anim_get_user_data(anim));
    if (self && self->splash_screen_) {
        // 使用 lv_timer_create_basic 代替 lv_timer_create
        lv_timer_t* timer = lv_timer_create(HideSplashScreen, 500, self);
        lv_timer_set_repeat_count(timer, 1);  // 只执行一次
    }
}

void BootAnimation::HideSplashScreen(lv_timer_t* timer) {
    // 使用 lv_timer_get_user_data 代替直接访问 user_data
    BootAnimation* self = static_cast<BootAnimation*>(lv_timer_get_user_data(timer));
    if (self) {
        self->Hide();
    }
    lv_timer_del(timer);
} 