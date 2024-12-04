#include "eyes_animation.h"
#include <esp_log.h>
#include <math.h>
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"

#define TAG "EyesAnimation"
#define PI 3.14159265358979323846f

// 屏幕刷新率
#define ANIMATION_FPS 30
#define ANIMATION_INTERVAL (1000 / ANIMATION_FPS)

// 眼睛参数
#define EYE_SPACING 100  // 两眼间距
#define EYE_SIZE 40     // 眼睛基础大小
#define BLINK_SPEED 0.2f // 眨眼速度
#define EXPRESSION_SPEED 0.1f // 表情变化速度
#define EMOTION_DURATION 3000 // 每个表情持续时间(毫秒)

#define ANIMATION_TASK_STACK_SIZE 8192  // 增加堆栈大小
#define ANIMATION_TASK_PRIORITY 3    // 降低优先级

#define BLOCK_HEIGHT 32  // 每次绘制的行数

EyesAnimation::EyesAnimation(esp_lcd_panel_handle_t panel, int width, int height)
    : panel_(panel), width_(width), height_(height) {
    
    // 参数检查
    if (width <= 0 || height <= 0 || !panel) {
        ESP_LOGE(TAG, "Invalid parameters: width=%d, height=%d, panel=%p", width, height, panel);
        width_ = 0;
        height_ = 0;
        panel_ = nullptr;
        return;
    }
    
    // 分配帧缓冲区 - 使用PSRAM
    frame_buffer_ = (uint16_t*)heap_caps_malloc(width_ * BLOCK_HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!frame_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer");
        return;
    }
    
    // 初始化左眼位置
    left_eye_.x = width_ / 2 - EYE_SPACING / 2;
    left_eye_.y = height_ / 2;
    left_eye_.size = EYE_SIZE;
    left_eye_.angle = 0;
    left_eye_.blink = 0;
    left_eye_.expression = 0;

    // 初始化右眼位置
    right_eye_.x = width_ / 2 + EYE_SPACING / 2;
    right_eye_.y = height_ / 2;
    right_eye_.size = EYE_SIZE;
    right_eye_.angle = 0;
    right_eye_.blink = 0;
    right_eye_.expression = 0;
}

EyesAnimation::~EyesAnimation() {
    Stop();
    if (frame_buffer_) {
        heap_caps_free(frame_buffer_);
        frame_buffer_ = nullptr;
    }
}

void EyesAnimation::Start() {
    if (!running_) {
        running_ = true;
        xTaskCreate(AnimationTask, "eyes_anim", ANIMATION_TASK_STACK_SIZE, 
                   this, ANIMATION_TASK_PRIORITY, &animation_task_);
    }
}

void EyesAnimation::Stop() {
    if (running_) {
        running_ = false;
        if (animation_task_) {
            vTaskDelete(animation_task_);
            animation_task_ = nullptr;
        }
    }
}

void EyesAnimation::SetState(State state) {
    current_state_ = state;
}

void EyesAnimation::AnimationTask(void* arg) {
    EyesAnimation* self = static_cast<EyesAnimation*>(arg);
    TickType_t last_wake_time = xTaskGetTickCount();
    TickType_t emotion_change_time = xTaskGetTickCount();
    
    // 添加任务到看门狗
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    
    // 表情序列
    const State emotion_sequence[] = {
        State::Normal,
        State::Happy,
        State::Sad,
        State::Angry,
        State::Sleepy,
    };
    const int emotion_count = sizeof(emotion_sequence) / sizeof(emotion_sequence[0]);
    int current_emotion_index = 0;
    
    ESP_LOGI(TAG, "Starting animation sequence");
    
    while (self->running_) {
        // 喂狗
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        
        // 检查是否需要切换表情
        if (xTaskGetTickCount() - emotion_change_time >= pdMS_TO_TICKS(EMOTION_DURATION)) {
            current_emotion_index = (current_emotion_index + 1) % emotion_count;
            self->SetState(emotion_sequence[current_emotion_index]);
            emotion_change_time = xTaskGetTickCount();
            
            const char* emotion_names[] = {"Normal", "Happy", "Sad", "Angry", "Sleepy", "Blink"};
            ESP_LOGI(TAG, "Switching to emotion: %s", emotion_names[static_cast<int>(emotion_sequence[current_emotion_index])]);
        }
        
        self->UpdateAnimation();
        self->DrawEyes();
        
        // 确保帧率
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(ANIMATION_INTERVAL));
    }
    
    // 从看门狗中移除任务
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
    vTaskDelete(nullptr);
}

