#ifndef EYES_ANIMATION_H
#define EYES_ANIMATION_H

#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <memory>

class EyesAnimation {
public:
    EyesAnimation(esp_lcd_panel_handle_t panel, int width, int height);
    ~EyesAnimation();

    void Start();
    void Stop();
    
    // 眼睛状态
    enum class State {
        Normal,     // 正常
        Happy,      // 开心
        Sad,       // 悲伤
        Angry,     // 生气
        Sleepy,    // 困倦
        Blink      // 眨眼
    };

    void SetState(State state);
    
protected:
    // 动画参数
    struct Eye {
        float x;           // 眼睛中心x坐标
        float y;           // 眼睛中心y坐标
        float size;        // 眼睛大小
        float angle;       // 眼睛角度
        float blink;       // 眨眼程度(0-1)
        float expression;  // 表情程度(0-1)
    };
    
private:
    esp_lcd_panel_handle_t panel_;
    int width_;
    int height_;
    bool running_ = false;
    State current_state_ = State::Normal;
    TaskHandle_t animation_task_ = nullptr;
    
    Eye left_eye_;
    Eye right_eye_;
    
    // 帧缓冲区
    uint16_t* frame_buffer_ = nullptr;
    static const int BLOCK_HEIGHT = 32;  // 每次绘制的块高度

    // 动画控制函数
    static void AnimationTask(void* arg);
    void UpdateAnimation();
    void DrawEyes();
    void DrawEye(const Eye& eye, uint16_t* buffer, int start_y, int block_height);
    void FillBuffer(uint16_t* buffer);
    
    // 辅助函数
    void InterpolateEye(Eye& eye, const Eye& target, float t);
    uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b);
};

#endif // EYES_ANIMATION_H 