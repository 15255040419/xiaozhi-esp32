// GIF播放器头文件
#pragma once

#include "display/display.h"
#include "lvgl.h"

// 声明所有 GIF 图像
LV_IMG_DECLARE(lzhch);
LV_IMG_DECLARE(lbuyao);
LV_IMG_DECLARE(ldtlr);
LV_IMG_DECLARE(ldxe);
LV_IMG_DECLARE(lgsx);
LV_IMG_DECLARE(llr);
LV_IMG_DECLARE(lshuizl);
LV_IMG_DECLARE(lwq);
LV_IMG_DECLARE(lyoushang);
LV_IMG_DECLARE(lzuoyou);
LV_IMG_DECLARE(lzy);
LV_IMG_DECLARE(game);
LV_IMG_DECLARE(ting);    // 添加聆听动画
LV_IMG_DECLARE(kaixin);  // 添加说话动画

class GifPlayer {
public:
    GifPlayer(Display* display);
    ~GifPlayer();
    
    bool Initialize();
    void StartLoop();
    void ShowListeningAnimation();
    void ShowSpeakingAnimation();

private:
    static void timer_cb(lv_timer_t* timer);
    static void OnGifEvent(lv_event_t* e);
    void LoadNextAnimation();
    void SwitchToAnimation(lv_obj_t* gif);
    
    Display* display_;
    lv_obj_t* current_gif_;
    lv_obj_t* standby_gif_;    // 待机动画
    lv_obj_t* listening_gif_;  // 聆听动画
    lv_obj_t* speaking_gif_;   // 说话动画
    int current_index_;
    lv_timer_t* timer_;
    
    static const uint32_t ANIMATION_DURATIONS[];
}; 