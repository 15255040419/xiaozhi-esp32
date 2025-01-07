#include "gif_player.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
LV_IMG_DECLARE(ting);
LV_IMG_DECLARE(kaixin);

static const char* TAG = "GifPlayer";

// 定义每个动画的播放时长（毫秒），移除 game 动画的时长
const uint32_t GifPlayer::ANIMATION_DURATIONS[] = {
    8000,   // lgsx
    6000,   // lzhch
    6000,   // ldtlr
    6000,   // ldxe
    6000,   // lwq
    6000,   // llr
    6000,   // lzy
    6000,   // lyoushang
    6000,   // lzuoyou
    8000    // lshuizl
};

void GifPlayer::timer_cb(lv_timer_t* timer) {
    GifPlayer* player = static_cast<GifPlayer*>(timer->user_data);
    if (player) {
        player->LoadNextAnimation();
    }
}

GifPlayer::GifPlayer(Display* display) 
    : display_(display)
    , current_gif_(nullptr)
    , standby_gif_(nullptr)
    , listening_gif_(nullptr)
    , speaking_gif_(nullptr)
    , text_label_(nullptr)
    , text_timer_(nullptr)
    , text_pos_(0)
    , current_index_(0)
    , timer_(nullptr) {
}

GifPlayer::~GifPlayer() {
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    if (standby_gif_) {
        lv_obj_del(standby_gif_);
        standby_gif_ = nullptr;
    }
    if (listening_gif_) {
        lv_obj_del(listening_gif_);
        listening_gif_ = nullptr;
    }
    if (speaking_gif_) {
        lv_obj_del(speaking_gif_);
        speaking_gif_ = nullptr;
    }
}

void GifPlayer::OnGifEvent(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    GifPlayer* player = static_cast<GifPlayer*>(lv_event_get_user_data(e));
    
    if (code == LV_EVENT_READY && player) {
        ESP_LOGI(TAG, "GIF ready to play");
    }
}

