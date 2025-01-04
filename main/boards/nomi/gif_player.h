// GIF播放器头文件
#pragma once

#include "display/display.h"
#include "lvgl.h"

class GifPlayer {
public:
    explicit GifPlayer(Display* display);
    ~GifPlayer();

    bool Initialize();
    void StartLoop();
    void LoadNextAnimation();

private:
    static void OnGifEvent(lv_event_t* e);
    static void timer_cb(lv_timer_t* timer);

    Display* display_;
    lv_obj_t* current_gif_;
    int current_index_;
    lv_timer_t* timer_;

    // 定义每个动画的播放时长（毫秒）
    static const uint32_t ANIMATION_DURATIONS[];
}; 