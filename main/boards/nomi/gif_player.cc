#include "gif_player.h"
#include <esp_log.h>

// 声明 GIF 图像
LV_IMG_DECLARE(lzy);
LV_IMG_DECLARE(ting);
LV_IMG_DECLARE(lzuoyou);

static const char* TAG = "GifPlayer";

GifPlayer::GifPlayer(Display* display) 
    : display_(display)
    , current_gif_(nullptr)
    , current_index_(0) {
}

GifPlayer::~GifPlayer() {
    if (current_gif_) {
        lv_obj_del(current_gif_);
        current_gif_ = nullptr;
    }
}

bool GifPlayer::Initialize() {
    if (!display_) {
        ESP_LOGE(TAG, "Display not initialized");
        return false;
    }
    
    // 创建全屏 GIF 对象
    current_gif_ = lv_gif_create(lv_scr_act());
    if (!current_gif_) {
        ESP_LOGE(TAG, "Failed to create GIF object");
        return false;
    }
    
    // 设置全屏显示
    lv_obj_set_size(current_gif_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(current_gif_);
    
    // 加载第一个动画
    LoadNextAnimation();
    
    return true;
}

void GifPlayer::LoadNextAnimation() {
    if (!current_gif_) {
        return;
    }
    
    // 循环切换动画
    switch (current_index_) {
        case 0:
            lv_gif_set_src(current_gif_, &lzy);
            break;
        case 1:
            lv_gif_set_src(current_gif_, &ting);
            break;
        case 2:
            lv_gif_set_src(current_gif_, &lzuoyou);
            break;
        default:
            current_index_ = 0;
            lv_gif_set_src(current_gif_, &lzy);
            break;
    }
    
    current_index_ = (current_index_ + 1) % 3;
}

void GifPlayer::StartLoop() {
    // GIF 动画会自动循环播放
    ESP_LOGI(TAG, "GIF animation loop started");
} 