bool GifPlayer::Initialize() {
    if (!display_) {
        ESP_LOGE(TAG, "Display not initialized");
        return false;
    }
    
    // 创建并初始化所有GIF对象
    standby_gif_ = lv_gif_create(lv_scr_act());
    listening_gif_ = lv_gif_create(lv_scr_act());
    speaking_gif_ = lv_gif_create(lv_scr_act());
    
    if (!standby_gif_ || !listening_gif_ || !speaking_gif_) {
        ESP_LOGE(TAG, "Failed to create GIF objects");
        return false;
    }
    
    // 设置所有GIF的大小和位置
    for (auto gif : {standby_gif_, listening_gif_, speaking_gif_}) {
        lv_obj_set_size(gif, LV_PCT(100), LV_PCT(100));
        lv_obj_center(gif);
        lv_obj_add_event_cb(gif, OnGifEvent, LV_EVENT_READY, this);
        lv_obj_add_flag(gif, LV_OBJ_FLAG_HIDDEN);  // 初始时隐藏所有GIF
    }
    
    // 创建文字标签
    text_label_ = lv_label_create(lv_scr_act());
    if (!text_label_) {
        ESP_LOGE(TAG, "Failed to create text label");
        return false;
    }
    
    // 设置文字标签样式
    static lv_style_t style_text;
    lv_style_init(&style_text);
    lv_style_set_text_font(&style_text, &font_puhui_14_1);
    lv_style_set_text_color(&style_text, lv_color_white());
    lv_style_set_text_align(&style_text, LV_TEXT_ALIGN_CENTER);
    
    lv_obj_add_style(text_label_, &style_text, 0);
    lv_obj_set_width(text_label_, LV_PCT(90));  // 设置标签宽度为屏幕的90%
    lv_obj_set_style_text_align(text_label_, LV_TEXT_ALIGN_CENTER, 0);
    
    // 将标签放在屏幕底部
    lv_obj_align(text_label_, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_flag(text_label_, LV_OBJ_FLAG_HIDDEN);  // 初始时隐藏文字
    
    // 加载GIF源
    lv_gif_set_src(standby_gif_, &lgsx);
    lv_gif_set_src(listening_gif_, &ting);
    lv_gif_set_src(speaking_gif_, &kaixin);
    
    // 设置当前GIF为待机动画
    current_gif_ = standby_gif_;
    lv_obj_clear_flag(current_gif_, LV_OBJ_FLAG_HIDDEN);
    
    // 创建定时器，增加初始间隔
    timer_ = lv_timer_create(timer_cb, 8000, this);  // 增加到8秒
    if (!timer_) {
        ESP_LOGE(TAG, "Failed to create timer");
        return false;
    }
    
    // 降低定时器优先级
    if (timer_) {
        lv_timer_set_period(timer_, 8000);  // 使用较长的周期来降低优先级
    }
    
    return true;
}

void GifPlayer::SwitchToAnimation(lv_obj_t* gif) {
    if (!gif || gif == current_gif_) {
        return;
    }
    
    // 暂停定时器
    if (timer_) {
        lv_timer_pause(timer_);
    }
    
    // 隐藏当前GIF
    if (current_gif_) {
        lv_obj_add_flag(current_gif_, LV_OBJ_FLAG_HIDDEN);
    }
    
    // 显示新GIF
    current_gif_ = gif;
    lv_obj_clear_flag(current_gif_, LV_OBJ_FLAG_HIDDEN);
    
    // 等待一帧渲染完成
    lv_timer_handler();
}

void GifPlayer::ShowSpeakingText(const char* text) {
    if (!text_label_ || !text) {
        return;
    }
    
    // 停止之前的文字动画
    if (text_timer_) {
        lv_timer_del(text_timer_);
        text_timer_ = nullptr;
    }
    
    // 保存新文字
    current_text_ = text;
    text_pos_ = 0;
    
    // 清空标签并显示
    lv_label_set_text(text_label_, "");
    lv_obj_clear_flag(text_label_, LV_OBJ_FLAG_HIDDEN);
    
    // 创建文字动画定时器
    text_timer_ = lv_timer_create([](lv_timer_t* timer) {
        GifPlayer* player = static_cast<GifPlayer*>(timer->user_data);
        player->UpdateSpeakingText();
    }, TEXT_ANIMATION_INTERVAL, this);
}

void GifPlayer::UpdateSpeakingText() {
    if (text_pos_ >= current_text_.length()) {
        // 文字显示完毕，删除定时器
        lv_timer_del(text_timer_);
        text_timer_ = nullptr;
        return;
    }
    
    // 逐字显示文本
    std::string display_text = current_text_.substr(0, text_pos_ + 1);
    lv_label_set_text(text_label_, display_text.c_str());
    text_pos_++;
}

void GifPlayer::StartLoop() {
    ESP_LOGI(TAG, "Starting animation loop");
    SwitchToAnimation(standby_gif_);
    if (timer_) {
        lv_timer_resume(timer_);
    }
    // 隐藏文字标签
    if (text_label_) {
        lv_obj_add_flag(text_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GifPlayer::ShowListeningAnimation() {
    ESP_LOGI(TAG, "Showing listening animation");
    SwitchToAnimation(listening_gif_);
    // 隐藏文字标签
    if (text_label_) {
        lv_obj_add_flag(text_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GifPlayer::ShowSpeakingAnimation() {
    ESP_LOGI(TAG, "Showing speaking animation");
    SwitchToAnimation(speaking_gif_);
}

void GifPlayer::LoadNextAnimation() {
    if (!standby_gif_) {
        return;
    }
    
    ESP_LOGI(TAG, "Loading animation %d", current_index_);
    
    // 只在待机状态时循环切换动画
    if (current_gif_ == standby_gif_) {
        // 清理之前的 GIF 内存
        lv_gif_set_src(standby_gif_, nullptr);
        
        // 强制进行垃圾回收
        lv_mem_buf_free_all();
        
        // 等待多个帧渲染完成
        for(int i = 0; i < 3; i++) {
            lv_timer_handler();
            vTaskDelay(10);  // 直接使用 tick 数
        }
        
        // 循环切换动画
        switch (current_index_) {
            case 0:
                lv_gif_set_src(standby_gif_, &lgsx);
                break;
            case 1:
                lv_gif_set_src(standby_gif_, &lzhch);
                break;
            case 2:
                lv_gif_set_src(standby_gif_, &ldtlr);
                break;
            case 3:
                lv_gif_set_src(standby_gif_, &ldxe);
                break;
            case 4:
                lv_gif_set_src(standby_gif_, &lwq);
                break;
            case 5:
                lv_gif_set_src(standby_gif_, &llr);
                break;
            case 6:
                lv_gif_set_src(standby_gif_, &lzy);
                break;
            case 7:
                lv_gif_set_src(standby_gif_, &lyoushang);
                break;
            case 8:
                lv_gif_set_src(standby_gif_, &lzuoyou);
                break;
            case 9:
                lv_gif_set_src(standby_gif_, &lshuizl);
                break;
            default:
                current_index_ = 0;
                lv_gif_set_src(standby_gif_, &lgsx);
                break;
        }
        
        // 等待多个帧渲染完成
        for(int i = 0; i < 3; i++) {
            lv_timer_handler();
            vTaskDelay(10);  // 直接使用 tick 数
        }
        
        // 更新定时器的周期
        if (timer_) {
            lv_timer_set_period(timer_, ANIMATION_DURATIONS[current_index_]);
        }
        
        current_index_ = (current_index_ + 1) % 10;
    }
} 