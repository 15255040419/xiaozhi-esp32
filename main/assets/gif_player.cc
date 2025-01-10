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

// 定义每个动画的播放时长（毫秒）
const uint32_t GifPlayer::ANIMATION_DURATIONS[] = {
    8000,   // lgsx     - 待机
    8000,   // lzhch    - 开心
    8000,   // ldtlr    - 点头
    8000,   // ldxe     - 点下额
    8000,   // lwq      - 微笑
    8000,   // llr      - 左右
    8000,   // lzy      - 左眼
    8000,   // lzuoyou  - 左右
    13000,   // game     - 游戏
    8000   // lshuizl  - 睡着了
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
    
    // 设置自动换行
    lv_label_set_long_mode(text_label_, LV_LABEL_LONG_WRAP);
    // 限制最大显示行数为2行
    lv_obj_set_height(text_label_, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(text_label_, 40, 0);  // 根据字体大小调整
    
    // 将标签放在屏幕底部
    lv_obj_align(text_label_, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_flag(text_label_, LV_OBJ_FLAG_HIDDEN);  // 初始时隐藏文字
    
    // 创建定时器，增加定时器间隔以降低CPU占用
    timer_ = lv_timer_create(timer_cb, ANIMATION_DURATIONS[0] + 500, this);  // 增加间隔
    if (!timer_) {
        ESP_LOGE(TAG, "Failed to create timer");
        return false;
    }
    
    // 预加载所有 GIF 到内存
    lv_gif_set_src(standby_gif_, &lgsx);
    lv_gif_set_src(listening_gif_, &ting);
    lv_gif_set_src(speaking_gif_, &kaixin);
    
    // 等待加载完成
    lv_timer_handler();
    
    // 设置当前GIF为待机动画
    current_gif_ = standby_gif_;
    lv_obj_clear_flag(current_gif_, LV_OBJ_FLAG_HIDDEN);
    
    initialized_ = true;
    return true;
}

void GifPlayer::SwitchToAnimation(lv_obj_t* gif) {
    if (!gif || gif == current_gif_) {
        return;
    }
    
    // 隐藏当前 GIF
    if (current_gif_) {
        lv_obj_add_flag(current_gif_, LV_OBJ_FLAG_HIDDEN);
    }
    
    // 暂停定时器
    if (timer_) {
        lv_timer_pause(timer_);
    }
    
    // 切换到新的 GIF
    current_gif_ = gif;
    lv_obj_clear_flag(current_gif_, LV_OBJ_FLAG_HIDDEN);
    
    // 如果是待机动画，恢复定时器
    if (gif == standby_gif_ && timer_) {
        lv_timer_resume(timer_);
    }
}

void GifPlayer::ShowSpeakingText(const char* text) {
    if (!text_label_ || !text) {
        return;
    }
    
    // 直接设置文字并显示
    lv_label_set_text(text_label_, text);
    lv_obj_clear_flag(text_label_, LV_OBJ_FLAG_HIDDEN);
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
    if (current_gif_ != listening_gif_) {
        SwitchToAnimation(listening_gif_);
    }
    // 隐藏文字标签
    if (text_label_) {
        lv_obj_add_flag(text_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GifPlayer::ShowSpeakingAnimation() {
    ESP_LOGI(TAG, "Showing speaking animation");
    if (current_gif_ != speaking_gif_) {
        SwitchToAnimation(speaking_gif_);
    }
}

void GifPlayer::LoadNextAnimation() {
    if (!standby_gif_ || current_gif_ != standby_gif_) {
        return;
    }

    ESP_LOGI(TAG, "Loading animation %d", current_index_);
    
    // 直接切换到下一个动画，不做内存清理
    const lv_img_dsc_t* next_gif = nullptr;
    switch (current_index_) {
        case 0: next_gif = &lgsx; break;
        case 1: next_gif = &lzhch; break;
        case 2: next_gif = &ldtlr; break;
        case 3: next_gif = &ldxe; break;
        case 4: next_gif = &lwq; break;
        case 5: next_gif = &llr; break;
        case 6: next_gif = &lzy; break;
        case 7: next_gif = &lzuoyou; break;
        case 8: next_gif = &game; break;
        case 9: next_gif = &lshuizl; break;
        default:
            current_index_ = 0;
            next_gif = &lgsx;
            break;
    }
    
    if (next_gif) {
        lv_gif_set_src(standby_gif_, next_gif);
        // 使用 TickType_t 替代 pdMS_TO_TICKS
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    // 更新定时器的周期
    if (timer_) {
        lv_timer_set_period(timer_, ANIMATION_DURATIONS[current_index_] + 100);
    }
    
    current_index_ = (current_index_ + 1) % 10;
} 