// GIF播放器头文件
#pragma once

#include "display/display.h"
#include "lvgl.h"

// 声明所有 GIF 图像
LV_IMG_DECLARE(lzhch);
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

// 声明中文字体
LV_FONT_DECLARE(font_puhui_14_1);

class GifPlayer {
public:
    GifPlayer(Display* display);
    ~GifPlayer();
    
    bool Initialize();
    void StartLoop();
    void ShowListeningAnimation();
    void ShowSpeakingAnimation();
    void ShowSpeakingText(const char* text);  // 新增：显示说话文字

private:
    static void timer_cb(lv_timer_t* timer);
    static void OnGifEvent(lv_event_t* e);
    void LoadNextAnimation();
    void SwitchToAnimation(lv_obj_t* gif);
    void UpdateSpeakingText();  // 新增：更新说话文字
    
    Display* display_;
    lv_obj_t* current_gif_;
    lv_obj_t* standby_gif_;    // 待机动画
    lv_obj_t* listening_gif_;  // 聆听动画
    lv_obj_t* speaking_gif_;   // 说话动画
    lv_obj_t* text_label_;     // 新增：文字标签
    lv_timer_t* text_timer_;   // 新增：文字动画定时器
    std::string current_text_;  // 新增：当前显示的文字
    size_t text_pos_;          // 新增：当前显示到的文字位置
    int current_index_;
    lv_timer_t* timer_;
    
    static const uint32_t ANIMATION_DURATIONS[];
    static constexpr uint32_t TEXT_ANIMATION_INTERVAL = 15;  // 文字动画间隔（毫秒）
}; 