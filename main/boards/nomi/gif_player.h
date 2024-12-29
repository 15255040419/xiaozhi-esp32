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

    Display* display_;
    lv_obj_t* current_gif_;
    int current_index_;
    lv_timer_t* timer_;  // 用于定时切换动画
}; 