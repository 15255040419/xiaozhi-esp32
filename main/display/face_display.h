#pragma once

#include "lvgl.h"
#include <esp_err.h>

#define TRANSITION_TIME_MS 200
#define BLINK_TIME_MS 100

// 表情状态枚举
enum class Expression {
    Normal,
    Happy,
    Sad,
    Angry,
    Surprised,
    Sleepy,
    Blink,
    Glee,
    Worried,
    Focused,
    Annoyed,
    Skeptic,
    Frustrated,
    Unimpressed,
    Suspicious,
    Squint,
    Furious,
    Scared,
    Awe,
    LookLeft,   // 添加向左看
    LookRight   // 添加向右看
};

enum class CornerType { 
    T_L,  // Top Left
    T_R,  // Top Right
    B_L,  // Bottom Left
    B_R   // Bottom Right
};

// 眼睛配置结构体，与原项目完全一致
struct EyeConfig {
    int32_t OffsetX;
    int32_t OffsetY;
    int32_t Height;
    int32_t Width;
    float Slope_Top;
    float Slope_Bottom;
    int32_t Radius_Top;
    int32_t Radius_Bottom;
    int32_t Inverse_Radius_Top;
    int32_t Inverse_Radius_Bottom;
    int32_t Inverse_Offset_Top;
    int32_t Inverse_Offset_Bottom;
};

class FaceDisplay {
public:
    FaceDisplay(lv_obj_t* parent, int width, int height);
    ~FaceDisplay();

    // 设置表情
    void setExpression(Expression exp);
    // 眨眼动画
    void doBlink();
    // 更新显示
    void update();

private:
    lv_obj_t* parent_;
    lv_obj_t* left_eye_canvas_;
    lv_obj_t* right_eye_canvas_;
    
    int width_;
    int height_;
    Expression current_expression_;
    Expression target_expression_;
    bool is_blinking_;
    bool is_transitioning_;
    
    EyeConfig current_config_;
    EyeConfig target_config_;
    uint32_t transition_start_;
    uint32_t transition_duration_;
    
    void createEyes();
    void drawEye(lv_obj_t* canvas, const EyeConfig& config, bool is_left);
    void fillRectangle(lv_draw_ctx_t& draw_ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void fillRectangularTriangle(lv_draw_ctx_t& draw_ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void fillEllipseCorner(lv_draw_ctx_t& draw_ctx, CornerType corner, int16_t x0, int16_t y0, int32_t rx, int32_t ry);
    EyeConfig getExpressionConfig(Expression exp);
    void startTransition(Expression exp);
    void updateTransition();
};
