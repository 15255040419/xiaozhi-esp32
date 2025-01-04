#include "gif_player.h"
#include <esp_log.h>

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
LV_IMG_DECLARE(ting);
LV_IMG_DECLARE(kaixin);

static const char* TAG = "GifPlayer";

// 定义每个动画的播放时长（毫秒）
const uint32_t GifPlayer::ANIMATION_DURATIONS[] = {
    5000,   // lgsx
    3000,   // lzhch
    4000,   // ldtlr
    3000,   // ldxe
    3000,   // lwq
    4000,   // llr
    3000,   // lzy
    3000,   // lbuyao
    4000,   // lyoushang
    3000,   // lzuoyou
    5000,   // game
    4000    // lshuizl
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
    
    // 加载GIF源
    lv_gif_set_src(standby_gif_, &lgsx);
    lv_gif_set_src(listening_gif_, &ting);
    lv_gif_set_src(speaking_gif_, &kaixin);
    
    // 设置当前GIF为待机动画
    current_gif_ = standby_gif_;
    lv_obj_clear_flag(current_gif_, LV_OBJ_FLAG_HIDDEN);
    
    // 创建定时器
    timer_ = lv_timer_create(timer_cb, ANIMATION_DURATIONS[0], this);
    if (!timer_) {
        ESP_LOGE(TAG, "Failed to create timer");
        return false;
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

void GifPlayer::ShowListeningAnimation() {
    ESP_LOGI(TAG, "Showing listening animation");
    SwitchToAnimation(listening_gif_);
}

void GifPlayer::ShowSpeakingAnimation() {
    ESP_LOGI(TAG, "Showing speaking animation");
    SwitchToAnimation(speaking_gif_);
}

void GifPlayer::StartLoop() {
    ESP_LOGI(TAG, "Starting animation loop");
    SwitchToAnimation(standby_gif_);
    if (timer_) {
        lv_timer_resume(timer_);
    }
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
        
        // 等待一帧渲染完成
        lv_timer_handler();
        
        // 循环切换动画
        switch (current_index_) {
            case 0:
                lv_gif_set_src(standby_gif_, &lgsx);  // 待机
                break;
            case 1:
                lv_gif_set_src(standby_gif_, &lzhch); // 开心
                break;
            case 2:
                lv_gif_set_src(standby_gif_, &ldtlr); // 点头
                break;
            case 3:
                lv_gif_set_src(standby_gif_, &ldxe);  // 点下额
                break;
            case 4:
                lv_gif_set_src(standby_gif_, &lwq);   // 微笑
                break;
            case 5:
                lv_gif_set_src(standby_gif_, &llr);   // 左右
                break;
            case 6:
                lv_gif_set_src(standby_gif_, &lzy);   // 左眼
                break;
            case 7:
                lv_gif_set_src(standby_gif_, &lbuyao); // 不要
                break;
            case 8:
                lv_gif_set_src(standby_gif_, &lyoushang); // 右上
                break;
            case 9:
                lv_gif_set_src(standby_gif_, &lzuoyou);   // 左右
                break;
            case 10:
                lv_gif_set_src(standby_gif_, &game);      // 游戏
                break;
            case 11:
                lv_gif_set_src(standby_gif_, &lshuizl);   // 睡着了
                break;
            default:
                current_index_ = 0;
                lv_gif_set_src(standby_gif_, &lgsx);
                break;
        }
        
        // 等待一帧渲染完成
        lv_timer_handler();
        
        // 更新定时器的周期为当前动画的时长
        if (timer_) {
            lv_timer_set_period(timer_, ANIMATION_DURATIONS[current_index_]);
        }
        
        current_index_ = (current_index_ + 1) % 12;  // 总共12个动画
    }
} 