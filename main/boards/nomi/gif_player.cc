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

static const char* TAG = "GifPlayer";

static void timer_cb(lv_timer_t* timer) {
    GifPlayer* player = static_cast<GifPlayer*>(timer->user_data);
    if (player) {
        player->LoadNextAnimation();
    }
}

GifPlayer::GifPlayer(Display* display) 
    : display_(display)
    , current_gif_(nullptr)
    , current_index_(0)
    , timer_(nullptr) {
}

GifPlayer::~GifPlayer() {
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    if (current_gif_) {
        lv_obj_del(current_gif_);
        current_gif_ = nullptr;
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
    
    current_gif_ = lv_gif_create(lv_scr_act());
    if (!current_gif_) {
        ESP_LOGE(TAG, "Failed to create GIF object");
        return false;
    }
    
    lv_obj_set_size(current_gif_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(current_gif_);
    
    // 添加事件回调
    lv_obj_add_event_cb(current_gif_, OnGifEvent, LV_EVENT_READY, this);
    
    // 创建定时器，每10秒切换一次动画
    timer_ = lv_timer_create(timer_cb, 10000, this);
    if (!timer_) {
        ESP_LOGE(TAG, "Failed to create timer");
        return false;
    }
    
    // 加载第一个动画
    LoadNextAnimation();
    return true;
}

void GifPlayer::LoadNextAnimation() {
    if (!current_gif_) {
        return;
    }
    
    ESP_LOGI(TAG, "Loading animation %d", current_index_);
    
    // 循环切换动画
    switch (current_index_) {
        case 0:
            lv_gif_set_src(current_gif_, &lgsx);  // 第一个
            break;
        case 1:
            lv_gif_set_src(current_gif_, &lzhch);
            break;
        case 2:
            lv_gif_set_src(current_gif_, &ldtlr);
            break;
        case 3:
            lv_gif_set_src(current_gif_, &ldxe);
            break;
        case 4:
            lv_gif_set_src(current_gif_, &lwq);
            break;
        case 5:
            lv_gif_set_src(current_gif_, &llr);
            break;
        case 6:
            lv_gif_set_src(current_gif_, &lzy);
            break;
        case 7:
            lv_gif_set_src(current_gif_, &lbuyao);
            break;
        case 8:
            lv_gif_set_src(current_gif_, &lyoushang);
            break;
        case 9:
            lv_gif_set_src(current_gif_, &lzuoyou);
            break;
        case 10:
            lv_gif_set_src(current_gif_, &game);
            break;
        case 11:
            lv_gif_set_src(current_gif_, &lshuizl);  // 最后一个
            break;
        default:
            current_index_ = 0;
            lv_gif_set_src(current_gif_, &lgsx);
            break;
    }
    
    current_index_ = (current_index_ + 1) % 12;  // 总共12个动画
}

void GifPlayer::StartLoop() {
    ESP_LOGI(TAG, "GIF animation loop started");
} 