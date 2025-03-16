#ifndef BOOT_ANIMATION_H
#define BOOT_ANIMATION_H

#include <lvgl.h>

class BootAnimation {
public:
    BootAnimation(lv_obj_t* parent);
    ~BootAnimation();

    void Show();
    void Hide();

private:
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* logo_ = nullptr;
    lv_obj_t* progress_bar_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    
    // 添加静态标志，跟踪动画是否已执行过
    static bool animation_played_;
    
    static void TimerCallback(lv_timer_t* timer);
};

#endif // BOOT_ANIMATION_H 