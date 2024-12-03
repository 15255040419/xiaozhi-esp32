#include "cozmo_face.h"
#include "esp_log.h"

static const char* TAG = "CozmoFace";

// 眼睛的基本参数
#define LEFT_EYE_X 60
#define RIGHT_EYE_X 200
#define EYE_Y 100
#define EYE_WIDTH 60
#define EYE_HEIGHT 40

// 动画参数
#define BLINK_STEPS 8  // 眨眼动画的步骤数
#define BLINK_STEP_TIME 30  // 每个步骤的时间(ms)
#define BLINK_INTERVAL 3000  // 两次眨眼之间的间隔(ms)
#define EMOTION_STEPS 10  // 表情变化的步骤数
#define EMOTION_STEP_TIME 50  // 表情变化每步时间(ms)

CozmoFace::CozmoFace(lv_obj_t* parent) 
    : parent_(parent), blink_timer_(nullptr), emotion_timer_(nullptr), 
      current_emotion_("neutral"), animation_step_(0) {
    createEyes();
    startBlinkAnimation();
}

CozmoFace::~CozmoFace() {
    if (blink_timer_) {
        lv_timer_del(blink_timer_);
    }
    if (emotion_timer_) {
        lv_timer_del(emotion_timer_);
    }
}

void CozmoFace::createEyes() {
    // 创建左眼
    left_eye_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(left_eye_);
    lv_obj_set_size(left_eye_, EYE_WIDTH, EYE_HEIGHT);
    lv_obj_set_pos(left_eye_, LEFT_EYE_X, EYE_Y);
    lv_obj_set_style_radius(left_eye_, 5, 0);
    lv_obj_set_style_bg_color(left_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(left_eye_, LV_OPA_COVER, 0);
    
    // 创建右眼
    right_eye_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(right_eye_);
    lv_obj_set_size(right_eye_, EYE_WIDTH, EYE_HEIGHT);
    lv_obj_set_pos(right_eye_, RIGHT_EYE_X, EYE_Y);
    lv_obj_set_style_radius(right_eye_, 5, 0);
    lv_obj_set_style_bg_color(right_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(right_eye_, LV_OPA_COVER, 0);

    setNeutral();
}

void CozmoFace::updateEyeShape(int height, int radius, int y_offset, int angle) {
    // 更新左眼
    lv_obj_set_height(left_eye_, height);
    lv_obj_set_style_radius(left_eye_, radius, 0);
    lv_obj_set_y(left_eye_, EYE_Y + y_offset);
    lv_obj_set_style_transform_angle(left_eye_, angle, 0);
    
    // 更新右眼
    lv_obj_set_height(right_eye_, height);
    lv_obj_set_style_radius(right_eye_, radius, 0);
    lv_obj_set_y(right_eye_, EYE_Y + y_offset);
    lv_obj_set_style_transform_angle(right_eye_, -angle, 0);  // 注意右眼角度相反
}

void CozmoFace::setBlink(int step) {
    float progress;
    if (step < BLINK_STEPS / 2) {
        // 闭眼阶段
        progress = 1.0f - (float)step / (BLINK_STEPS / 2);
    } else {
        // 睁眼阶段
        progress = (float)(step - BLINK_STEPS / 2) / (BLINK_STEPS / 2);
    }
    
    int height = 5 + (EYE_HEIGHT - 5) * progress;
    int radius = 2 + 3 * progress;
    updateEyeShape(height, radius, 0, 0);
}

void CozmoFace::setNeutral() {
    if (current_emotion_ != "neutral") {
        animateToEmotion("neutral");
    } else {
        updateEyeShape(EYE_HEIGHT, 5, 0, 0);
    }
}

void CozmoFace::setHappy() {
    if (current_emotion_ != "happy") {
        animateToEmotion("happy");
    } else {
        updateEyeShape(EYE_HEIGHT/2, 15, -10, 0);
    }
}

void CozmoFace::setSad() {
    if (current_emotion_ != "sad") {
        animateToEmotion("sad");
    } else {
        updateEyeShape(EYE_HEIGHT/2, 5, 10, 0);
    }
}

void CozmoFace::setAngry() {
    if (current_emotion_ != "angry") {
        animateToEmotion("angry");
    } else {
        updateEyeShape(EYE_HEIGHT/3, 0, 0, 300);
    }
}

void CozmoFace::setSurprised() {
    if (current_emotion_ != "surprised") {
        animateToEmotion("surprised");
    } else {
        updateEyeShape(EYE_HEIGHT * 1.5, 25, 0, 0);
    }
}

void CozmoFace::animateToEmotion(const std::string& target_emotion) {
    // 如果已经在进行动画，先停止
    if (emotion_timer_) {
        lv_timer_del(emotion_timer_);
        emotion_timer_ = nullptr;
    }
    
    // 设置目标表情
    current_emotion_ = target_emotion;
    animation_step_ = 0;
    
    // 创建新的动画定时器
    emotion_timer_ = lv_timer_create(emotionAnimationCallback, EMOTION_STEP_TIME, this);
}

void CozmoFace::emotionAnimationCallback(lv_timer_t* timer) {
    CozmoFace* face = static_cast<CozmoFace*>(timer->user_data);
    
    // 计算动画进度 (0.0 到 1.0)
    float progress = (float)face->animation_step_ / EMOTION_STEPS;
    
    // 根据目标表情设置相应的参数
    int height, radius, y_offset, angle;
    if (face->current_emotion_ == "happy") {
        height = EYE_HEIGHT - (EYE_HEIGHT/2 * progress);
        radius = 5 + 10 * progress;
        y_offset = -10 * progress;
        angle = 0;
    } else if (face->current_emotion_ == "sad") {
        height = EYE_HEIGHT - (EYE_HEIGHT/2 * progress);
        radius = 5;
        y_offset = 10 * progress;
        angle = 0;
    } else if (face->current_emotion_ == "angry") {
        height = EYE_HEIGHT - (2*EYE_HEIGHT/3 * progress);
        radius = 5 - 5 * progress;
        y_offset = 0;
        angle = 300 * progress;
    } else if (face->current_emotion_ == "surprised") {
        height = EYE_HEIGHT + (EYE_HEIGHT/2 * progress);
        radius = 5 + 20 * progress;
        y_offset = 0;
        angle = 0;
    } else { // neutral
        height = EYE_HEIGHT;
        radius = 5;
        y_offset = 0;
        angle = 0;
    }
    
    face->updateEyeShape(height, radius, y_offset, angle);
    
    face->animation_step_++;
    if (face->animation_step_ >= EMOTION_STEPS) {
        // 动画完成，删除定时器
        lv_timer_del(timer);
        face->emotion_timer_ = nullptr;
    }
}

void CozmoFace::blinkTimerCallback(lv_timer_t* timer) {
    CozmoFace* face = static_cast<CozmoFace*>(timer->user_data);
    static int blink_step = 0;
    static bool is_blinking = false;
    
    if (!is_blinking) {
        is_blinking = true;
        blink_step = 0;
        lv_timer_set_period(timer, BLINK_STEP_TIME);
    }
    
    face->setBlink(blink_step);
    blink_step++;
    
    if (blink_step >= BLINK_STEPS) {
        is_blinking = false;
        blink_step = 0;
        lv_timer_set_period(timer, BLINK_INTERVAL);
        
        // 恢复当前表情
        if (face->current_emotion_ == "happy") face->setHappy();
        else if (face->current_emotion_ == "sad") face->setSad();
        else if (face->current_emotion_ == "angry") face->setAngry();
        else if (face->current_emotion_ == "surprised") face->setSurprised();
        else face->setNeutral();
    }
}

void CozmoFace::startBlinkAnimation() {
    if (!blink_timer_) {
        blink_timer_ = lv_timer_create(blinkTimerCallback, BLINK_INTERVAL, this);
    }
}

void CozmoFace::stopBlinkAnimation() {
    if (blink_timer_) {
        lv_timer_del(blink_timer_);
        blink_timer_ = nullptr;
    }
}
