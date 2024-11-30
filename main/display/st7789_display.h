#ifndef ST7789_DISPLAY_H
#define ST7789_DISPLAY_H

#include "display.h"
#include <vector>

#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

class St7789Display : public Display {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    gpio_num_t backlight_pin_ = GPIO_NUM_NC;
    bool backlight_output_invert_ = false;
    bool mirror_x_ = false;
    bool mirror_y_ = false;
    bool swap_xy_ = false;
    
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    
    // 眼睛相关对象
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_anim_t blink_anim_;
    bool is_blinking_ = false;
    
    // 眼睛动作类型
    enum EyeAction {
        ACTION_NORMAL,      // 正常状态
        ACTION_BLINK,       // 眨眼
        ACTION_LOOK_LEFT,   // 向左看
        ACTION_LOOK_RIGHT,  // 向右看
        ACTION_LOOK_UP,     // 向上看
        ACTION_LOOK_DOWN,   // 向下看
        ACTION_LOOK_CENTER, // 回到中间
        ACTION_HAPPY,       // 开心
        ACTION_SAD,         // 悲伤
        ACTION_ANGRY,       // 生气
        ACTION_SURPRISED,   // 惊讶
        ACTION_SLEEPY,      // 困倦
        ACTION_DIZZY,       // 眩晕
    };
    
    EyeAction current_action_ = ACTION_NORMAL;
    void SetEyeAction(EyeAction action);
    void UpdateEyeAction();
    
    void InitializeBacklight(gpio_num_t backlight_pin);
    void SetBacklight(uint8_t brightness);
    void SetupUI();
    void SetupEyes();
    
    // 动画回调函数
    static void BlinkAnimCallback(void* obj, int32_t value);
    static void OnBlinkAnimComplete(lv_anim_t* anim);
    
    // 眼睛动作
    void StartBlinking();
    
    // 眼睛参数
    static constexpr int EYE_CORNER_RADIUS = 20;  // 眼睛圆角半径
    static constexpr float EYE_Y_OFFSET = 0.4f;   // 眼睛垂直位置(0.5是中间)
    static constexpr float EYE_SIZE_RATIO = 0.4f; // 眼睛尺寸占屏幕高度的比例
    static constexpr int EYE_SPACING = 20;        // 眼睛之间的固定间距
    
    // 表情缩放比例
    static constexpr float HAPPY_SCALE = 0.5f;    // 开心时眼睛缩放比例
    static constexpr float ANGRY_SCALE = 0.3f;    // 生气时眼睛缩放比例
    static constexpr float SURPRISED_SCALE = 1.3f; // 惊讶时眼睛缩放比例
    static constexpr float SLEEPY_SCALE = 0.4f;   // 困倦时眼睛缩放比例
    
    // 动画参数
    static constexpr float LOOK_OFFSET = 0.3f;    // 视线移动偏移量
    static constexpr float DIZZY_ANGLE = 30.0f;   // 眩晕旋转角度
    static constexpr int DIZZY_TIME = 2000;       // 眩晕动画时长
    
    // 动画相关
    lv_anim_t look_anim_;   // 视线移动动画
    lv_anim_t dizzy_anim_;  // 眩晕动画
    bool is_looking_ = false;
    bool is_dizzy_ = false;
    
    void StartLookAnimation(float x_offset, float y_offset);
    void StartDizzyAnimation();
    static void LookAnimCallback(void* obj, int32_t value);
    static void DizzyAnimCallback(void* obj, int32_t value);
    
    virtual void Lock() override;
    virtual void Unlock() override;
    
    // 视线移动动画数据
    struct LookAnimData {
        int start_x_left, start_x_right, start_y;
        int target_x_left, target_x_right, target_y;
    };
    
    // 动作序列相关
    struct EyeActionStep {
        EyeAction action;    // 动作类型
        uint32_t duration;   // 持续时间(ms)
    };
    
    static constexpr EyeActionStep IDLE_SEQUENCE[] = {
        {ACTION_SLEEPY, 2000},      // 开始时显示困倦2秒
        {ACTION_BLINK, 200},        // 快速眨眼200ms
        {ACTION_NORMAL, 1000},      // 正常状态1秒
        {ACTION_BLINK, 200},        // 再次快速眨眼
        {ACTION_NORMAL, 3000},      // 正常状态3秒
        {ACTION_BLINK, 200},        // 眨眼200ms (改快一点)
        {ACTION_NORMAL, 2000},      // 正常状态2秒
        {ACTION_LOOK_LEFT, 3000},   // 向左看3秒
        {ACTION_LOOK_LEFT, 1500},   // 保持左看1.5秒
        {ACTION_LOOK_CENTER, 1000}, // 平滑回中间1秒
        {ACTION_NORMAL, 1000},      // 保持中间1秒
        {ACTION_LOOK_RIGHT, 3000},  // 向右看3秒
        {ACTION_LOOK_RIGHT, 1500},  // 保持右看1.5秒
        {ACTION_LOOK_CENTER, 1000}, // 平滑回中间1秒
        {ACTION_NORMAL, 1000},      // 保持中间1秒
        {ACTION_BLINK, 200},        // 眨眼200ms
        {ACTION_NORMAL, 2000},      // 正常状态2秒
    };
    static constexpr size_t IDLE_SEQUENCE_SIZE = sizeof(IDLE_SEQUENCE) / sizeof(IDLE_SEQUENCE[0]);
    
    uint32_t current_step_ = 0;  // 当前动作步骤
    bool sequence_running_ = false;
    lv_timer_t* sequence_timer_ = nullptr;
    
    void StartActionSequence();
    void StopActionSequence();
    void NextActionStep();

public:
    St7789Display(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  gpio_num_t backlight_pin, bool backlight_output_invert,
                  int width, int height, bool mirror_x, bool mirror_y, bool swap_xy);
    ~St7789Display();
};

#endif // ST7789_DISPLAY_H
