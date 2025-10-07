#include "music_player_ui.h"
#include "esp_log.h"
#include "lvgl_theme.h"
#include "board.h"

static const char* TAG = "MusicPlayerUI";

// LVGL符号定义（播放控制按钮）
#define SYMBOL_PLAY     "\xEF\x81\x8B"     // 播放符号
#define SYMBOL_PAUSE    "\xEF\x81\x8C"     // 暂停符号  
#define SYMBOL_PREVIOUS "\xEF\x81\x88"     // 上一曲符号
#define SYMBOL_NEXT     "\xEF\x81\x91"     // 下一曲符号
#define SYMBOL_VOLUME   "\xEF\x80\xA6"     // 音量符号

// 颜色定义（方便统一修改）
#define COLOR_PRIMARY           0x2196F3    // 主色调 - 蓝色（播放按钮、所有进度条指示器）
#define COLOR_PROGRESS_BG_LIGHT 0xE0E0E0    // 浅色主题进度条背景色 - 浅灰色
#define COLOR_PROGRESS_BG_DARK  0x404040    // 深色主题进度条背景色 - 深灰色
#define COLOR_TEXT_DEFAULT      0x000000    // 默认文字颜色 - 黑色
#define COLOR_TEXT_BG_LIGHT     0xFFFFFF    // 浅色主题文字背景颜色 - 白色
#define COLOR_TEXT_BG_DARK      0x333333    // 深色主题文字背景颜色 - 深灰色
#define COLOR_BACKGROUND_WHITE  0xFFFFFF    // 白色背景

// 字体获取宏定义（方便统一管理）
#define GET_DEFAULT_FONT()      LV_FONT_DEFAULT
#define GET_ICON_FONT(theme)    ((theme) ? (theme)->icon_font()->font() : LV_FONT_DEFAULT)
#define GET_LARGE_ICON_FONT(theme) ((theme) ? (theme)->large_icon_font()->font() : LV_FONT_DEFAULT)
#define GET_TEXT_FONT(theme)    ((theme) ? (theme)->text_font()->font() : LV_FONT_DEFAULT)

// 颜色获取宏定义（优先使用主题颜色，回退到默认颜色）
#define GET_TEXT_COLOR(theme)   ((theme) ? (theme)->text_color() : lv_color_hex(COLOR_TEXT_DEFAULT))
#define GET_PRIMARY_COLOR(theme) ((theme) ? (theme)->text_color() : lv_color_hex(COLOR_PRIMARY))

// 根据主题判断是否为浅色主题（通过文字颜色判断）
// 浅色主题：黑色文字(0xFF000000)，深色主题：白色文字(0xFFFFFFFF)
#define IS_LIGHT_THEME(theme) ((theme) && lv_color_to_u32((theme)->text_color()) == 0xFF000000)
#define GET_TEXT_BG_COLOR(theme) (IS_LIGHT_THEME(theme) ? lv_color_hex(COLOR_TEXT_BG_LIGHT) : lv_color_hex(COLOR_TEXT_BG_DARK))
#define GET_PROGRESS_BG_COLOR(theme) (IS_LIGHT_THEME(theme) ? lv_color_hex(COLOR_PROGRESS_BG_LIGHT) : lv_color_hex(COLOR_PROGRESS_BG_DARK))

MusicPlayerUI::MusicPlayerUI(lv_obj_t* parent, int width, int height, LvglTheme* theme)
    : parent_(parent), container_(nullptr), song_info_label_(nullptr),
      control_container_(nullptr), prev_btn_(nullptr), play_pause_btn_(nullptr),
      next_btn_(nullptr), progress_container_(nullptr), current_time_label_(nullptr),
      progress_bar_(nullptr), duration_label_(nullptr),
      volume_container_(nullptr), volume_slider_(nullptr), volume_label_(nullptr), music_icon_label_(nullptr),
      visible_(false), play_state_(STOPPED), width_(width), height_(height), current_volume_(50),
      theme_(theme), current_song_title_(""), current_artist_(""), current_lyrics_(""),
      play_pause_callback_(nullptr), play_pause_user_data_(nullptr),
      previous_callback_(nullptr), previous_user_data_(nullptr),
      next_callback_(nullptr), next_user_data_(nullptr),
      progress_callback_(nullptr), progress_user_data_(nullptr),
      volume_callback_(nullptr), volume_user_data_(nullptr) {
    
    ESP_LOGI(TAG, "Creating MusicPlayerUI with size %dx%d", width, height);
}

