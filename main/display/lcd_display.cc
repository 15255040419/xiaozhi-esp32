#include "lcd_display.h"
#include "bizhi.h"  // 添加背景图片头文件

#include <vector>
#include <font_awesome_symbols.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include "assets/lang_config.h"
#include <cstring>
#include "settings.h"

#include "board.h"
#include <ctime>
#include "esp_timer.h"

// 添加时间字体声明
LV_FONT_DECLARE(font_time);

// 在文件顶部添加字体声明
LV_FONT_DECLARE(font_dingding);

// 在文件顶部添加壁纸声明
LV_IMG_DECLARE(bizhi);  // 默认壁纸
LV_IMG_DECLARE(wallpaper1);
LV_IMG_DECLARE(wallpaper2);
LV_IMG_DECLARE(wallpaper3);

// 在文件中添加壁纸资源数组
static const lv_img_dsc_t* wallpapers[] = {
    &bizhi,
    &wallpaper1,
    &wallpaper2,
    &wallpaper3,
};

// 壁纸数量
static const int WALLPAPER_COUNT = sizeof(wallpapers) / sizeof(wallpapers[0]);

#define TAG "LcdDisplay"

// Color definitions for dark theme
#define DARK_BACKGROUND_COLOR       lv_color_hex(0x000000)     // 纯黑色背景
#define DARK_TEXT_COLOR             lv_color_white()           // White text
#define DARK_CHAT_BACKGROUND_COLOR  lv_color_hex(0x000000)     // 聊天区域也使用纯黑色背景
#define DARK_USER_BUBBLE_COLOR      lv_color_hex(0x1A6C37)     // Dark green
#define DARK_ASSISTANT_BUBBLE_COLOR lv_color_hex(0x333333)     // Dark gray
#define DARK_SYSTEM_BUBBLE_COLOR    lv_color_hex(0x2A2A2A)     // Medium gray
#define DARK_SYSTEM_TEXT_COLOR      lv_color_hex(0xAAAAAA)     // Light gray text
#define DARK_BORDER_COLOR           lv_color_hex(0x000000)     // 纯黑色边框，与背景相同
#define DARK_LOW_BATTERY_COLOR      lv_color_hex(0xFF0000)     // Red for dark mode

// Color definitions for light theme
#define LIGHT_BACKGROUND_COLOR       lv_color_white()           // White background
#define LIGHT_TEXT_COLOR             lv_color_black()           // Black text
#define LIGHT_CHAT_BACKGROUND_COLOR  lv_color_hex(0xE0E0E0)     // Light gray background
#define LIGHT_USER_BUBBLE_COLOR      lv_color_hex(0x95EC69)     // WeChat green
#define LIGHT_ASSISTANT_BUBBLE_COLOR lv_color_white()           // White
#define LIGHT_SYSTEM_BUBBLE_COLOR    lv_color_hex(0xE0E0E0)     // Light gray
#define LIGHT_SYSTEM_TEXT_COLOR      lv_color_hex(0x666666)     // Dark gray text
#define LIGHT_BORDER_COLOR           lv_color_hex(0xE0E0E0)     // Light gray border
#define LIGHT_LOW_BATTERY_COLOR      lv_color_black()           // Black for light mode

// Theme color structure
struct ThemeColors {
    lv_color_t background;
    lv_color_t text;
    lv_color_t chat_background;
    lv_color_t user_bubble;
    lv_color_t assistant_bubble;
    lv_color_t system_bubble;
    lv_color_t system_text;
    lv_color_t border;
    lv_color_t low_battery;
};

// Define dark theme colors
static const ThemeColors DARK_THEME = {
    .background = DARK_BACKGROUND_COLOR,
    .text = DARK_TEXT_COLOR,
    .chat_background = DARK_CHAT_BACKGROUND_COLOR,
    .user_bubble = DARK_USER_BUBBLE_COLOR,
    .assistant_bubble = DARK_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = DARK_SYSTEM_BUBBLE_COLOR,
    .system_text = DARK_SYSTEM_TEXT_COLOR,
    .border = DARK_BORDER_COLOR,
    .low_battery = DARK_LOW_BATTERY_COLOR
};

// Define light theme colors
static const ThemeColors LIGHT_THEME = {
    .background = LIGHT_BACKGROUND_COLOR,
    .text = LIGHT_TEXT_COLOR,
    .chat_background = LIGHT_CHAT_BACKGROUND_COLOR,
    .user_bubble = LIGHT_USER_BUBBLE_COLOR,
    .assistant_bubble = LIGHT_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = LIGHT_SYSTEM_BUBBLE_COLOR,
    .system_text = LIGHT_SYSTEM_TEXT_COLOR,
    .border = LIGHT_BORDER_COLOR,
    .low_battery = LIGHT_LOW_BATTERY_COLOR
};

// Current theme - initialize based on default config
static ThemeColors current_theme = DARK_THEME;

// 添加一个静态变量来跟踪冒号的可见性
static bool colon_visible = true;

// 定时器回调函数
static void colon_blink_timer_callback(void* arg) {
    LcdDisplay* display = static_cast<LcdDisplay*>(arg);
    if (display) {
        // 切换冒号可见性
        colon_visible = !colon_visible;
        // 更新时间显示
        display->ShowTimeAndDate();
    }
}

LV_FONT_DECLARE(font_awesome_30_4);
LV_FONT_DECLARE(font_dingding);

SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy,
                           DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts) {
    width_ = width;
    height_ = height;

    // draw black
    std::vector<uint16_t> buffer(width_, 0x0000);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD screen");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 10),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    // Update the theme
    if (current_theme_name_ == "dark") {
        current_theme = DARK_THEME;
    } else if (current_theme_name_ == "light") {
        current_theme = LIGHT_THEME;
    }

    SetupUI();
}

// RGB LCD实现
RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y,
                           bool mirror_x, bool mirror_y, bool swap_xy,
                           DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts) {
    width_ = width;
    height_ = height;
    
    // draw black
    std::vector<uint16_t> buffer(width_, 0x0000);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD screen");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .buffer_size = static_cast<uint32_t>(width_ * 10),
        .double_buffer = true,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = 1,
            .swap_bytes = 0,
            .full_refresh = 1,
            .direct_mode = 1,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        }
    };
    
    display_ = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add RGB display");
        return;
    }
    
    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    // Update the theme
    if (current_theme_name_ == "dark") {
        current_theme = DARK_THEME;
    } else if (current_theme_name_ == "light") {
        current_theme = LIGHT_THEME;
    }

    SetupUI();
}

