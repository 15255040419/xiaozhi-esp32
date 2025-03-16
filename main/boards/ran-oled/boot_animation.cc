#include "boot_animation.h"
#include <cstdio>  // 添加这一行以使用 snprintf
#include "lvgl.h"  // 确保包含完整的 LVGL 头文件
#include "config.h"  // 添加这行以访问 DISPLAY_MIRROR_X 和 DISPLAY_MIRROR_Y

#define TAG "BootAnimation"

BootAnimation::BootAnimation(const lv_font_t* font_large, const lv_font_t* font_small)
    : splash_screen_(nullptr), logo_label_(nullptr), version_label_(nullptr), 
      progress_bar_(nullptr), font_large_(font_large), font_small_(font_small) {
}

BootAnimation::~BootAnimation() {
    Hide();
}

void BootAnimation::Show(const char* logo_text, const char* version_text, uint32_t duration_ms) {
    // 如果已经存在，先删除旧的
    if (splash_screen_ != nullptr) {
        Hide();
    }
    
    // 创建全屏容器
    splash_screen_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(splash_screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(splash_screen_, 0, 0);
    lv_obj_set_style_radius(splash_screen_, 0, 0);
    lv_obj_set_style_pad_all(splash_screen_, 0, 0);
    lv_obj_clear_flag(splash_screen_, LV_OBJ_FLAG_SCROLLABLE);
    
    // 创建版本标签 - 放在顶部状态栏位置，整体向上偏移2像素
    version_label_ = lv_label_create(splash_screen_);
    lv_obj_set_style_text_font(version_label_, font_small_, 0);
    lv_label_set_text(version_label_, version_text);
    lv_obj_align(version_label_, LV_ALIGN_TOP_MID, 0, 2);  // 向上偏移到2
    
    // 创建标题标签 - 放在中间，整体向上偏移2像素
    logo_label_ = lv_label_create(splash_screen_);
    lv_obj_set_style_text_font(logo_label_, font_small_, 0);
    lv_obj_set_style_text_align(logo_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(logo_label_, logo_text);
    lv_obj_align(logo_label_, LV_ALIGN_CENTER, 0, -2);  // 向上偏移2
    
    // 创建进度条 - 放在底部，整体向上偏移2像素
    progress_bar_ = lv_bar_create(splash_screen_);
    lv_obj_set_size(progress_bar_, 80, 6);
    lv_obj_align(progress_bar_, LV_ALIGN_BOTTOM_MID, 0, -10);  // 向上偏移到-10
    lv_obj_set_style_radius(progress_bar_, 3, 0);
    
    // 设置进度条样式 - 改为实心样式，但使用黑色填充
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_COVER, LV_PART_MAIN);  // 主体背景不透明
    lv_obj_set_style_bg_color(progress_bar_, lv_color_black(), LV_PART_MAIN);  // 主体背景黑色
    lv_obj_set_style_border_width(progress_bar_, 1, LV_PART_MAIN);  // 保留边框
    lv_obj_set_style_border_color(progress_bar_, lv_color_white(), LV_PART_MAIN);  // 边框白色
    
    // 设置指示器样式 - 实心填充，使用白色
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progress_bar_, lv_color_white(), LV_PART_INDICATOR);  // 指示器白色
    
    // 镜像进度条 - 设置范围为100到0，初始值为100
    lv_bar_set_range(progress_bar_, 100, 0);
    lv_bar_set_value(progress_bar_, 100, LV_ANIM_OFF);
    
    // 创建进度条动画
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, progress_bar_);
    lv_anim_set_time(&anim, duration_ms);
    
    // 设置进度条更新回调
    lv_anim_set_exec_cb(&anim, [](void* var, int32_t value) {
        // 更新进度条
        lv_bar_set_value((lv_obj_t*)var, value, LV_ANIM_OFF);
    });
    
    // 设置动画值从100到0（从右到左）
    lv_anim_set_values(&anim, 100, 0);
    
    // 设置动画完成回调
    lv_anim_set_user_data(&anim, this);
    lv_anim_set_ready_cb(&anim, [](lv_anim_t* a) {
        BootAnimation* self = (BootAnimation*)lv_anim_get_user_data(a);
        
        // 延迟500ms后安全地隐藏启动画面
        lv_timer_t* timer = lv_timer_create([](lv_timer_t* t) {
            BootAnimation* self = (BootAnimation*)lv_timer_get_user_data(t);
            if (self && self->IsVisible()) {
                self->Hide();
            }
            lv_timer_del(t);
        }, 500, self);
        lv_timer_set_repeat_count(timer, 1);
        
        ESP_LOGI(TAG, "Animation complete, will hide in 500ms");
    });
    
    lv_anim_start(&anim);
    
    ESP_LOGI(TAG, "Boot animation shown");
}

// 安全地隐藏启动画面
void BootAnimation::Hide() {
    if (splash_screen_ != nullptr) {
        // 使用安全的方式删除对象
        lv_obj_t* to_delete = splash_screen_;
        splash_screen_ = nullptr;
        logo_label_ = nullptr;
        version_label_ = nullptr;
        progress_bar_ = nullptr;
        
        // 使用 LVGL 的安全删除方法
        lv_obj_del_async(to_delete);
        
        ESP_LOGI(TAG, "Boot animation hidden");
    }
}

bool BootAnimation::IsVisible() const {
    return splash_screen_ != nullptr;
} 