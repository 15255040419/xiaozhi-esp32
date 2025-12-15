#include "music_player_ui.h"
#include "esp_log.h"
#include "lvgl_theme.h"
#include "board.h"
#include <font_awesome.h>
#include <algorithm>

static const char* TAG = "MusicPlayerUI";

// LVGL符号定义（播放控制按钮）
#define SYMBOL_PLAY     "\xEF\x81\x8B"
#define SYMBOL_PAUSE    "\xEF\x81\x8C"
#define SYMBOL_PREVIOUS "\xEF\x81\x88"
#define SYMBOL_NEXT     "\xEF\x81\x91"

// 颜色定义
#define COLOR_PRIMARY           0x2196F3
#define COLOR_PROGRESS_BG_LIGHT 0xE0E0E0
#define COLOR_PROGRESS_BG_DARK  0x404040
#define COLOR_TEXT_DEFAULT      0x000000
#define COLOR_TEXT_BG_LIGHT     0xFFFFFF
#define COLOR_TEXT_BG_DARK      0x333333

// 响应式布局参数 - 使用百分比和比例
#define LAYOUT_PADDING_PERCENT      3      // 外边距占屏幕宽度的3%
#define VOLUME_HEIGHT_PERCENT       8      // 音量区域占屏幕高度的8%
#define CONTROL_HEIGHT_PERCENT      12     // 控制按钮区域占屏幕高度的12%
#define PROGRESS_HEIGHT_PERCENT     10     // 进度条区域占屏幕高度的10%
#define MIN_COMPONENT_HEIGHT        20     // 最小组件高度（像素）
#define MAX_COMPONENT_HEIGHT        100    // 最大组件高度（像素）

// 字体和颜色获取宏
#define GET_DEFAULT_FONT()      LV_FONT_DEFAULT
#define GET_ICON_FONT(theme)    ((theme) ? (theme)->icon_font()->font() : LV_FONT_DEFAULT)
#define GET_LARGE_ICON_FONT(theme) ((theme) ? (theme)->large_icon_font()->font() : LV_FONT_DEFAULT)
#define GET_TEXT_FONT(theme)    ((theme) ? (theme)->text_font()->font() : LV_FONT_DEFAULT)
#define GET_TEXT_COLOR(theme)   ((theme) ? (theme)->text_color() : lv_color_hex(COLOR_TEXT_DEFAULT))
#define GET_PRIMARY_COLOR(theme) ((theme) ? (theme)->text_color() : lv_color_hex(COLOR_PRIMARY))
#define IS_LIGHT_THEME(theme) ((theme) && lv_color_to_u32((theme)->text_color()) == 0xFF000000)
#define GET_PROGRESS_BG_COLOR(theme) (IS_LIGHT_THEME(theme) ? lv_color_hex(COLOR_PROGRESS_BG_LIGHT) : lv_color_hex(COLOR_PROGRESS_BG_DARK))

// 响应式尺寸计算辅助函数
static inline int CalcPercent(int total, int percent) {
    return (total * percent) / 100;
}

static inline int ClampSize(int size, int min_size, int max_size) {
    if (size < min_size) return min_size;
    if (size > max_size) return max_size;
    return size;
}

MusicPlayerUI::MusicPlayerUI(lv_obj_t* parent, int width, int height, LvglTheme* theme, int status_bar_height)
    : parent_(parent), container_(nullptr), song_title_label_(nullptr), lyrics_label_(nullptr),
      control_container_(nullptr), prev_btn_(nullptr), play_pause_btn_(nullptr),
      next_btn_(nullptr), progress_container_(nullptr), current_time_label_(nullptr),
      progress_bar_(nullptr), duration_label_(nullptr),
      volume_container_(nullptr), volume_slider_(nullptr), volume_label_(nullptr), music_icon_label_(nullptr),
      visible_(false), play_state_(STOPPED), width_(width), height_(height), 
      status_bar_height_(status_bar_height), current_volume_(50),
      theme_(theme), current_song_title_(""), current_artist_(""), current_lyrics_(""),
      play_pause_callback_(nullptr), play_pause_user_data_(nullptr),
      previous_callback_(nullptr), previous_user_data_(nullptr),
      next_callback_(nullptr), next_user_data_(nullptr),
      progress_callback_(nullptr), progress_user_data_(nullptr),
      volume_callback_(nullptr), volume_user_data_(nullptr) {
    
    ESP_LOGI(TAG, "Creating responsive MusicPlayerUI with size %dx%d, status_bar_height=%d", width, height, status_bar_height);
}

