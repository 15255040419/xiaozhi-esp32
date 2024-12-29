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

private:
    void LoadNextAnimation();

    Display* display_;
    lv_obj_t* current_gif_;
    int current_index_;
}; 