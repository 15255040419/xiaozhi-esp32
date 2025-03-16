#pragma once

#include <lvgl.h>
#include <esp_log.h>

class BootAnimation {
private:
    lv_obj_t* splash_screen_ = nullptr;
    lv_obj_t* logo_label_ = nullptr;
    lv_obj_t* version_label_ = nullptr;
    lv_obj_t* progress_bar_ = nullptr;
    const lv_font_t* font_large_ = nullptr;
    const lv_font_t* font_small_ = nullptr;
    
    // 动画完成回调
    static void AnimationFinished(lv_anim_t* anim);
    
    // 隐藏启动画面的定时器回调
    static void HideSplashScreen(lv_timer_t* timer);

public:
    BootAnimation(const lv_font_t* font_large, const lv_font_t* font_small);
    ~BootAnimation();
    
    // 显示启动动画
    void Show(const char* logo_text, const char* version_text, uint32_t duration_ms);
    
    // 隐藏启动动画
    void Hide();
    
    // 检查是否可见
    bool IsVisible() const;
}; 