MusicPlayerUI::~MusicPlayerUI() {
    DestroyUI();
}

void MusicPlayerUI::Show() {
    if (visible_) return;
    
    CreateUI();
    
    if (container_) {
        lv_obj_move_foreground(container_);
        lv_obj_clear_flag(parent_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(parent_, LV_DIR_NONE);
        lv_obj_set_scrollbar_mode(parent_, LV_SCROLLBAR_MODE_OFF);
        ESP_LOGI(TAG, "Music player shown");
    }
    
    visible_ = true;
}

void MusicPlayerUI::Hide() {
    if (!visible_) return;
    DestroyUI();
    visible_ = false;
    ESP_LOGI(TAG, "Music player hidden");
}

void MusicPlayerUI::CreateUI() {
    if (container_) return;
    
    // 计算响应式尺寸
    int padding = CalcPercent(width_, LAYOUT_PADDING_PERCENT);
    
    // 使用传入的状态栏高度，如果没有传入则使用默认估算值
    int status_bar_height = status_bar_height_ > 0 ? status_bar_height_ : std::min(35, std::max(25, CalcPercent(height_, 6)));
    
    // 调整可用高度，扣除状态栏
    int available_height = height_ - status_bar_height;
    
    // 判断屏幕方向：竖屏(height > width)或横屏(width >= height)
    bool is_portrait = height_ > width_;
    
    int volume_height = ClampSize(CalcPercent(available_height, VOLUME_HEIGHT_PERCENT), MIN_COMPONENT_HEIGHT, MAX_COMPONENT_HEIGHT);
    int control_height = ClampSize(CalcPercent(available_height, CONTROL_HEIGHT_PERCENT), MIN_COMPONENT_HEIGHT * 2, MAX_COMPONENT_HEIGHT * 2);
    int progress_height = ClampSize(CalcPercent(available_height, PROGRESS_HEIGHT_PERCENT), MIN_COMPONENT_HEIGHT, MAX_COMPONENT_HEIGHT);
    int element_spacing = std::max(2, padding / 3);
    
    // 根据屏幕方向调整歌曲信息区域的高度
    int reserved_height = volume_height + control_height + progress_height + (padding * 2) + (element_spacing * 3);
    int song_info_height;
    if (is_portrait) {
        // 竖屏：歌曲信息区域占剩余空间的80%，让播放控件往上移
        int remaining_space = available_height - reserved_height;
        song_info_height = std::max(50, CalcPercent(remaining_space, 80));
    } else {
        // 横屏：歌曲信息区域占剩余空间的100%，能显示更多歌词
        song_info_height = std::max(60, available_height - reserved_height);
    }
    
    ESP_LOGI(TAG, "Layout: status_bar=%d, padding=%d, volume=%d, control=%d, progress=%d, song_info=%d", 
             status_bar_height, padding, volume_height, control_height, progress_height, song_info_height);
    
    // 创建主容器 - 从状态栏下方开始
    container_ = lv_obj_create(parent_);
    lv_obj_set_size(container_, width_, available_height);
    lv_obj_set_pos(container_, 0, status_bar_height);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(container_, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
            Board::GetInstance().SetPowerSaveMode(false);
        }
    }, LV_EVENT_ALL, nullptr);
    
    lv_obj_set_style_bg_opa(container_, LV_OPA_30, 0);
    lv_obj_set_style_border_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, padding, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(container_, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
    
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container_, element_spacing, 0);
    
    // 计算统一的进度条高度（音量滑块和时间进度条使用相同高度）
    int unified_bar_height = std::max(8, CalcPercent(progress_height, 40));
    
    // 1. 音量控制区域
    CreateVolumeControl(volume_height, padding, unified_bar_height);
    
    // 2. 歌曲信息区域
    CreateSongInfoArea(song_info_height, padding);
    
    // 3. 控制按钮区域
    CreateControlButtons(control_height, padding);
    
    // 4. 时间进度条区域 - 始终在最底部
    CreateProgressBar(progress_height, padding);
    
    ESP_LOGI(TAG, "Responsive UI created successfully");
}