LcdDisplay::~LcdDisplay() {
    // 然后再清理 LVGL 对象
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }
    if (status_bar_ != nullptr) {
        lv_obj_del(status_bar_);
    }
    if (side_bar_ != nullptr) {
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }
    if (display_ != nullptr) {
        lv_display_delete(display_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
}

bool LcdDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void LcdDisplay::Unlock() {
    lvgl_port_unlock();
}

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
void LcdDisplay::SetupUI() {
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);
    lv_obj_set_style_text_color(screen, current_theme.text, 0);
    lv_obj_set_style_bg_color(screen, current_theme.background, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, current_theme.background, 0);
    lv_obj_set_style_border_color(container_, current_theme.border, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, current_theme.background, 0);
    lv_obj_set_style_text_color(status_bar_, current_theme.text, 0);
    
    /* Content - Chat area */
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 10, 0);
    lv_obj_set_style_bg_color(content_, current_theme.chat_background, 0); // Background for chat area
    
    // 使用方案2: 边框颜色与背景相同
    lv_obj_set_style_border_color(content_, current_theme.chat_background, 0);
    
    // Enable scrolling for chat content
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    
    // Create a flex container for chat messages
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content_, 10, 0); // Space between messages

    // We'll create chat messages dynamically in SetChatMessage
    chat_message_label_ = nullptr;

    /* Status bar */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 10, 0);
    lv_obj_set_style_pad_right(status_bar_, 10, 0);
    lv_obj_set_style_pad_top(status_bar_, 2, 0);
    lv_obj_set_style_pad_bottom(status_bar_, 2, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    // 设置状态栏的内容垂直居中
    lv_obj_set_flex_align(status_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 创建emotion_label_在状态栏最左侧
    emotion_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_4, 0);
    lv_obj_set_style_text_color(emotion_label_, current_theme.text, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);
    lv_obj_set_style_margin_right(emotion_label_, 5, 0); // 添加右边距，与后面的元素分隔

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, current_theme.text, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, current_theme.text, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    
    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, current_theme.text, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(network_label_, current_theme.text, 0);
    lv_obj_set_style_margin_left(network_label_, 5, 0); // 添加左边距，与前面的元素分隔

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, current_theme.text, 0);
    lv_obj_set_style_margin_left(battery_label_, 5, 0); // 添加左边距，与前面的元素分隔

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, current_theme.low_battery, 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    lv_obj_t* low_battery_label = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label, lv_color_white(), 0);
    lv_obj_center(low_battery_label);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    // 创建欢迎容器 - 直接放在屏幕上，而不是container_中
    welcome_container_ = lv_obj_create(screen);
    lv_obj_set_style_radius(welcome_container_, 0, 0);
    lv_obj_set_size(welcome_container_, LV_HOR_RES, LV_VER_RES); // 设置为全屏大小
    lv_obj_set_style_pad_all(welcome_container_, 0, 0);
    lv_obj_set_style_border_width(welcome_container_, 0, 0); // 移除边框
    
    // 创建背景图片
    bg_img_ = lv_img_create(welcome_container_);
    
    // 从NVS读取上次保存的壁纸设置
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        uint8_t saved_index;
        err = nvs_get_u8(handle, "wallpaper", &saved_index);
        if (err == ESP_OK) {
            current_wallpaper_index_ = saved_index;
        } else {
            current_wallpaper_index_ = 0; // 如果没有保存的设置，使用第一个壁纸
        }
        nvs_close(handle);
    }

    // 设置壁纸
    if (current_wallpaper_index_ >= WALLPAPER_COUNT) {
        current_wallpaper_index_ = 0;
    }
    lv_img_set_src(bg_img_, wallpapers[current_wallpaper_index_]);
    lv_obj_set_size(bg_img_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(bg_img_);
    lv_obj_set_style_img_recolor_opa(bg_img_, 50, 0);
    lv_obj_set_style_img_recolor(bg_img_, lv_color_black(), 0);
    
    // 获取当前时间和日期
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    // 获取日期和星期
    int day = timeinfo->tm_mday;
    const char* weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    const char* weekday = weekdays[timeinfo->tm_wday];
    
    // 格式化日期为 "23 周日" 格式
    char date_str[20];
    snprintf(date_str, sizeof(date_str), "%d %s", day, weekday);
    
    // 创建日期标签 - 使用丁丁字体
    lv_obj_t* date_label = lv_label_create(welcome_container_);
    lv_obj_set_style_text_font(date_label, &font_dingding, 0);
    lv_obj_set_style_text_color(date_label, lv_color_white(), 0);
    lv_label_set_text(date_label, date_str);
    lv_obj_align(date_label, LV_ALIGN_TOP_RIGHT, -10, 90);
    
    // 创建分离的时间标签
    hour_label_ = lv_label_create(welcome_container_);
    colon_label_ = lv_label_create(welcome_container_);
    minute_label_ = lv_label_create(welcome_container_);
    
    // 设置字体和颜色
    lv_obj_set_style_text_font(hour_label_, &font_time, 0);
    lv_obj_set_style_text_font(colon_label_, &font_time, 0);
    lv_obj_set_style_text_font(minute_label_, &font_time, 0);
    lv_obj_set_style_text_color(hour_label_, lv_color_white(), 0);
    lv_obj_set_style_text_color(colon_label_, lv_color_white(), 0);
    lv_obj_set_style_text_color(minute_label_, lv_color_white(), 0);
    
    // 设置初始文本
    lv_label_set_text(hour_label_, "00");
    lv_label_set_text(colon_label_, ":");
    lv_label_set_text(minute_label_, "00");
    
    // 禁用标签的自动换行
    lv_label_set_long_mode(hour_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(colon_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(minute_label_, LV_LABEL_LONG_CLIP);
    
    // 设置固定宽度 - 为40号字体设置更大的宽度
    lv_coord_t hour_width = 60;  // 为40号字体增加宽度
    lv_coord_t minute_width = 60; // 为40号字体增加宽度
    lv_obj_set_width(hour_label_, hour_width);
    lv_obj_set_width(minute_label_, minute_width);
    
    // 设置文本对齐方式为居中
    lv_obj_set_style_text_align(hour_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(minute_label_, LV_TEXT_ALIGN_CENTER, 0);
    
    // 获取冒号的实际宽度
    lv_obj_update_layout(colon_label_);
    lv_coord_t colon_width = lv_obj_get_width(colon_label_);
    
    // 定义标签之间的固定间距
    lv_coord_t spacing = 2; // 为40号字体增加间距
    
    // 计算总宽度
    lv_coord_t total_width = hour_width + spacing + colon_width + spacing + minute_width;
    
    // 计算起始x坐标（从右边缘向左偏移）
    lv_coord_t right_margin = 5; // 右边距
    lv_coord_t start_x = LV_HOR_RES - right_margin - total_width;
    
    // 计算时间标签的垂直位置 - 在日期下方，电池图标上方
    lv_coord_t time_y = 40; // 距离顶部40像素，可以根据需要调整
    
    // 设置每个标签的精确位置
    lv_obj_set_pos(hour_label_, LV_HOR_RES - hour_width - spacing - colon_width - spacing - minute_width - 10, time_y);
    lv_obj_set_pos(colon_label_, LV_HOR_RES - colon_width - spacing - minute_width - 10, time_y);
    lv_obj_set_pos(minute_label_, LV_HOR_RES - minute_width - 10, time_y);
    
    // 创建欢迎界面上的电池图标
    lv_obj_t* welcome_battery_label = lv_label_create(welcome_container_);
    lv_obj_set_style_text_font(welcome_battery_label, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(welcome_battery_label, lv_color_white(), 0);
    lv_label_set_text(welcome_battery_label, "");
    lv_obj_align(welcome_battery_label, LV_ALIGN_TOP_RIGHT, -10, 5);
    
    // 创建欢迎界面上的WiFi图标
    lv_obj_t* welcome_network_label = lv_label_create(welcome_container_);
    lv_obj_set_style_text_font(welcome_network_label, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(welcome_network_label, lv_color_white(), 0);
    lv_label_set_text(welcome_network_label, "");
    lv_obj_align(welcome_network_label, LV_ALIGN_TOP_RIGHT, -40, 5);
    
    // 创建欢迎界面上的静音图标
    lv_obj_t* welcome_mute_label = lv_label_create(welcome_container_);
    lv_obj_set_style_text_font(welcome_mute_label, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(welcome_mute_label, lv_color_white(), 0);
    lv_label_set_text(welcome_mute_label, "");
    lv_obj_align(welcome_mute_label, LV_ALIGN_TOP_RIGHT, -70, 5);
    
    // 保存这些标签的引用，以便稍后更新
    welcome_battery_label_ = welcome_battery_label;
    welcome_network_label_ = welcome_network_label;
    welcome_mute_label_ = welcome_mute_label;
    
    // 初始时显示欢迎界面，隐藏聊天界面和状态栏
    lv_obj_clear_flag(welcome_container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN); // 隐藏整个container_，包括状态栏和聊天区域
    
    // 初始化冒号闪烁定时器
    esp_timer_handle_t colon_timer;
    esp_timer_create_args_t timer_args = {
        .callback = &colon_blink_timer_callback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "colon_blink_timer"
    };
    
    esp_timer_create(&timer_args, &colon_timer);
    esp_timer_start_periodic(colon_timer, 1000000); // 1秒 = 1000000微秒

    // 强制立即更新一次时间显示，确保初始显示正确
    ShowTimeAndDate();
}

#define  MAX_MESSAGES 20
void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    
    // 如果是系统消息且内容只有空格，这是我们用来切换界面的特殊情况
    bool is_switch_trigger = (strcmp(role, "system") == 0 && content && strlen(content) == 1 && content[0] == ' ');
    
    // 如果有消息内容或是切换触发器，隐藏欢迎界面，显示聊天界面
    if ((content && strlen(content) > 0) || is_switch_trigger) {
        if (welcome_container_ != nullptr) {
            lv_obj_add_flag(welcome_container_, LV_OBJ_FLAG_HIDDEN);
        }
        if (container_ != nullptr) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
        if (content_ != nullptr) {
            lv_obj_clear_flag(content_, LV_OBJ_FLAG_HIDDEN);
        }
        
        // 如果只是切换触发器，不创建消息气泡
        if (is_switch_trigger) {
            return;
        }
    } else {
        // 如果没有消息内容，显示欢迎界面，隐藏聊天界面
        if (welcome_container_ != nullptr) {
            lv_obj_clear_flag(welcome_container_, LV_OBJ_FLAG_HIDDEN);
        }
        if (container_ != nullptr) {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
        return; // 避免创建空消息框
    }
    
    // 检查消息数量是否超过限制
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (child_count >= MAX_MESSAGES) {
        // 删除最早的消息（第一个子对象）
        lv_obj_t* first_child = lv_obj_get_child(content_, 0);
        lv_obj_t* last_child = lv_obj_get_child(content_, child_count - 1);
        if (first_child != nullptr) {
            lv_obj_del(first_child);
        }
        // Scroll to the last message immediately
        if (last_child != nullptr) {
            lv_obj_scroll_to_view_recursive(last_child, LV_ANIM_OFF);
        }
    }
    
    // Create a message bubble
    lv_obj_t* msg_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(msg_bubble, 1, 0);
    lv_obj_set_style_border_color(msg_bubble, current_theme.border, 0);
    lv_obj_set_style_pad_all(msg_bubble, 8, 0);

    // Create the message text
    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_label_set_text(msg_text, content);
    
    // 计算文本实际宽度
    lv_coord_t text_width = lv_txt_get_width(content, strlen(content), fonts_.text_font, 0);

    // 计算气泡宽度
    lv_coord_t max_width = LV_HOR_RES * 85 / 100 - 16;  // 屏幕宽度的85%
    lv_coord_t min_width = 20;  
    lv_coord_t bubble_width;
    
    // 确保文本宽度不小于最小宽度
    if (text_width < min_width) {
        text_width = min_width;
    }

    // 如果文本宽度小于最大宽度，使用文本宽度
    if (text_width < max_width) {
        bubble_width = text_width; 
    } else {
        bubble_width = max_width;
    }
    
    // 设置消息文本的宽度
    lv_obj_set_width(msg_text, bubble_width);  // 减去padding
    lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(msg_text, fonts_.text_font, 0);

    // 设置气泡宽度
    lv_obj_set_width(msg_bubble, bubble_width);
    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

    // Set alignment and style based on message role
    if (strcmp(role, "user") == 0) {
        // User messages are right-aligned with green background
        lv_obj_set_style_bg_color(msg_bubble, current_theme.user_bubble, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, current_theme.text, 0);
        
        // 设置自定义属性标记气泡类型
        lv_obj_set_user_data(msg_bubble, (void*)"user");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "assistant") == 0) {
        // Assistant messages are left-aligned with white background
        lv_obj_set_style_bg_color(msg_bubble, current_theme.assistant_bubble, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, current_theme.text, 0);
        
        // 设置自定义属性标记气泡类型
        lv_obj_set_user_data(msg_bubble, (void*)"assistant");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        // System messages are center-aligned with light gray background
        lv_obj_set_style_bg_color(msg_bubble, current_theme.system_bubble, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, current_theme.system_text, 0);
        
        // 设置自定义属性标记气泡类型
        lv_obj_set_user_data(msg_bubble, (void*)"system");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    }
    
    // Create a full-width container for user messages to ensure right alignment
    if (strcmp(role, "user") == 0) {
        // Create a full-width container
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        
        // Make container transparent and borderless
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        
        // Move the message bubble into this container
        lv_obj_set_parent(msg_bubble, container);
        
        // Right align the bubble in the container
        lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, -25, 0);
        
        // Auto-scroll to this container
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else if (strcmp(role, "system") == 0) {
        // 为系统消息创建全宽容器以确保居中对齐
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        
        // 使容器透明且无边框
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        
        // 将消息气泡移入此容器
        lv_obj_set_parent(msg_bubble, container);
        
        // 将气泡居中对齐在容器中
        lv_obj_align(msg_bubble, LV_ALIGN_CENTER, 0, 0);
        
        // 自动滚动底部
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else {
        // For assistant messages
        // Left align assistant messages
        lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);

        // Auto-scroll to the message bubble
        lv_obj_scroll_to_view_recursive(msg_bubble, LV_ANIM_ON);
    }
    
    // Store reference to the latest message label
    chat_message_label_ = msg_text;
}

void LcdDisplay::ShowTimeAndDate() {
    DisplayLockGuard lock(this);
    
    if (welcome_container_ == nullptr) {
        return;
    }
    
    // 获取当前时间和日期
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    // 格式化小时和分钟
    char hour_str[3];
    char min_str[3];
    snprintf(hour_str, sizeof(hour_str), "%02d", timeinfo->tm_hour);
    snprintf(min_str, sizeof(min_str), "%02d", timeinfo->tm_min);
    
    // 更新小时和分钟标签
    if (hour_label_ != nullptr) {
        lv_label_set_text(hour_label_, hour_str);
    }
    
    if (minute_label_ != nullptr) {
        lv_label_set_text(minute_label_, min_str);
    }
    
    // 控制冒号的可见性
    if (colon_label_ != nullptr) {
        if (colon_visible) {
            // 显示冒号
            lv_obj_clear_flag(colon_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            // 隐藏冒号
            lv_obj_add_flag(colon_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    // 获取日期和星期
    int day = timeinfo->tm_mday;
    const char* weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    const char* weekday = weekdays[timeinfo->tm_wday];
    
    // 格式化日期为 "23 周日" 格式
    char date_str[20];
    snprintf(date_str, sizeof(date_str), "%d %s", day, weekday);
    
    // 查找日期标签
    lv_obj_t* date_label = NULL;
    
    // 遍历welcome_container_的子对象，找到日期标签
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(welcome_container_); i++) {
        lv_obj_t* child = lv_obj_get_child(welcome_container_, i);
        if (child != NULL && child != hour_label_ && child != colon_label_ && child != minute_label_ &&
            child != welcome_battery_label_ && child != welcome_network_label_) {
            // 假设第一个非时间标签、非图标的标签是日期标签
            if (lv_obj_check_type(child, &lv_label_class)) {
                date_label = child;
                break;
            }
        }
    }
    
    // 更新日期标签（只在日期变化时更新）
    static int last_day = -1;
    static int last_wday = -1;
    
    if (date_label != NULL && (day != last_day || timeinfo->tm_wday != last_wday || last_day == -1)) {
        lv_label_set_text(date_label, date_str);
        last_day = day;
        last_wday = timeinfo->tm_wday;
    }
    
    // 同步欢迎界面上的电池和网络图标
    if (welcome_battery_label_ != nullptr && battery_label_ != nullptr) {
        const char* battery_text = lv_label_get_text(battery_label_);
        if (battery_text && strlen(battery_text) > 0) {
            lv_label_set_text(welcome_battery_label_, battery_text);
        }
    }
    
    if (welcome_network_label_ != nullptr && network_label_ != nullptr) {
        const char* network_text = lv_label_get_text(network_label_);
        if (network_text && strlen(network_text) > 0) {
            lv_label_set_text(welcome_network_label_, network_text);
        }
    }
    
    // 同步欢迎界面上的静音图标
    if (welcome_mute_label_ != nullptr && mute_label_ != nullptr) {
        const char* mute_text = lv_label_get_text(mute_label_);
        if (mute_text) {
            lv_label_set_text(welcome_mute_label_, mute_text);
        }
    }
}

void LcdDisplay::UpdateBatteryIcon(const char* icon) {
    DisplayLockGuard lock(this);
    
    // 更新状态栏中的电池图标
    if (battery_label_ != nullptr) {
        lv_label_set_text(battery_label_, icon);
    }
    
    // 同时更新欢迎界面中的电池图标
    if (welcome_battery_label_ != nullptr) {
        lv_label_set_text(welcome_battery_label_, icon);
    }
}

void LcdDisplay::UpdateNetworkIcon(const char* icon) {
    DisplayLockGuard lock(this);
    
    // 更新状态栏中的网络图标
    if (network_label_ != nullptr) {
        lv_label_set_text(network_label_, icon);
    }
    
    // 同时更新欢迎界面中的网络图标
    if (welcome_network_label_ != nullptr) {
        lv_label_set_text(welcome_network_label_, icon);
    }
}

void LcdDisplay::ChangeWallpaper(const char* direction) {
    DisplayLockGuard lock(this);

    if (bg_img_ == nullptr) {
        ESP_LOGE(TAG, "Background image not initialized");
        return;
    }

    if (strcmp(direction, "next") == 0) {
        current_wallpaper_index_ = (current_wallpaper_index_ + 1) % WALLPAPER_COUNT;
    } else if (strcmp(direction, "previous") == 0) {
        current_wallpaper_index_ = (current_wallpaper_index_ - 1 + WALLPAPER_COUNT) % WALLPAPER_COUNT;
    }

    ESP_LOGI(TAG, "Changing wallpaper to index %d", current_wallpaper_index_);
    
    // 设置新壁纸
    lv_img_set_src(bg_img_, wallpapers[current_wallpaper_index_]);

    // 保存当前壁纸设置到NVS
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_u8(handle, "wallpaper", current_wallpaper_index_);
        nvs_commit(handle);
        nvs_close(handle);
    }
}
#else
void LcdDisplay::SetupUI() {
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);
    lv_obj_set_style_text_color(screen, current_theme.text, 0);
    lv_obj_set_style_bg_color(screen, current_theme.background, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, current_theme.background, 0);
    lv_obj_set_style_border_color(container_, current_theme.border, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, fonts_.text_font->line_height);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, current_theme.background, 0);
    lv_obj_set_style_text_color(status_bar_, current_theme.text, 0);
    
    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 5, 0);
    lv_obj_set_style_bg_color(content_, current_theme.chat_background, 0);
    lv_obj_set_style_border_color(content_, current_theme.border, 0); // Border color for content

    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN); // 垂直布局（从上到下）
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY); // 子对象居中对齐，等距分布

    emotion_label_ = lv_label_create(content_);
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_4, 0);
    lv_obj_set_style_text_color(emotion_label_, current_theme.text, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);

    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9); // 限制宽度为屏幕宽度的 90%
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP); // 设置为自动换行模式
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0); // 设置文本居中对齐
    lv_obj_set_style_text_color(chat_message_label_, current_theme.text, 0);

    /* Status bar */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 2, 0);
    lv_obj_set_style_pad_right(status_bar_, 2, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(network_label_, current_theme.text, 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, current_theme.text, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, current_theme.text, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, current_theme.text, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, current_theme.text, 0);

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, current_theme.low_battery, 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    lv_obj_t* low_battery_label = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label, lv_color_white(), 0);
    lv_obj_center(low_battery_label);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

	    // 创建欢迎容器 - 直接放在屏幕上，而不是container_中
    welcome_container_ = lv_obj_create(screen);
    lv_obj_set_style_radius(welcome_container_, 0, 0);
    lv_obj_set_size(welcome_container_, LV_HOR_RES, LV_VER_RES); // 设置为全屏大小
    lv_obj_set_style_pad_all(welcome_container_, 0, 0);
    lv_obj_set_style_border_width(welcome_container_, 0, 0); // 移除边框
    
    // 创建背景图片
    bg_img_ = lv_img_create(welcome_container_);
    
    // 从NVS读取上次保存的壁纸设置
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        uint8_t saved_index;
        err = nvs_get_u8(handle, "wallpaper", &saved_index);
        if (err == ESP_OK) {
            current_wallpaper_index_ = saved_index;
        } else {
            current_wallpaper_index_ = 0; // 如果没有保存的设置，使用第一个壁纸
        }
        nvs_close(handle);
    }

    // 设置壁纸
    if (current_wallpaper_index_ >= WALLPAPER_COUNT) {
        current_wallpaper_index_ = 0;
    }
    lv_img_set_src(bg_img_, wallpapers[current_wallpaper_index_]);
    lv_obj_set_size(bg_img_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(bg_img_);
    lv_obj_set_style_img_recolor_opa(bg_img_, 50, 0);
    lv_obj_set_style_img_recolor(bg_img_, lv_color_black(), 0);
    
    // 获取当前时间和日期
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    // 获取日期和星期
    int day = timeinfo->tm_mday;
    const char* weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    const char* weekday = weekdays[timeinfo->tm_wday];
    
    // 格式化日期为 "23 周日" 格式
    char date_str[20];
    snprintf(date_str, sizeof(date_str), "%d %s", day, weekday);
    
    // 创建日期标签 - 使用丁丁字体
    lv_obj_t* date_label = lv_label_create(welcome_container_);
    lv_obj_set_style_text_font(date_label, &font_dingding, 0);
    lv_obj_set_style_text_color(date_label, lv_color_white(), 0);
    lv_label_set_text(date_label, date_str);
    lv_obj_align(date_label, LV_ALIGN_TOP_RIGHT, -10, 90);
    
    // 创建分离的时间标签
    hour_label_ = lv_label_create(welcome_container_);
    colon_label_ = lv_label_create(welcome_container_);
    minute_label_ = lv_label_create(welcome_container_);
    
    // 设置字体和颜色
    lv_obj_set_style_text_font(hour_label_, &font_time, 0);
    lv_obj_set_style_text_font(colon_label_, &font_time, 0);
    lv_obj_set_style_text_font(minute_label_, &font_time, 0);
    lv_obj_set_style_text_color(hour_label_, lv_color_white(), 0);
    lv_obj_set_style_text_color(colon_label_, lv_color_white(), 0);
    lv_obj_set_style_text_color(minute_label_, lv_color_white(), 0);
    
    // 设置初始文本
    lv_label_set_text(hour_label_, "00");
    lv_label_set_text(colon_label_, ":");
    lv_label_set_text(minute_label_, "00");
    
    // 禁用标签的自动换行
    lv_label_set_long_mode(hour_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(colon_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(minute_label_, LV_LABEL_LONG_CLIP);
    
    // 设置固定宽度 - 为40号字体设置更大的宽度
    lv_coord_t hour_width = 60;  // 为40号字体增加宽度
    lv_coord_t minute_width = 60; // 为40号字体增加宽度
    lv_obj_set_width(hour_label_, hour_width);
    lv_obj_set_width(minute_label_, minute_width);
    
    // 设置文本对齐方式为居中
    lv_obj_set_style_text_align(hour_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(minute_label_, LV_TEXT_ALIGN_CENTER, 0);
    
    // 获取冒号的实际宽度
    lv_obj_update_layout(colon_label_);
    lv_coord_t colon_width = lv_obj_get_width(colon_label_);
    
    // 定义标签之间的固定间距
    lv_coord_t spacing = 2; // 为40号字体增加间距
    
    // 计算总宽度
    lv_coord_t total_width = hour_width + spacing + colon_width + spacing + minute_width;
    
    // 计算起始x坐标（从右边缘向左偏移）
    lv_coord_t right_margin = 5; // 右边距
    lv_coord_t start_x = LV_HOR_RES - right_margin - total_width;
    
    // 计算时间标签的垂直位置 - 在日期下方，电池图标上方
    lv_coord_t time_y = 40; // 距离顶部40像素，可以根据需要调整
    
    // 设置每个标签的精确位置
    lv_obj_set_pos(hour_label_, LV_HOR_RES - hour_width - spacing - colon_width - spacing - minute_width - 10, time_y);
    lv_obj_set_pos(colon_label_, LV_HOR_RES - colon_width - spacing - minute_width - 10, time_y);
    lv_obj_set_pos(minute_label_, LV_HOR_RES - minute_width - 10, time_y);
    
    // 创建欢迎界面上的电池图标
    lv_obj_t* welcome_battery_label = lv_label_create(welcome_container_);
    lv_obj_set_style_text_font(welcome_battery_label, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(welcome_battery_label, lv_color_white(), 0);
    lv_label_set_text(welcome_battery_label, "");
    lv_obj_align(welcome_battery_label, LV_ALIGN_TOP_RIGHT, -10, 5);
    
    // 创建欢迎界面上的WiFi图标
    lv_obj_t* welcome_network_label = lv_label_create(welcome_container_);
    lv_obj_set_style_text_font(welcome_network_label, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(welcome_network_label, lv_color_white(), 0);
    lv_label_set_text(welcome_network_label, "");
    lv_obj_align(welcome_network_label, LV_ALIGN_TOP_RIGHT, -40, 5);
    
    // 创建欢迎界面上的静音图标
    lv_obj_t* welcome_mute_label = lv_label_create(welcome_container_);
    lv_obj_set_style_text_font(welcome_mute_label, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(welcome_mute_label, lv_color_white(), 0);
    lv_label_set_text(welcome_mute_label, "");
    lv_obj_align(welcome_mute_label, LV_ALIGN_TOP_RIGHT, -70, 5);
    
    // 保存这些标签的引用，以便稍后更新
    welcome_battery_label_ = welcome_battery_label;
    welcome_network_label_ = welcome_network_label;
    welcome_mute_label_ = welcome_mute_label;
    
    // 初始时显示欢迎界面，隐藏聊天界面和状态栏
    lv_obj_clear_flag(welcome_container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN); // 隐藏整个container_，包括状态栏和聊天区域
    
    // 初始化冒号闪烁定时器
    esp_timer_handle_t colon_timer;
    esp_timer_create_args_t timer_args = {
        .callback = &colon_blink_timer_callback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "colon_blink_timer"
    };
    
    esp_timer_create(&timer_args, &colon_timer);
    esp_timer_start_periodic(colon_timer, 1000000); // 1秒 = 1000000微秒

    // 强制立即更新一次时间显示，确保初始显示正确
    ShowTimeAndDate();
}
void LcdDisplay::ShowTimeAndDate() {
    DisplayLockGuard lock(this);
    
    if (welcome_container_ == nullptr) {
        return;
    }
    
    // 获取当前时间和日期
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    // 格式化小时和分钟
    char hour_str[3];
    char min_str[3];
    snprintf(hour_str, sizeof(hour_str), "%02d", timeinfo->tm_hour);
    snprintf(min_str, sizeof(min_str), "%02d", timeinfo->tm_min);
    
    // 更新小时和分钟标签
    if (hour_label_ != nullptr) {
        lv_label_set_text(hour_label_, hour_str);
    }
    
    if (minute_label_ != nullptr) {
        lv_label_set_text(minute_label_, min_str);
    }
    
    // 控制冒号的可见性
    if (colon_label_ != nullptr) {
        if (colon_visible) {
            // 显示冒号
            lv_obj_clear_flag(colon_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            // 隐藏冒号
            lv_obj_add_flag(colon_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    // 获取日期和星期
    int day = timeinfo->tm_mday;
    const char* weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    const char* weekday = weekdays[timeinfo->tm_wday];
    
    // 格式化日期为 "23 周日" 格式
    char date_str[20];
    snprintf(date_str, sizeof(date_str), "%d %s", day, weekday);
    
    // 查找日期标签
    lv_obj_t* date_label = NULL;
    
    // 遍历welcome_container_的子对象，找到日期标签
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(welcome_container_); i++) {
        lv_obj_t* child = lv_obj_get_child(welcome_container_, i);
        if (child != NULL && child != hour_label_ && child != colon_label_ && child != minute_label_ &&
            child != welcome_battery_label_ && child != welcome_network_label_) {
            // 假设第一个非时间标签、非图标的标签是日期标签
            if (lv_obj_check_type(child, &lv_label_class)) {
                date_label = child;
                break;
            }
        }
    }
    
    // 更新日期标签（只在日期变化时更新）
    static int last_day = -1;
    static int last_wday = -1;
    
    if (date_label != NULL && (day != last_day || timeinfo->tm_wday != last_wday || last_day == -1)) {
        lv_label_set_text(date_label, date_str);
        last_day = day;
        last_wday = timeinfo->tm_wday;
    }
    
    // 同步欢迎界面上的电池和网络图标
    if (welcome_battery_label_ != nullptr && battery_label_ != nullptr) {
        const char* battery_text = lv_label_get_text(battery_label_);
        if (battery_text && strlen(battery_text) > 0) {
            lv_label_set_text(welcome_battery_label_, battery_text);
        }
    }
    
    if (welcome_network_label_ != nullptr && network_label_ != nullptr) {
        const char* network_text = lv_label_get_text(network_label_);
        if (network_text && strlen(network_text) > 0) {
            lv_label_set_text(welcome_network_label_, network_text);
        }
    }
    
    // 同步欢迎界面上的静音图标
    if (welcome_mute_label_ != nullptr && mute_label_ != nullptr) {
        const char* mute_text = lv_label_get_text(mute_label_);
        if (mute_text) {
            lv_label_set_text(welcome_mute_label_, mute_text);
        }
    }
}

void LcdDisplay::UpdateBatteryIcon(const char* icon) {
    DisplayLockGuard lock(this);
    
    // 更新状态栏中的电池图标
    if (battery_label_ != nullptr) {
        lv_label_set_text(battery_label_, icon);
    }
    
    // 同时更新欢迎界面中的电池图标
    if (welcome_battery_label_ != nullptr) {
        lv_label_set_text(welcome_battery_label_, icon);
    }
}

void LcdDisplay::UpdateNetworkIcon(const char* icon) {
    DisplayLockGuard lock(this);
    
    // 更新状态栏中的网络图标
    if (network_label_ != nullptr) {
        lv_label_set_text(network_label_, icon);
    }
    
    // 同时更新欢迎界面中的网络图标
    if (welcome_network_label_ != nullptr) {
        lv_label_set_text(welcome_network_label_, icon);
    }
}

void LcdDisplay::ChangeWallpaper(const char* direction) {
    DisplayLockGuard lock(this);

    if (bg_img_ == nullptr) {
        ESP_LOGE(TAG, "Background image not initialized");
        return;
    }

    if (strcmp(direction, "next") == 0) {
        current_wallpaper_index_ = (current_wallpaper_index_ + 1) % WALLPAPER_COUNT;
    } else if (strcmp(direction, "previous") == 0) {
        current_wallpaper_index_ = (current_wallpaper_index_ - 1 + WALLPAPER_COUNT) % WALLPAPER_COUNT;
    }

    ESP_LOGI(TAG, "Changing wallpaper to index %d", current_wallpaper_index_);
    
    // 设置新壁纸
    lv_img_set_src(bg_img_, wallpapers[current_wallpaper_index_]);

    // 保存当前壁纸设置到NVS
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_u8(handle, "wallpaper", current_wallpaper_index_);
        nvs_commit(handle);
        nvs_close(handle);
    }
}
#endif

void LcdDisplay::SetEmotion(const char* emotion) {
    struct Emotion {
        const char* icon;
        const char* text;
    };

    static const std::vector<Emotion> emotions = {
        {"😶", "neutral"},
        {"🙂", "happy"},
        {"😆", "laughing"},
        {"😂", "funny"},
        {"😔", "sad"},
        {"😠", "angry"},
        {"😭", "crying"},
        {"😍", "loving"},
        {"😳", "embarrassed"},
        {"😯", "surprised"},
        {"😱", "shocked"},
        {"🤔", "thinking"},
        {"😉", "winking"},
        {"😎", "cool"},
        {"😌", "relaxed"},
        {"🤤", "delicious"},
        {"😘", "kissy"},
        {"😏", "confident"},
        {"😴", "sleepy"},
        {"😜", "silly"},
        {"🙄", "confused"}
    };
    
    // 查找匹配的表情
    std::string_view emotion_view(emotion);
    auto it = std::find_if(emotions.begin(), emotions.end(),
        [&emotion_view](const Emotion& e) { return e.text == emotion_view; });

    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }

    // 如果找到匹配的表情就显示对应图标，否则显示默认的neutral表情
    lv_obj_set_style_text_font(emotion_label_, fonts_.emoji_font, 0);
    if (it != emotions.end()) {
        lv_label_set_text(emotion_label_, it->icon);
    } else {
        lv_label_set_text(emotion_label_, "😶");
    }
}

void LcdDisplay::SetIcon(const char* icon) {
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_4, 0);
    lv_label_set_text(emotion_label_, icon);
}

void LcdDisplay::SetTheme(const std::string& theme_name) {
    DisplayLockGuard lock(this);
    
    if (theme_name == "dark" || theme_name == "DARK") {
        current_theme = DARK_THEME;
    } else if (theme_name == "light" || theme_name == "LIGHT") {
        current_theme = LIGHT_THEME;
    } else {
        // Invalid theme name, return false
        ESP_LOGE(TAG, "Invalid theme name: %s", theme_name.c_str());
        return;
    }
    
    // Get the active screen
    lv_obj_t* screen = lv_screen_active();
    
    // Update the screen colors
    lv_obj_set_style_bg_color(screen, current_theme.background, 0);
    lv_obj_set_style_text_color(screen, current_theme.text, 0);
    
    // Update container colors
    if (container_ != nullptr) {
        lv_obj_set_style_bg_color(container_, current_theme.background, 0);
        lv_obj_set_style_border_color(container_, current_theme.border, 0);
    }
    
    // Update status bar colors
    if (status_bar_ != nullptr) {
        lv_obj_set_style_bg_color(status_bar_, current_theme.background, 0);
        lv_obj_set_style_text_color(status_bar_, current_theme.text, 0);
        
        // Update status bar elements
        if (network_label_ != nullptr) {
            lv_obj_set_style_text_color(network_label_, current_theme.text, 0);
        }
        if (status_label_ != nullptr) {
            lv_obj_set_style_text_color(status_label_, current_theme.text, 0);
        }
        if (notification_label_ != nullptr) {
            lv_obj_set_style_text_color(notification_label_, current_theme.text, 0);
        }
        if (mute_label_ != nullptr) {
            lv_obj_set_style_text_color(mute_label_, current_theme.text, 0);
        }
        if (battery_label_ != nullptr) {
            lv_obj_set_style_text_color(battery_label_, current_theme.text, 0);
        }
        if (emotion_label_ != nullptr) {
            lv_obj_set_style_text_color(emotion_label_, current_theme.text, 0);
        }
    }
    
    // Update content area colors
    if (content_ != nullptr) {
        lv_obj_set_style_bg_color(content_, current_theme.chat_background, 0);
        lv_obj_set_style_border_color(content_, current_theme.border, 0);
        
        // If we have the chat message style, update all message bubbles
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
        // Iterate through all children of content (message containers or bubbles)
        uint32_t child_count = lv_obj_get_child_cnt(content_);
        for (uint32_t i = 0; i < child_count; i++) {
            lv_obj_t* obj = lv_obj_get_child(content_, i);
            if (obj == nullptr) continue;
            
            lv_obj_t* bubble = nullptr;
            
            // 检查这个对象是容器还是气泡
            // 如果是容器（用户或系统消息），则获取其子对象作为气泡
            // 如果是气泡（助手消息），则直接使用
            if (lv_obj_get_child_cnt(obj) > 0) {
                // 可能是容器，检查它是否为用户或系统消息容器
                // 用户和系统消息容器是透明的
                lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, 0);
                if (bg_opa == LV_OPA_TRANSP) {
                    // 这是用户或系统消息的容器
                    bubble = lv_obj_get_child(obj, 0);
                } else {
                    // 这可能是助手消息的气泡自身
                    bubble = obj;
                }
            } else {
                // 没有子元素，可能是其他UI元素，跳过
                continue;
            }
            
            if (bubble == nullptr) continue;
            
            // 使用保存的用户数据来识别气泡类型
            void* bubble_type_ptr = lv_obj_get_user_data(bubble);
            if (bubble_type_ptr != nullptr) {
                const char* bubble_type = static_cast<const char*>(bubble_type_ptr);
                
                // 根据气泡类型应用正确的颜色
                if (strcmp(bubble_type, "user") == 0) {
                    lv_obj_set_style_bg_color(bubble, current_theme.user_bubble, 0);
                } else if (strcmp(bubble_type, "assistant") == 0) {
                    lv_obj_set_style_bg_color(bubble, current_theme.assistant_bubble, 0); 
                } else if (strcmp(bubble_type, "system") == 0) {
                    lv_obj_set_style_bg_color(bubble, current_theme.system_bubble, 0);
                }
                
                // Update border color
                lv_obj_set_style_border_color(bubble, current_theme.border, 0);
                
                // Update text color for the message
                if (lv_obj_get_child_cnt(bubble) > 0) {
                    lv_obj_t* text = lv_obj_get_child(bubble, 0);
                    if (text != nullptr) {
                        // 根据气泡类型设置文本颜色
                        if (strcmp(bubble_type, "system") == 0) {
                            lv_obj_set_style_text_color(text, current_theme.system_text, 0);
                        } else {
                            lv_obj_set_style_text_color(text, current_theme.text, 0);
                        }
                    }
                }
            } else {
                // 如果没有标记，回退到之前的逻辑（颜色比较）
                // ...保留原有的回退逻辑...
                lv_color_t bg_color = lv_obj_get_style_bg_color(bubble, 0);
            
                // 改进bubble类型检测逻辑，不仅使用颜色比较
                bool is_user_bubble = false;
                bool is_assistant_bubble = false;
                bool is_system_bubble = false;
            
                // 检查用户bubble
                if (lv_color_eq(bg_color, DARK_USER_BUBBLE_COLOR) || 
                    lv_color_eq(bg_color, LIGHT_USER_BUBBLE_COLOR) ||
                    lv_color_eq(bg_color, current_theme.user_bubble)) {
                    is_user_bubble = true;
                }
                // 检查系统bubble
                else if (lv_color_eq(bg_color, DARK_SYSTEM_BUBBLE_COLOR) || 
                         lv_color_eq(bg_color, LIGHT_SYSTEM_BUBBLE_COLOR) ||
                         lv_color_eq(bg_color, current_theme.system_bubble)) {
                    is_system_bubble = true;
                }
                // 剩余的都当作助手bubble处理
                else {
                    is_assistant_bubble = true;
                }
            
                // 根据bubble类型应用正确的颜色
                if (is_user_bubble) {
                    lv_obj_set_style_bg_color(bubble, current_theme.user_bubble, 0);
                } else if (is_assistant_bubble) {
                    lv_obj_set_style_bg_color(bubble, current_theme.assistant_bubble, 0);
                } else if (is_system_bubble) {
                    lv_obj_set_style_bg_color(bubble, current_theme.system_bubble, 0);
                }
                
                // Update border color
                lv_obj_set_style_border_color(bubble, current_theme.border, 0);
                
                // Update text color for the message
                if (lv_obj_get_child_cnt(bubble) > 0) {
                    lv_obj_t* text = lv_obj_get_child(bubble, 0);
                    if (text != nullptr) {
                        // 回退到颜色检测逻辑
                        if (lv_color_eq(bg_color, current_theme.system_bubble) ||
                            lv_color_eq(bg_color, DARK_SYSTEM_BUBBLE_COLOR) || 
                            lv_color_eq(bg_color, LIGHT_SYSTEM_BUBBLE_COLOR)) {
                            lv_obj_set_style_text_color(text, current_theme.system_text, 0);
                        } else {
                            lv_obj_set_style_text_color(text, current_theme.text, 0);
                        }
                    }
                }
            }
        }
#else
        // Simple UI mode - just update the main chat message
        if (chat_message_label_ != nullptr) {
            lv_obj_set_style_text_color(chat_message_label_, current_theme.text, 0);
        }
        
        if (emotion_label_ != nullptr) {
            lv_obj_set_style_text_color(emotion_label_, current_theme.text, 0);
        }
#endif
    }
    
    // Update low battery popup
    if (low_battery_popup_ != nullptr) {
        lv_obj_set_style_bg_color(low_battery_popup_, current_theme.low_battery, 0);
    }

    // No errors occurred. Save theme to settings
    Display::SetTheme(theme_name);
}

// 将SetChatMessage函数移出条件编译块，使其在所有模式下都可用
void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    
    // 如果是系统消息且内容只有空格，这是我们用来切换界面的特殊情况
    bool is_switch_trigger = (strcmp(role, "system") == 0 && content && strlen(content) == 1 && content[0] == ' ');
    
    // 如果有消息内容或是切换触发器，隐藏欢迎界面，显示聊天界面
    if ((content && strlen(content) > 0) || is_switch_trigger) {
        if (welcome_container_ != nullptr) {
            lv_obj_add_flag(welcome_container_, LV_OBJ_FLAG_HIDDEN);
        }
        if (container_ != nullptr) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
        if (content_ != nullptr) {
            lv_obj_clear_flag(content_, LV_OBJ_FLAG_HIDDEN);
        }
        
        // 如果只是切换触发器，不创建消息气泡
        if (is_switch_trigger) {
            return;
        }
    } else {
        // 如果没有消息内容，显示欢迎界面，隐藏聊天界面
        if (welcome_container_ != nullptr) {
            lv_obj_clear_flag(welcome_container_, LV_OBJ_FLAG_HIDDEN);
        }
        if (container_ != nullptr) {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
        return; // 避免创建空消息框
    }
}