MusicPlayerUI::~MusicPlayerUI() {
    DestroyUI();
}

void MusicPlayerUI::Show() {
    if (visible_) return;
    
    CreateUI();
    
    // 确保整个容器在最前面，并确保父容器不可滚动
    if (container_) {
        lv_obj_move_foreground(container_);
        
        // 确保父容器（屏幕）也不可滚动
        lv_obj_clear_flag(parent_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(parent_, LV_DIR_NONE);
        lv_obj_set_scrollbar_mode(parent_, LV_SCROLLBAR_MODE_OFF);
        
        ESP_LOGI(TAG, "Music player container moved to foreground and parent scroll disabled");
    }
    
    visible_ = true;
    ESP_LOGI(TAG, "Music player UI shown");
}

void MusicPlayerUI::Hide() {
    if (!visible_) return;
    
    DestroyUI();
    visible_ = false;
    ESP_LOGI(TAG, "Music player UI hidden");
}

void MusicPlayerUI::CreateUI() {
    if (container_) return;  // 已经创建
    
    // 创建主容器，填满整个屏幕
    container_ = lv_obj_create(parent_);
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_pos(container_, 0, 0);
    
    // 设置毛玻璃效果背景
    lv_obj_set_style_bg_opa(container_, LV_OPA_30, 0);
    lv_obj_set_style_border_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 10, 0);  // 统一的外边距
    
    // 禁用滚动，固定界面
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(container_, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
    
    // 设置为垂直FLEX布局
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container_, 2, 0);  // 最小元素间距，接近0间距效果
    
    // 1. 音量控制区域 - 使用FLEX布局
    volume_container_ = lv_obj_create(container_);
    lv_obj_set_width(volume_container_, LV_PCT(100));  // 使用100%宽度，但内容固定
    lv_obj_set_height(volume_container_, 40);  // 固定高度
    lv_obj_set_style_bg_opa(volume_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(volume_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(volume_container_, 0, 0);  // 移除内边距，避免挤压
    lv_obj_set_style_margin_top(volume_container_, 30, 0);
    
    // 禁用滚动和任何可能的交互
    lv_obj_clear_flag(volume_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(volume_container_, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(volume_container_, LV_SCROLLBAR_MODE_OFF);
    
    // 设置水平FLEX布局，居中对齐，固定间距
    lv_obj_set_flex_flow(volume_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(volume_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(volume_container_, 12, 0);  // 增加列间距，给文字更多空间
    
    // 音量图标标签
    music_icon_label_ = lv_label_create(volume_container_);
    lv_label_set_text(music_icon_label_, "Vol");
    lv_obj_set_style_text_font(music_icon_label_, GET_DEFAULT_FONT(), 0);
    lv_obj_set_style_text_color(music_icon_label_, GET_TEXT_COLOR(theme_), 0);
    
    // 音量滑块 - 使用固定宽度，避免动态调整
    volume_slider_ = lv_slider_create(volume_container_);
    lv_obj_set_width(volume_slider_, 150);  // 固定宽度150px，避免长度变化
    lv_obj_set_height(volume_slider_, 20);
    lv_obj_set_style_margin_left(volume_slider_, 0, 0);   // 移除边距，依赖容器的pad_column
    lv_obj_set_style_margin_right(volume_slider_, 0, 0);  // 移除边距，依赖容器的pad_column
    lv_slider_set_range(volume_slider_, 0, 100);
    lv_slider_set_value(volume_slider_, current_volume_, LV_ANIM_OFF);
    // 滑块样式
    // 音量滑块样式设置 - 统一使用主题色
    lv_color_t progress_bg_color = GET_PROGRESS_BG_COLOR(theme_);
    lv_obj_set_style_bg_color(volume_slider_, progress_bg_color, LV_PART_MAIN);  // 跟随主题的进度条背景色
    
    // 主题颜色已正确应用
    lv_obj_set_style_bg_opa(volume_slider_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider_, GET_PRIMARY_COLOR(theme_), LV_PART_INDICATOR);  // 指示器使用主题主色调
    lv_obj_set_style_bg_opa(volume_slider_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider_, GET_PRIMARY_COLOR(theme_), LV_PART_KNOB);  // 旋钮使用主题主色调
    lv_obj_set_style_bg_opa(volume_slider_, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_add_event_cb(volume_slider_, VolumeEventCb, LV_EVENT_VALUE_CHANGED, this);
    
    // 音量数值标签
    volume_label_ = lv_label_create(volume_container_);
    lv_label_set_text_fmt(volume_label_, "%d", current_volume_);
    lv_obj_set_style_text_font(volume_label_, GET_DEFAULT_FONT(), 0);
    lv_obj_set_style_text_color(volume_label_, GET_TEXT_COLOR(theme_), 0);
    
    // 2. 歌曲信息区域 - 占用主要空间，显示3行文字（歌名-歌手 + 2行歌词）
    song_info_label_ = lv_label_create(container_);
    lv_obj_set_width(song_info_label_, LV_PCT(90));  // 90%宽度
    lv_obj_set_height(song_info_label_, 100);  // 固定高度100px
    lv_obj_remove_flag(song_info_label_, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);  // 移除弹性增长
    lv_obj_set_style_pad_all(song_info_label_, 12, 0);  // 增加内边距，更好的显示效果
    lv_obj_set_style_margin_top(song_info_label_, 10, 0);  // 距离音量控制区域10px
    
    // 使用主题样式设置 - 文字水平和垂直居中
    lv_obj_set_style_text_align(song_info_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_flex_align(song_info_label_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (theme_) {
        lv_obj_set_style_text_font(song_info_label_, theme_->text_font()->font(), 0);
        lv_obj_set_style_text_color(song_info_label_, theme_->text_color(), 0);
        lv_obj_set_style_bg_color(song_info_label_, GET_PROGRESS_BG_COLOR(theme_), 0);  // 使用与进度条相同的背景色
        lv_obj_set_style_bg_opa(song_info_label_, LV_OPA_60, 0);  // 稍微增加不透明度以便更好显示
    } else {
        lv_obj_set_style_text_font(song_info_label_, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(song_info_label_, lv_color_hex(COLOR_TEXT_DEFAULT), 0);
        lv_obj_set_style_bg_color(song_info_label_, lv_color_hex(COLOR_PROGRESS_BG_LIGHT), 0);  // 无主题时使用浅色背景
        lv_obj_set_style_bg_opa(song_info_label_, LV_OPA_60, 0);  // 稍微增加不透明度以便更好显示
    }
    lv_obj_set_style_radius(song_info_label_, 8, 0);  // 圆角
    
    // 设置文本和模式
    lv_label_set_text(song_info_label_, "音乐加载中...");
    lv_label_set_long_mode(song_info_label_, LV_LABEL_LONG_WRAP);
    
    // 确保标签本身不可滚动
    lv_obj_clear_flag(song_info_label_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(song_info_label_, LV_DIR_NONE);
    
    ESP_LOGI(TAG, "Song info label created with flex layout");
    
    // 3. 控制按钮容器 - 使用FLEX布局
    control_container_ = lv_obj_create(container_);
    lv_obj_set_width(control_container_, LV_PCT(100));  // 100%宽度
    lv_obj_set_height(control_container_, LV_SIZE_CONTENT);  // 高度自适应
    lv_obj_set_style_bg_opa(control_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(control_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(control_container_, 10, 0);
    lv_obj_set_style_margin_top(control_container_, 0, 0);  // 距离歌曲信息区域00px
    
    // 禁用控制按钮容器的滚动
    lv_obj_clear_flag(control_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(control_container_, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(control_container_, LV_SCROLLBAR_MODE_OFF);
    
    // 水平FLEX布局，居中对齐，设置按钮间距
    lv_obj_set_flex_flow(control_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(control_container_, 40, 0);  // 增加按钮间距到40px
    
    // 上一曲按钮 - 只显示符号，无背景
    prev_btn_ = lv_label_create(control_container_);
    lv_label_set_text(prev_btn_, SYMBOL_PREVIOUS);
    if (theme_) {
        lv_obj_set_style_text_font(prev_btn_, theme_->icon_font()->font(), 0);  // 使用主题的图标字体
        lv_obj_set_style_text_color(prev_btn_, GET_PRIMARY_COLOR(theme_), 0);  // 使用主题主色调
    } else {
        lv_obj_set_style_text_font(prev_btn_, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(prev_btn_, GET_PRIMARY_COLOR(theme_), 0);
    }
    lv_obj_add_flag(prev_btn_, LV_OBJ_FLAG_CLICKABLE);  // 让标签可点击
    lv_obj_add_event_cb(prev_btn_, PreviousEventCb, LV_EVENT_CLICKED, this);
    
    // 播放/暂停按钮 - 只显示符号，使用大字体突出显示
    play_pause_btn_ = lv_label_create(control_container_);
    lv_label_set_text(play_pause_btn_, SYMBOL_PLAY);
    if (theme_) {
        lv_obj_set_style_text_font(play_pause_btn_, theme_->large_icon_font()->font(), 0);  // 播放按钮使用大图标字体
        lv_obj_set_style_text_color(play_pause_btn_, GET_PRIMARY_COLOR(theme_), 0);  // 使用主题主色调
    } else {
        lv_obj_set_style_text_font(play_pause_btn_, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(play_pause_btn_, GET_PRIMARY_COLOR(theme_), 0);
    }
    lv_obj_add_flag(play_pause_btn_, LV_OBJ_FLAG_CLICKABLE);  // 让标签可点击
    lv_obj_add_event_cb(play_pause_btn_, PlayPauseEventCb, LV_EVENT_CLICKED, this);
    
    // 下一曲按钮 - 只显示符号，无背景
    next_btn_ = lv_label_create(control_container_);
    lv_label_set_text(next_btn_, SYMBOL_NEXT);
    if (theme_) {
        lv_obj_set_style_text_font(next_btn_, theme_->icon_font()->font(), 0);  // 使用主题的图标字体
        lv_obj_set_style_text_color(next_btn_, GET_PRIMARY_COLOR(theme_), 0);  // 使用主题主色调
    } else {
        lv_obj_set_style_text_font(next_btn_, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(next_btn_, GET_PRIMARY_COLOR(theme_), 0);
    }
    lv_obj_add_flag(next_btn_, LV_OBJ_FLAG_CLICKABLE);  // 让标签可点击
    lv_obj_add_event_cb(next_btn_, NextEventCb, LV_EVENT_CLICKED, this);
    
    // 4. 进度条容器 - 固定在底部，使用FLEX布局
    progress_container_ = lv_obj_create(container_);
    lv_obj_set_width(progress_container_, LV_PCT(100));  // 使用100%宽度，但内容固定
    lv_obj_set_height(progress_container_, 45);  // 固定高度45px
    lv_obj_set_style_bg_opa(progress_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(progress_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(progress_container_, 0, 0);  // 移除内边距，避免挤压
    lv_obj_set_style_margin_top(progress_container_, 5, 0);  // 距离播放控制按钮5px
    // 注意：不设置margin_bottom，因为位置已经由宽度、高度和margin_top完全确定
    
    // 禁用进度条容器的滚动
    lv_obj_clear_flag(progress_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(progress_container_, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(progress_container_, LV_SCROLLBAR_MODE_OFF);
    
    // 水平FLEX布局，设置固定间距
    lv_obj_set_flex_flow(progress_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(progress_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(progress_container_, 15, 0);  // 增加列间距，给时间标签更多空间
    
    // 当前时间标签
    current_time_label_ = lv_label_create(progress_container_);
    lv_label_set_text(current_time_label_, "00:00");
    lv_obj_set_style_text_font(current_time_label_, LV_FONT_DEFAULT, 0);
    if (theme_) {
        lv_obj_set_style_text_color(current_time_label_, theme_->text_color(), 0);
    }
    
    // 进度条 - 使用固定像素宽度，避免动态调整
    progress_bar_ = lv_bar_create(progress_container_);
    lv_obj_set_width(progress_bar_, 100);  // 减少宽度到100px，给时间标签更多空间
    lv_obj_set_height(progress_bar_, 12);
    lv_obj_set_style_margin_left(progress_bar_, 0, 0);   // 移除边距，依赖容器的pad_column
    lv_obj_set_style_margin_right(progress_bar_, 0, 0);  // 移除边距，依赖容器的pad_column
    lv_bar_set_range(progress_bar_, 0, 1000);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
    // 使用统一定义的颜色
    lv_obj_set_style_bg_color(progress_bar_, GET_PROGRESS_BG_COLOR(theme_), LV_PART_MAIN);  // 跟随主题的进度条背景色
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_COVER, LV_PART_MAIN);  // 确保背景不透明
    lv_obj_set_style_bg_color(progress_bar_, GET_PRIMARY_COLOR(theme_), LV_PART_INDICATOR);  // 使用主题主色调
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_COVER, LV_PART_INDICATOR);  // 确保进度不透明
    lv_obj_set_style_radius(progress_bar_, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(progress_bar_, ProgressEventCb, LV_EVENT_CLICKED, this);
    
    // 总时长标签
    duration_label_ = lv_label_create(progress_container_);
    lv_label_set_text(duration_label_, "--:--");
    lv_obj_set_style_text_font(duration_label_, LV_FONT_DEFAULT, 0);
    if (theme_) {
        lv_obj_set_style_text_color(duration_label_, theme_->text_color(), 0);
    }
    
    ESP_LOGI(TAG, "Music player UI created successfully");
}

void MusicPlayerUI::DestroyUI() {
    if (container_) {
        lv_obj_del(container_);
        container_ = nullptr;
        song_info_label_ = nullptr;
        control_container_ = nullptr;
        prev_btn_ = nullptr;
        play_pause_btn_ = nullptr;
        next_btn_ = nullptr;
        progress_container_ = nullptr;
        current_time_label_ = nullptr;
        progress_bar_ = nullptr;
        duration_label_ = nullptr;
        volume_container_ = nullptr;
        volume_slider_ = nullptr;
        volume_label_ = nullptr;
        music_icon_label_ = nullptr;
    }
}

void MusicPlayerUI::SetSongTitle(const char* title) {
    if (title) {
        current_song_title_ = title;
        ESP_LOGI(TAG, "Song title set to: %s", title);
        UpdateSongInfoDisplay();
    } else {
        current_song_title_ = "";
        ESP_LOGW(TAG, "Song title cleared");
        UpdateSongInfoDisplay();
    }
}

void MusicPlayerUI::SetArtist(const char* artist) {
    if (artist) {
        current_artist_ = artist;
        ESP_LOGI(TAG, "Artist set to: %s", artist);
        UpdateSongInfoDisplay();
    } else {
        current_artist_ = "";
        ESP_LOGW(TAG, "Artist cleared");
        UpdateSongInfoDisplay();
    }
}

void MusicPlayerUI::SetLyrics(const char* lyrics) {
    if (lyrics && strlen(lyrics) > 0) {
        current_lyrics_ = lyrics;
        ESP_LOGI(TAG, "Lyrics set to: %s", lyrics);
    } else {
        current_lyrics_ = "";
        ESP_LOGI(TAG, "Lyrics cleared");
    }
    UpdateSongInfoDisplay();
}

void MusicPlayerUI::SetProgress(float progress) {
    if (progress_bar_) {
        int value = (int)(progress * 1000);  // 范围是0-1000
        if (value > 1000) value = 1000;
        if (value < 0) value = 0;
        lv_bar_set_value(progress_bar_, value, LV_ANIM_OFF);
    }
}

void MusicPlayerUI::SetPlayState(PlayState state) {
    play_state_ = state;
    UpdatePlayPauseButton();
}

void MusicPlayerUI::UpdateSongInfoDisplay() {
    if (!song_info_label_) {
        ESP_LOGE(TAG, "song_info_label_ is null!");
        return;
    }
    
    std::string display_text = "";
    
    // 构建三行显示格式：第1行=歌名-歌手，第2-3行=歌词
    if (!current_song_title_.empty() || !current_artist_.empty()) {
        // 第一行：歌名和歌手
        if (!current_song_title_.empty() && !current_artist_.empty()) {
            display_text = current_song_title_ + " - " + current_artist_;
        } else if (!current_song_title_.empty()) {
            display_text = current_song_title_;
        } else if (!current_artist_.empty()) {
            display_text = current_artist_;
        }
        
        // 第2-3行：歌词（如果有的话）
        if (!current_lyrics_.empty()) {
            display_text += "\n" + current_lyrics_;
        }
    } else if (!current_lyrics_.empty()) {
        // 如果没有歌名和歌手但有歌词，只显示歌词
        display_text = current_lyrics_;
    } else {
        // 都没有，显示默认文字
        display_text = "音乐加载中...";
    }
    
    // 检查文本是否发生变化，避免不必要的更新
    const char* current_text = lv_label_get_text(song_info_label_);
    if (current_text && display_text == current_text) {
        return;  // 文本没有变化，不需要更新
    }
    
    ESP_LOGI(TAG, "Updating song info display: '%s'", display_text.c_str());
    
    // 设置到标签
    lv_label_set_text(song_info_label_, display_text.c_str());
    
    // 只在必要时更新样式（减少CPU占用）
    static bool styles_set = false;
    if (!styles_set) {
        // 确保样式正确 - 使用主题字体和颜色
        if (theme_) {
            lv_obj_set_style_text_font(song_info_label_, theme_->text_font()->font(), 0);
            lv_obj_set_style_text_color(song_info_label_, theme_->text_color(), 0);
        } else {
            lv_obj_set_style_text_font(song_info_label_, LV_FONT_DEFAULT, 0);
            lv_obj_set_style_text_color(song_info_label_, lv_color_hex(0x000000), 0);
        }
        lv_obj_set_style_text_align(song_info_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(song_info_label_, LV_LABEL_LONG_WRAP);
        styles_set = true;
    }
}

void MusicPlayerUI::SetDuration(const char* duration) {
    if (duration_label_ && duration) {
        lv_label_set_text(duration_label_, duration);
    }
}

void MusicPlayerUI::SetCurrentTime(const char* current_time) {
    if (current_time_label_ && current_time) {
        lv_label_set_text(current_time_label_, current_time);
    }
}

void MusicPlayerUI::UpdatePlayPauseButton() {
    if (play_pause_btn_) {
        // 现在 play_pause_btn_ 直接是一个标签，不需要获取子对象
        if (play_state_ == PLAYING) {
            lv_label_set_text(play_pause_btn_, SYMBOL_PAUSE);
        } else {
            lv_label_set_text(play_pause_btn_, SYMBOL_PLAY);
        }
    }
}

// 设置回调函数
void MusicPlayerUI::SetPlayPauseCallback(void (*callback)(void* user_data), void* user_data) {
    play_pause_callback_ = callback;
    play_pause_user_data_ = user_data;
}

void MusicPlayerUI::SetPreviousCallback(void (*callback)(void* user_data), void* user_data) {
    previous_callback_ = callback;
    previous_user_data_ = user_data;
}

void MusicPlayerUI::SetNextCallback(void (*callback)(void* user_data), void* user_data) {
    next_callback_ = callback;
    next_user_data_ = user_data;
}

void MusicPlayerUI::SetProgressCallback(void (*callback)(float progress, void* user_data), void* user_data) {
    progress_callback_ = callback;
    progress_user_data_ = user_data;
}

void MusicPlayerUI::SetVolumeCallback(void (*callback)(int volume, void* user_data), void* user_data) {
    volume_callback_ = callback;
    volume_user_data_ = user_data;
}

void MusicPlayerUI::SetVolume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    current_volume_ = volume;
    
    if (volume_slider_) {
        lv_slider_set_value(volume_slider_, volume, LV_ANIM_OFF);
        
        // 更新音量数值标签
        if (volume_label_) {
            lv_label_set_text_fmt(volume_label_, "%d", volume);
            ESP_LOGI("MusicPlayerUI", "SetVolume called: %d, label updated", volume);
        } else {
            ESP_LOGW("MusicPlayerUI", "SetVolume: Volume label not initialized");
        }
    }
}

// 静态事件回调
void MusicPlayerUI::PlayPauseEventCb(lv_event_t* e) {
    MusicPlayerUI* ui = static_cast<MusicPlayerUI*>(lv_event_get_user_data(e));
    if (ui) {
        if (ui->play_pause_callback_) {
            ui->play_pause_callback_(ui->play_pause_user_data_);
        }
    }
}

void MusicPlayerUI::PreviousEventCb(lv_event_t* e) {
    MusicPlayerUI* ui = static_cast<MusicPlayerUI*>(lv_event_get_user_data(e));
    if (ui) {
        if (ui->previous_callback_) {
            ui->previous_callback_(ui->previous_user_data_);
        }
    }
}

void MusicPlayerUI::NextEventCb(lv_event_t* e) {
    MusicPlayerUI* ui = static_cast<MusicPlayerUI*>(lv_event_get_user_data(e));
    if (ui) {
        if (ui->next_callback_) {
            ui->next_callback_(ui->next_user_data_);
        }
    }
}

void MusicPlayerUI::ProgressEventCb(lv_event_t* e) {
    MusicPlayerUI* ui = static_cast<MusicPlayerUI*>(lv_event_get_user_data(e));
    if (ui) {
        if (ui->progress_callback_) {
            lv_obj_t* progress_bar = static_cast<lv_obj_t*>(lv_event_get_target(e));
            lv_point_t point;
            lv_indev_get_point(lv_indev_get_act(), &point);
            
            // 计算点击位置对应的进度
            lv_coord_t bar_x = lv_obj_get_x(progress_bar);
            lv_coord_t bar_width = lv_obj_get_width(progress_bar);
            float progress = (float)(point.x - bar_x) / bar_width;
            
            if (progress < 0) progress = 0;
            if (progress > 1) progress = 1;
            
            ui->progress_callback_(progress, ui->progress_user_data_);
        }
    }
}

void MusicPlayerUI::VolumeEventCb(lv_event_t* e) {
    MusicPlayerUI* ui = static_cast<MusicPlayerUI*>(lv_event_get_user_data(e));
    if (ui) {
        if (ui->volume_callback_) {
            lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
            int volume = lv_slider_get_value(slider);
            ui->current_volume_ = volume;
            
            // 更新音量数值标签
            if (ui->volume_label_) {
                lv_label_set_text_fmt(ui->volume_label_, "%d", volume);
                ESP_LOGI("MusicPlayerUI", "Volume slider changed to: %d, label updated", volume);
            } else {
                ESP_LOGW("MusicPlayerUI", "Volume label not initialized in callback");
            }
            
            ui->volume_callback_(volume, ui->volume_user_data_);
        }
    }
}

// 更新主题
void MusicPlayerUI::UpdateTheme(LvglTheme* theme) {
    if (!theme) return;
    
    theme_ = theme;
    
    // 如果UI已经创建，更新所有文本元素的颜色和字体
    if (song_info_label_) {
        lv_obj_set_style_text_font(song_info_label_, theme_->text_font()->font(), 0);
        lv_obj_set_style_text_color(song_info_label_, theme_->text_color(), 0);
        lv_obj_set_style_bg_color(song_info_label_, GET_PROGRESS_BG_COLOR(theme_), 0);  // 更新背景色与进度条一致
        ESP_LOGI(TAG, "Updated song info label theme with progress background color");
    }
    
    if (music_icon_label_) {
        lv_obj_set_style_text_color(music_icon_label_, theme_->text_color(), 0);
        ESP_LOGI(TAG, "Updated volume icon (Vol) label theme");
    }
    
    if (current_time_label_) {
        lv_obj_set_style_text_color(current_time_label_, theme_->text_color(), 0);
        ESP_LOGI(TAG, "Updated current time label theme");
    }
    
    if (duration_label_) {
        lv_obj_set_style_text_color(duration_label_, theme_->text_color(), 0);
        ESP_LOGI(TAG, "Updated duration label theme");
    }
    
    // 更新音量数值标签（需要找到它）
    if (volume_container_) {
        lv_obj_t* vol_value_label = lv_obj_get_child(volume_container_, 2); // 第三个子元素
        if (vol_value_label) {
            lv_obj_set_style_text_color(vol_value_label, theme_->text_color(), 0);
            ESP_LOGI(TAG, "Updated volume value label theme");
        }
    }
    
    // 更新进度条背景色
    if (volume_slider_) {
        lv_obj_set_style_bg_color(volume_slider_, GET_PROGRESS_BG_COLOR(theme_), LV_PART_MAIN);
        ESP_LOGI(TAG, "Updated volume slider background theme");
    }
    
    if (progress_bar_) {
        lv_obj_set_style_bg_color(progress_bar_, GET_PROGRESS_BG_COLOR(theme_), LV_PART_MAIN);
        ESP_LOGI(TAG, "Updated progress bar background theme");
    }
    
    ESP_LOGI(TAG, "Music player UI theme updated");
}