void EyesAnimation::UpdateAnimation() {
    static float blink_timer = 0;
    static float expression_timer = 0;
    
    // 更新眨眼动画
    blink_timer += BLINK_SPEED;
    if (blink_timer >= 2 * PI) {
        blink_timer = 0;
    }
    
    float blink = (sinf(blink_timer) + 1) / 2;  // 0-1范围
    left_eye_.blink = blink;
    right_eye_.blink = blink;
    
    // 更新表情动画
    expression_timer += EXPRESSION_SPEED;
    if (expression_timer >= 2 * PI) {
        expression_timer = 0;
    }
    
    // 根据当前状态更新表情
    switch (current_state_) {
        case State::Happy:
            left_eye_.angle = -0.2f;
            right_eye_.angle = 0.2f;
            left_eye_.expression = 0.8f;
            right_eye_.expression = 0.8f;
            break;
            
        case State::Sad:
            left_eye_.angle = 0.2f;
            right_eye_.angle = -0.2f;
            left_eye_.expression = 0.3f;
            right_eye_.expression = 0.3f;
            break;
            
        case State::Angry:
            left_eye_.angle = 0.4f;
            right_eye_.angle = -0.4f;
            left_eye_.expression = 0.9f;
            right_eye_.expression = 0.9f;
            break;
            
        case State::Sleepy:
            left_eye_.angle = 0;
            right_eye_.angle = 0;
            left_eye_.expression = 0.5f + sinf(expression_timer) * 0.2f;
            right_eye_.expression = 0.5f + sinf(expression_timer) * 0.2f;
            break;
            
        default:  // Normal
            left_eye_.angle = 0;
            right_eye_.angle = 0;
            left_eye_.expression = 0.5f + sinf(expression_timer) * 0.1f;
            right_eye_.expression = 0.5f + sinf(expression_timer) * 0.1f;
            break;
    }
}

void EyesAnimation::DrawEyes() {
    if (!frame_buffer_) {
        ESP_LOGE(TAG, "Frame buffer not allocated");
        return;
    }
    
    // 分块绘制整个屏幕
    for (int y = 0; y < height_; y += BLOCK_HEIGHT) {
        // 计算当前块的实际高度
        int current_block_height = std::min(BLOCK_HEIGHT, height_ - y);
        
        // 清空缓冲区(黑色背景)
        for (int i = 0; i < width_ * current_block_height; i++) {
            frame_buffer_[i] = 0;
        }
        
        // 绘制眼睛的这一部分
        DrawEye(left_eye_, frame_buffer_, y, current_block_height);
        DrawEye(right_eye_, frame_buffer_, y, current_block_height);
        
        // 更新显示的这一块区域
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, current_block_height, frame_buffer_);
    }
}

void EyesAnimation::DrawEye(const Eye& eye, uint16_t* buffer, int start_y, int block_height) {
    // 使用16.16定点数计算
    const int FIXED_POINT_SHIFT = 16;
    const int FIXED_POINT_ONE = 1 << FIXED_POINT_SHIFT;
    
    int actual_size = (int)(eye.size * (1.0f - eye.blink * 0.8f) * FIXED_POINT_ONE);
    int expr_factor = (int)(eye.expression * 0.5f * FIXED_POINT_ONE);
    
    // 预计算三角函数
    int cos_angle = (int)(cosf(eye.angle) * FIXED_POINT_ONE);
    int sin_angle = (int)(sinf(eye.angle) * FIXED_POINT_ONE);
    
    // 计算边界框以减少循环范围
    int box_size = (actual_size >> FIXED_POINT_SHIFT) + 1;
    int start_x = (int)eye.x - box_size;
    int end_x = (int)eye.x + box_size;
    int end_y = start_y + block_height;
    
    // 限制在屏幕和当前块范围内
    start_x = start_x < 0 ? 0 : start_x;
    end_x = end_x >= width_ ? width_ - 1 : end_x;
    
    // 只处理当前块内的像素
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x <= end_x; x++) {
            int dx = (x - (int)eye.x) << FIXED_POINT_SHIFT;
            int dy = (y - (int)eye.y) << FIXED_POINT_SHIFT;
            
            // 应用旋转（使用定点数乘法）
            int rx = ((dx * cos_angle - dy * sin_angle) >> FIXED_POINT_SHIFT);
            int ry = ((dx * sin_angle + dy * cos_angle) >> FIXED_POINT_SHIFT);
            
            // 计算椭圆方程（使用定点数）
            int ex = (rx << FIXED_POINT_SHIFT) / actual_size;
            int ey = (ry << FIXED_POINT_SHIFT) / (actual_size + ((actual_size * expr_factor) >> FIXED_POINT_SHIFT));
            
            // 使用定点数比较
            if (((int64_t)ex * ex + (int64_t)ey * ey) <= (FIXED_POINT_ONE * FIXED_POINT_ONE)) {
                buffer[(y - start_y) * width_ + x] = 0xFFFF;  // 白色
            }
        }
    }
}

void EyesAnimation::FillBuffer(uint16_t* buffer) {
    for (int i = 0; i < width_ * height_; i++) {
        buffer[i] = 0;  // 黑色
    }
}

void EyesAnimation::InterpolateEye(Eye& eye, const Eye& target, float t) {
    eye.angle = eye.angle * (1 - t) + target.angle * t;
    eye.expression = eye.expression * (1 - t) + target.expression * t;
}

uint16_t EyesAnimation::RGB565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}