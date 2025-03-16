#include "boot_animation.h"
#include <esp_log.h>

#define TAG "BootAnimation"

// 初始化静态标志
bool BootAnimation::animation_played_ = false;

BootAnimation::BootAnimation(lv_obj_t* parent) : parent_(parent) {
    // 创建容器
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    
    // 创建logo (对于OLED可能需要调整)
    logo_ = lv_img_create(container_);
    // 设置logo图片，这里假设您有一个logo图片
    // lv_img_set_src(logo_, &your_logo);
    lv_obj_align(logo_, LV_ALIGN_CENTER, 0, -10);
    
    // 创建进度条
    progress_bar_ = lv_bar_create(container_);
    lv_obj_set_size(progress_bar_, lv_pct(60), 8);
    lv_obj_align(progress_bar_, LV_ALIGN_CENTER, 0, 20);
    lv_bar_set_range(progress_bar_, 0, 100);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
    
    // 默认隐藏容器
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    
    timer_ = nullptr;
}

BootAnimation::~BootAnimation() {
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    
    if (container_) {
        lv_obj_del(container_);
        container_ = nullptr;
    }
}

void BootAnimation::Show() {
    // 检查动画是否已经播放过
    if (animation_played_) {
        ESP_LOGI(TAG, "Boot animation already played, skipping");
        return;
    }
    
    ESP_LOGI(TAG, "Showing boot animation");
    
    // 显示容器
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    
    // 创建定时器，更新进度条
    if (timer_ == nullptr) {
        timer_ = lv_timer_create(TimerCallback, 10, this);
    }
    
    // 标记动画已播放
    animation_played_ = true;
}

void BootAnimation::Hide() {
    ESP_LOGI(TAG, "Hiding boot animation");
    
    // 删除定时器
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    
    // 隐藏容器
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }
}

void BootAnimation::TimerCallback(lv_timer_t* timer) {
    BootAnimation* animation = static_cast<BootAnimation*>(lv_timer_get_user_data(timer));
    if (!animation || !animation->progress_bar_) {
        return;
    }
    
    // 获取当前进度
    int value = lv_bar_get_value(animation->progress_bar_);
    
    // 更新进度
    value += 20;
    if (value > 100) {
        // 进度达到100%，隐藏动画
        animation->Hide();
        return;
    }
    
    // 设置新进度
    lv_bar_set_value(animation->progress_bar_, value, LV_ANIM_ON);
} 