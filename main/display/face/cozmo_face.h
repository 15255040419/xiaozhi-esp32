#pragma once

#include <lvgl.h>
#include <memory>
#include <string>

class CozmoFace {
public:
    CozmoFace(lv_obj_t* parent);
    ~CozmoFace();

    // 基本表情状态
    void setNeutral();
    void setHappy();
    void setSad();
    void setAngry();
    void setSurprised();
    
    // 眨眼动画控制
    void startBlinkAnimation();
    void stopBlinkAnimation();

    // 显示控制
    void show() {
        if (left_eye_) lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
        if (right_eye_) lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
    }
    
    void hide() {
        if (left_eye_) lv_obj_add_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
        if (right_eye_) lv_obj_add_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
    }

    // 设置眼睛大小和位置
    void setEyeSize(int width, int height) {
        if (left_eye_) lv_obj_set_size(left_eye_, width, height);
        if (right_eye_) lv_obj_set_size(right_eye_, width, height);
    }

    void setEyePositions(int left_x, int right_x, int y) {
        if (left_eye_) lv_obj_set_pos(left_eye_, left_x, y);
        if (right_eye_) lv_obj_set_pos(right_eye_, right_x, y);
    }

private:
    lv_obj_t* parent_;
    lv_obj_t* left_eye_;
    lv_obj_t* right_eye_;
    lv_timer_t* blink_timer_;
    lv_timer_t* emotion_timer_;  // 用于表情过渡动画
    std::string current_emotion_;
    int animation_step_;
    
    void createEyes();
    static void blinkTimerCallback(lv_timer_t* timer);
    static void emotionAnimationCallback(lv_timer_t* timer);
    
    // 动画辅助函数
    void updateEyeShape(int height, int radius, int y_offset = 0, int angle = 0);
    void animateToEmotion(const std::string& target_emotion);
    void setBlink(int step);
};