void MusicPlayerUI::CreateVolumeControl(int height, int padding, int progress_bar_height) {
    volume_container_ = lv_obj_create(container_);
    lv_obj_add_flag(volume_container_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(volume_container_, LV_PCT(100), height);
    lv_obj_set_style_bg_opa(volume_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(volume_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(volume_container_, 0, 0);
    lv_obj_clear_flag(volume_container_, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_flex_flow(volume_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(volume_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(volume_container_, padding, 0);  // 增加列间距
    
    // 计算标签固定宽度（和进度条区域的时间标签保持一致）
    int label_width = std::max(50, CalcPercent(width_, 12));
    
    // 音量图标 - 固定宽度，右对齐
    music_icon_label_ = lv_label_create(volume_container_);
    lv_obj_set_width(music_icon_label_, label_width);
    lv_label_set_text(music_icon_label_, FONT_AWESOME_VOLUME_HIGH);  // 🔊 喇叭图标
    lv_obj_set_style_text_font(music_icon_label_, GET_ICON_FONT(theme_), 0);  // 使用图标字体
    lv_obj_set_style_text_color(music_icon_label_, GET_TEXT_COLOR(theme_), 0);
    lv_obj_set_style_text_align(music_icon_label_, LV_TEXT_ALIGN_RIGHT, 0);
    
    // 音量滑块 - 与进度条相同的可视高度，但扩大触摸区域
    volume_slider_ = lv_slider_create(volume_container_);
    int slider_width = std::max(80, width_ - (label_width * 2) - (padding * 3));  // 和进度条相同的计算方式
    // 使用传入的统一高度，确保与时间进度条粗细完全一致
    lv_obj_set_size(volume_slider_, slider_width, progress_bar_height);
    
    // 扩展触摸区域，使整个进度条区域都可以响应触摸
    lv_obj_set_ext_click_area(volume_slider_, 30);  // 在四周扩展30像素的触摸区域
    
    // 设置滑块模式：允许点击跳转和拖动
    lv_slider_set_mode(volume_slider_, LV_SLIDER_MODE_NORMAL);  // 普通模式，支持点击和拖动
    lv_slider_set_range(volume_slider_, 0, 100);
    lv_slider_set_value(volume_slider_, current_volume_, LV_ANIM_OFF);
    
    // 关键设置：让整个滑块区域都可以响应触摸
    lv_obj_clear_flag(volume_slider_, LV_OBJ_FLAG_ADV_HITTEST);  // 整个区域可点击，不只是按钮
    lv_obj_add_flag(volume_slider_, LV_OBJ_FLAG_CLICKABLE);      // 确保可点击
    lv_obj_add_flag(volume_slider_, LV_OBJ_FLAG_CLICK_FOCUSABLE); // 允许通过点击获得焦点
    lv_obj_clear_flag(volume_slider_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); // 禁止滚动干扰
    
    // 背景样式 - 与时间进度条一致
    lv_obj_set_style_bg_color(volume_slider_, GET_PROGRESS_BG_COLOR(theme_), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(volume_slider_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(volume_slider_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(volume_slider_, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(volume_slider_, 0, LV_PART_MAIN);  // 移除阴影
    
    // 进度指示器样式 - 使用主题颜色，与时间进度条一致
    lv_obj_set_style_bg_color(volume_slider_, GET_PRIMARY_COLOR(theme_), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(volume_slider_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(volume_slider_, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(volume_slider_, 0, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(volume_slider_, 0, LV_PART_INDICATOR);  // 移除阴影
    
    // 显示一个小的滑块按钮，便于用户看到当前位置和拖动
    lv_obj_set_style_bg_color(volume_slider_, GET_PRIMARY_COLOR(theme_), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(volume_slider_, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(volume_slider_, 0, LV_PART_KNOB);  // 移除描边
    lv_obj_set_style_shadow_width(volume_slider_, 0, LV_PART_KNOB);  // 移除阴影
    lv_obj_set_style_pad_all(volume_slider_, 2, LV_PART_KNOB);  // 按钮padding
    int knob_size = std::max(14, progress_bar_height + 2);  // 按钮稍大于滑块，最小14像素
    lv_obj_set_style_width(volume_slider_, knob_size, LV_PART_KNOB);
    lv_obj_set_style_height(volume_slider_, knob_size, LV_PART_KNOB);
    lv_obj_set_style_radius(volume_slider_, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    
    // 监听多种事件，提高触摸响应性
    lv_obj_add_event_cb(volume_slider_, VolumeEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(volume_slider_, VolumeEventCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(volume_slider_, VolumeEventCb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(volume_slider_, VolumeEventCb, LV_EVENT_RELEASED, this);
    
    // 音量数值 - 固定宽度，左对齐
    volume_label_ = lv_label_create(volume_container_);
    lv_obj_set_width(volume_label_, label_width);
    lv_label_set_text_fmt(volume_label_, "%d", current_volume_);
    lv_obj_set_style_text_font(volume_label_, GET_DEFAULT_FONT(), 0);
    lv_obj_set_style_text_color(volume_label_, GET_TEXT_COLOR(theme_), 0);
    lv_obj_set_style_text_align(volume_label_, LV_TEXT_ALIGN_LEFT, 0);
}

void MusicPlayerUI::CreateSongInfoArea(int height, int padding) {
    // 创建外层容器，固定高度，透明背景
    lv_obj_t* info_box = lv_obj_create(container_);
    lv_obj_add_flag(info_box, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(info_box, LV_PCT(90), height);
    lv_obj_set_style_pad_all(info_box, padding, 0);
    lv_obj_set_style_border_width(info_box, 0, 0);
    lv_obj_clear_flag(info_box, LV_OBJ_FLAG_SCROLLABLE);
    
    // 容器背景样式 - 完全透明
    lv_obj_set_style_bg_opa(info_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(info_box, LV_OPA_TRANSP, 0);
    
    // 容器使用flex布局，从顶部开始排列
    lv_obj_set_flex_flow(info_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(info_box, 0, 0);  // 不需要间距，歌词会自动占据剩余空间
    lv_obj_set_style_pad_top(info_box, 10, 0);  // 顶部留一点间距
    
    // 创建歌名label（顶部居中显示）
    song_title_label_ = lv_label_create(info_box);
    lv_obj_set_width(song_title_label_, LV_PCT(90));
    lv_obj_set_style_bg_opa(song_title_label_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(song_title_label_, 0, 0);
    lv_obj_set_style_text_align(song_title_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(song_title_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_opa(song_title_label_, LV_OPA_COVER, 0);  // 歌名完全不透明
    lv_obj_clear_flag(song_title_label_, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);  // 确保在同一track
    
    if (theme_) {
        lv_obj_set_style_text_font(song_title_label_, theme_->text_font()->font(), 0);
        lv_obj_set_style_text_color(song_title_label_, theme_->text_color(), 0);
    } else {
        lv_obj_set_style_text_font(song_title_label_, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(song_title_label_, lv_color_hex(COLOR_TEXT_DEFAULT), 0);
    }
    lv_label_set_text(song_title_label_, "音乐加载中...");
    
    // 创建歌词容器（占据剩余空间，用于让歌词垂直居中）
    lv_obj_t* lyrics_container = lv_obj_create(info_box);
    lv_obj_set_size(lyrics_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(lyrics_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(lyrics_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(lyrics_container, 0, 0);
    lv_obj_clear_flag(lyrics_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_grow(lyrics_container, 1);  // 占据剩余所有空间
    // 容器内部使用flex布局，让歌词居中
    lv_obj_set_flex_flow(lyrics_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lyrics_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // 在容器内创建歌词label（会自动垂直和水平居中）
    lyrics_label_ = lv_label_create(lyrics_container);
    lv_obj_set_width(lyrics_label_, LV_PCT(95));
    lv_obj_set_style_bg_opa(lyrics_label_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(lyrics_label_, 0, 0);
    lv_obj_set_style_text_align(lyrics_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lyrics_label_, LV_LABEL_LONG_WRAP);
    
    if (theme_) {
        lv_obj_set_style_text_font(lyrics_label_, theme_->text_font()->font(), 0);
        lv_obj_set_style_text_color(lyrics_label_, theme_->text_color(), 0);
        lv_obj_set_style_text_opa(lyrics_label_, LV_OPA_80, 0);  // 歌词稍透明，区分歌名
    } else {
        lv_obj_set_style_text_font(lyrics_label_, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(lyrics_label_, lv_color_hex(COLOR_TEXT_DEFAULT), 0);
        lv_obj_set_style_text_opa(lyrics_label_, LV_OPA_80, 0);
    }
    lv_label_set_text(lyrics_label_, "");
}

void MusicPlayerUI::CreateControlButtons(int height, int padding) {
    control_container_ = lv_obj_create(container_);
    lv_obj_add_flag(control_container_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(control_container_, LV_PCT(100), height);
    lv_obj_set_style_bg_opa(control_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(control_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(control_container_, 0, 0);
    lv_obj_clear_flag(control_container_, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_flex_flow(control_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control_container_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    const lv_font_t* icon_font = theme_ ? theme_->icon_font()->font() : LV_FONT_DEFAULT;
    const lv_font_t* large_icon_font = theme_ ? theme_->large_icon_font()->font() : LV_FONT_DEFAULT;
    lv_color_t btn_color = GET_PRIMARY_COLOR(theme_);
    
    // 上一曲按钮
    prev_btn_ = lv_label_create(control_container_);
    lv_label_set_text(prev_btn_, SYMBOL_PREVIOUS);
    lv_obj_set_style_text_font(prev_btn_, icon_font, 0);
    lv_obj_set_style_text_color(prev_btn_, btn_color, 0);
    lv_obj_add_flag(prev_btn_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(prev_btn_, PreviousEventCb, LV_EVENT_CLICKED, this);
    
    // 播放/暂停按钮（大号）
    play_pause_btn_ = lv_label_create(control_container_);
    lv_label_set_text(play_pause_btn_, SYMBOL_PLAY);
    lv_obj_set_style_text_font(play_pause_btn_, large_icon_font, 0);
    lv_obj_set_style_text_color(play_pause_btn_, btn_color, 0);
    lv_obj_add_flag(play_pause_btn_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(play_pause_btn_, PlayPauseEventCb, LV_EVENT_CLICKED, this);
    
    // 下一曲按钮
    next_btn_ = lv_label_create(control_container_);
    lv_label_set_text(next_btn_, SYMBOL_NEXT);
    lv_obj_set_style_text_font(next_btn_, icon_font, 0);
    lv_obj_set_style_text_color(next_btn_, btn_color, 0);
    lv_obj_add_flag(next_btn_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(next_btn_, NextEventCb, LV_EVENT_CLICKED, this);
}

void MusicPlayerUI::CreateProgressBar(int height, int padding) {
    progress_container_ = lv_obj_create(container_);
    lv_obj_add_flag(progress_container_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(progress_container_, LV_PCT(100), height);
    lv_obj_set_style_bg_opa(progress_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(progress_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(progress_container_, 0, 0);
    lv_obj_clear_flag(progress_container_, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_flex_flow(progress_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(progress_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(progress_container_, padding, 0);  // 增加列间距
    
    lv_color_t text_color = theme_ ? theme_->text_color() : lv_color_hex(COLOR_TEXT_DEFAULT);
    
    // 计算时间标签的固定宽度（足够容纳 "99:99" 格式）
    int time_label_width = std::max(50, CalcPercent(width_, 12));  // 增加宽度确保显示完整
    
    // 当前时间 - 固定宽度，右对齐
    current_time_label_ = lv_label_create(progress_container_);
    lv_obj_set_width(current_time_label_, time_label_width);
    lv_label_set_text(current_time_label_, "00:00");
    lv_obj_set_style_text_font(current_time_label_, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(current_time_label_, text_color, 0);
    lv_obj_set_style_text_align(current_time_label_, LV_TEXT_ALIGN_RIGHT, 0);
    
    // 进度条 - 固定宽度
    progress_bar_ = lv_bar_create(progress_container_);
    int bar_width = std::max(80, width_ - (time_label_width * 2) - (padding * 3));  // 总宽度减去两个时间标签和间距
    lv_obj_set_size(progress_bar_, bar_width, height * 0.4);
    lv_bar_set_range(progress_bar_, 0, 1000);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
    
    lv_obj_set_style_bg_color(progress_bar_, GET_PROGRESS_BG_COLOR(theme_), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar_, GET_PRIMARY_COLOR(theme_), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_bar_, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(progress_bar_, ProgressEventCb, LV_EVENT_CLICKED, this);
    
    // 总时长 - 固定宽度，左对齐
    duration_label_ = lv_label_create(progress_container_);
    lv_obj_set_width(duration_label_, time_label_width);
    lv_label_set_text(duration_label_, "--:--");
    lv_obj_set_style_text_font(duration_label_, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(duration_label_, text_color, 0);
    lv_obj_set_style_text_align(duration_label_, LV_TEXT_ALIGN_LEFT, 0);
}

void MusicPlayerUI::DestroyUI() {
    if (container_) {
        lv_obj_del(container_);
        container_ = nullptr;
        song_title_label_ = nullptr;
        lyrics_label_ = nullptr;
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
        UpdateSongInfoDisplay();
    } else {
        current_song_title_ = "";
        UpdateSongInfoDisplay();
    }
}

void MusicPlayerUI::SetArtist(const char* artist) {
    if (artist) {
        current_artist_ = artist;
        UpdateSongInfoDisplay();
    } else {
        current_artist_ = "";
        UpdateSongInfoDisplay();
    }
}

void MusicPlayerUI::SetLyrics(const char* lyrics) {
    if (lyrics && strlen(lyrics) > 0) {
        current_lyrics_ = lyrics;
    } else {
        current_lyrics_ = "";
    }
    UpdateSongInfoDisplay();
}

void MusicPlayerUI::SetProgress(float progress) {
    if (progress_bar_) {
        int value = (int)(progress * 1000);
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
    if (!song_title_label_ || !lyrics_label_) return;
    
    // 更新歌名（固定不动）
    std::string title_text = "";
    if (!current_song_title_.empty() && !current_artist_.empty()) {
        title_text = current_song_title_ + " - " + current_artist_;
    } else if (!current_song_title_.empty()) {
        title_text = current_song_title_;
    } else if (!current_artist_.empty()) {
        title_text = current_artist_;
    } else {
        title_text = "音乐加载中...";
    }
    
    const char* current_title = lv_label_get_text(song_title_label_);
    if (!current_title || title_text != current_title) {
        lv_label_set_text(song_title_label_, title_text.c_str());
    }
    
    // 更新歌词（可更新）
    const char* current_lyric = lv_label_get_text(lyrics_label_);
    if (!current_lyric || current_lyrics_ != current_lyric) {
        lv_label_set_text(lyrics_label_, current_lyrics_.c_str());
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
        if (play_state_ == PLAYING) {
            lv_label_set_text(play_pause_btn_, SYMBOL_PAUSE);
        } else {
            lv_label_set_text(play_pause_btn_, SYMBOL_PLAY);
        }
    }
}

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
        if (volume_label_) {
            lv_label_set_text_fmt(volume_label_, "%d", volume);
        }
    }
}

// 静态事件回调
void MusicPlayerUI::PlayPauseEventCb(lv_event_t* e) {
    MusicPlayerUI* ui = static_cast<MusicPlayerUI*>(lv_event_get_user_data(e));
    if (ui && ui->play_pause_callback_) {
        ui->play_pause_callback_(ui->play_pause_user_data_);
    }
}

void MusicPlayerUI::PreviousEventCb(lv_event_t* e) {
    MusicPlayerUI* ui = static_cast<MusicPlayerUI*>(lv_event_get_user_data(e));
    if (ui && ui->previous_callback_) {
        ui->previous_callback_(ui->previous_user_data_);
    }
}

void MusicPlayerUI::NextEventCb(lv_event_t* e) {
    MusicPlayerUI* ui = static_cast<MusicPlayerUI*>(lv_event_get_user_data(e));
    if (ui && ui->next_callback_) {
        ui->next_callback_(ui->next_user_data_);
    }
}

void MusicPlayerUI::ProgressEventCb(lv_event_t* e) {
    MusicPlayerUI* ui = static_cast<MusicPlayerUI*>(lv_event_get_user_data(e));
    if (ui && ui->progress_callback_) {
        lv_obj_t* progress_bar = static_cast<lv_obj_t*>(lv_event_get_target(e));
        lv_point_t point;
        lv_indev_get_point(lv_indev_get_act(), &point);
        
        lv_coord_t bar_x = lv_obj_get_x(progress_bar);
        lv_coord_t bar_width = lv_obj_get_width(progress_bar);
        float progress = (float)(point.x - bar_x) / bar_width;
        
        if (progress < 0) progress = 0;
        if (progress > 1) progress = 1;
        
        ui->progress_callback_(progress, ui->progress_user_data_);
    }
}

void MusicPlayerUI::VolumeEventCb(lv_event_t* e) {
    MusicPlayerUI* ui = static_cast<MusicPlayerUI*>(lv_event_get_user_data(e));
    if (!ui) return;
    
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int volume = lv_slider_get_value(slider);
    
    // 触摸开始时唤醒一次（避免频繁唤醒）
    static bool touch_active = false;
    if (code == LV_EVENT_PRESSED) {
        touch_active = true;
        Board::GetInstance().SetPowerSaveMode(false);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        touch_active = false;
    }
    
    // 处理音量变化
    if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSING || 
        code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED) {
        
        // 只有音量真正改变时才更新
        if (volume != ui->current_volume_) {
            ui->current_volume_ = volume;
            
            // 更新显示
            if (ui->volume_label_) {
                lv_label_set_text_fmt(ui->volume_label_, "%d", volume);
            }
            
            // 触发回调
            if (ui->volume_callback_) {
                ui->volume_callback_(volume, ui->volume_user_data_);
            }
        }
    }
}

void MusicPlayerUI::UpdateTheme(LvglTheme* theme) {
    if (!theme) return;
    
    theme_ = theme;
    
    if (song_title_label_) {
        lv_obj_set_style_text_font(song_title_label_, theme_->text_font()->font(), 0);
        lv_obj_set_style_text_color(song_title_label_, theme_->text_color(), 0);
        lv_obj_set_style_text_opa(song_title_label_, LV_OPA_COVER, 0);  // 歌名完全不透明
    }
    
    if (lyrics_label_) {
        lv_obj_set_style_text_font(lyrics_label_, theme_->text_font()->font(), 0);
        lv_obj_set_style_text_color(lyrics_label_, theme_->text_color(), 0);
        lv_obj_set_style_text_opa(lyrics_label_, LV_OPA_80, 0);  // 歌词稍透明
    }
    
    if (music_icon_label_) {
        lv_obj_set_style_text_font(music_icon_label_, GET_ICON_FONT(theme_), 0);
        lv_obj_set_style_text_color(music_icon_label_, theme_->text_color(), 0);
    }
    
    if (current_time_label_) {
        lv_obj_set_style_text_color(current_time_label_, theme_->text_color(), 0);
    }
    
    if (duration_label_) {
        lv_obj_set_style_text_color(duration_label_, theme_->text_color(), 0);
    }
    
    if (volume_label_) {
        lv_obj_set_style_text_color(volume_label_, theme_->text_color(), 0);
    }
    
    if (volume_slider_) {
        lv_obj_set_style_bg_color(volume_slider_, GET_PROGRESS_BG_COLOR(theme_), LV_PART_MAIN);
        lv_obj_set_style_bg_color(volume_slider_, GET_PRIMARY_COLOR(theme_), LV_PART_INDICATOR);
    }
    
    if (progress_bar_) {
        lv_obj_set_style_bg_color(progress_bar_, GET_PROGRESS_BG_COLOR(theme_), LV_PART_MAIN);
        lv_obj_set_style_bg_color(progress_bar_, GET_PRIMARY_COLOR(theme_), LV_PART_INDICATOR);
    }
    
    ESP_LOGI(TAG, "Theme updated");
}
