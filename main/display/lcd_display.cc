#include "lcd_display.h"
#include "gif/lvgl_gif.h"
#include "settings.h"
#include "lvgl_theme.h"
#include "assets/lang_config.h"

#include <lvgl.h>

#include <vector>
#include <algorithm>
#include <font_awesome.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <cstring>
#include <dirent.h>

#include "board.h"
#include "application.h"
#include "device_state.h"
#include "audio/audio_codec.h"
#include "music.h"
#include "boards/common/esp32_music.h"
extern "C" {
    void* clock_face_create(void* parent, int width, int height);
    void clock_face_show(void* ctx);
    void clock_face_hide(void* ctx);
    bool clock_face_load_background(void* ctx, const char* asset_name);
    void clock_face_set_loop(void* ctx, int loop_mode);
}

#define TAG "LcdDisplay"


static bool IsSdMounted() {
    DIR* root = opendir("/sdcard");
    if (root) { closedir(root); return true; }
    return false;
}


LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_4);

// 统一的表情显隐逻辑：仅在聊天态（聆听/说话）显示
bool LcdDisplay::IsPreviewVisible() const {
    return preview_image_ && !lv_obj_has_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
}

bool LcdDisplay::ShouldShowEmojis() const {
    // 规则：播放器显示/时钟显示/预览显示 任一为真 -> 不显示表情
    if (music_player_ui_ && music_player_ui_->IsVisible()) return false;
    if (clock_visible_) return false;
    if (IsPreviewVisible()) return false;
    // 其他情况（聊天态）允许显示
    return true;
}

void LcdDisplay::ApplyEmojiVisibility() {
    // 锁内做显隐，防止撕裂
    DisplayLockGuard lock(this);
    bool should_show = ShouldShowEmojis();
    if (emoji_box_) {
        if (should_show) {
            lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
            if (gif_controller_) gif_controller_->Start();
        } else {
            if (gif_controller_) gif_controller_->Stop();
            lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void LcdDisplay::InitializeLcdThemes() {
    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_awesome_30_4);

    // light theme
    auto light_theme = new LvglTheme("light");
    light_theme->set_background_color(lv_color_hex(0xFFFFFF));          //rgb(255, 255, 255)
    light_theme->set_text_color(lv_color_hex(0x000000));                //rgb(0, 0, 0)
    light_theme->set_chat_background_color(lv_color_hex(0xE0E0E0));     //rgb(224, 224, 224)
    light_theme->set_user_bubble_color(lv_color_hex(0x00FF00));         //rgb(0, 128, 0)
    light_theme->set_assistant_bubble_color(lv_color_hex(0xDDDDDD));    //rgb(221, 221, 221)
    light_theme->set_system_bubble_color(lv_color_hex(0xFFFFFF));       //rgb(255, 255, 255)
    light_theme->set_system_text_color(lv_color_hex(0x000000));         //rgb(0, 0, 0)
    light_theme->set_border_color(lv_color_hex(0x000000));              //rgb(0, 0, 0)
    light_theme->set_low_battery_color(lv_color_hex(0x000000));         //rgb(0, 0, 0)
    light_theme->set_text_font(text_font);
    light_theme->set_icon_font(icon_font);
    light_theme->set_large_icon_font(large_icon_font);

    // dark theme
    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_background_color(lv_color_hex(0x000000));           //rgb(0, 0, 0)
    dark_theme->set_text_color(lv_color_hex(0xFFFFFF));                 //rgb(255, 255, 255)
    dark_theme->set_chat_background_color(lv_color_hex(0x1F1F1F));      //rgb(31, 31, 31)
    dark_theme->set_user_bubble_color(lv_color_hex(0x00FF00));          //rgb(0, 128, 0)
    dark_theme->set_assistant_bubble_color(lv_color_hex(0x222222));     //rgb(34, 34, 34)
    dark_theme->set_system_bubble_color(lv_color_hex(0x000000));        //rgb(0, 0, 0)
    dark_theme->set_system_text_color(lv_color_hex(0xFFFFFF));          //rgb(255, 255, 255)
    dark_theme->set_border_color(lv_color_hex(0xFFFFFF));               //rgb(255, 255, 255)
    dark_theme->set_low_battery_color(lv_color_hex(0xFF0000));          //rgb(255, 0, 0)
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("light", light_theme);
    theme_manager.RegisterTheme("dark", dark_theme);
}

LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    // Initialize LCD themes
    InitializeLcdThemes();

    // Load theme from settings
    Settings settings("display", false);
    std::string theme_name = settings.GetString("theme", "light");
    current_theme_ = LvglThemeManager::GetInstance().GetTheme(theme_name);

    // Create a timer to hide the preview image
    esp_timer_create_args_t preview_timer_args = {
        .callback = [](void* arg) {
            LcdDisplay* display = static_cast<LcdDisplay*>(arg);
            display->SetPreviewImage(nullptr);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "preview_timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&preview_timer_args, &preview_timer_);
}

SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {

    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

#if CONFIG_SPIRAM
    // lv image cache, currently only PNG is supported
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
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

    SetupUI();
}

// RGB LCD实现
RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y,
                           bool mirror_x, bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {

    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
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

    SetupUI();
}

MipiLcdDisplay::MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                            int width, int height,  int offset_x, int offset_y,
                            bool mirror_x, bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 50),
        .double_buffer = false,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram =false,
            .sw_rotate = false,
        },
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {
        .flags = {
            .avoid_tearing = false,
        }
    };
    display_ = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    SetupUI();
}

LcdDisplay::~LcdDisplay() {
    SetPreviewImage(nullptr);
    
    // Clean up GIF controller
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
        esp_timer_delete(preview_timer_);
    }
    

    if (preview_image_ != nullptr) {
        lv_obj_del(preview_image_);
    }
    if (chat_message_label_ != nullptr) {
        lv_obj_del(chat_message_label_);
    }
    if (emoji_label_ != nullptr) {
        lv_obj_del(emoji_label_);
    }
    if (emoji_image_ != nullptr) {
        lv_obj_del(emoji_image_);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_del(emoji_box_);
    }
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

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);
    // 全局触摸唤醒：在屏幕层监听触摸事件，任何界面触摸都唤醒
    lv_obj_add_event_cb(screen, [](lv_event_t* e){
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
            Board::GetInstance().SetPowerSaveMode(false);
        }
    }, LV_EVENT_ALL, nullptr);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_color(container_, lvgl_theme->border_color(), 0);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* Status bar */
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // 全透明，让播放器背景透过来
    lv_obj_set_style_text_color(status_bar_, lvgl_theme->text_color(), 0);
    lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_EVENT_BUBBLE);
    
    /* Content - Chat area */
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_bg_color(content_, lvgl_theme->chat_background_color(), 0); // Background for chat area
    lv_obj_add_flag(content_, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Enable scrolling for chat content
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    
    // Create a flex container for chat messages
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content_, lvgl_theme->spacing(4), 0); // Space between messages

    // We'll create chat messages dynamically in SetChatMessage
    chat_message_label_ = nullptr;

    /* Status bar */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_left(status_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(status_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    // 设置状态栏的内容垂直居中
    lv_obj_set_flex_align(status_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    
    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_margin_left(battery_label_, lvgl_theme->spacing(2), 0); // 添加左边距，与前面的元素分隔

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    emoji_image_ = lv_img_create(screen);
    lv_obj_align(emoji_image_, LV_ALIGN_TOP_MID, 0, text_font->line_height + lvgl_theme->spacing(8));
    lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Display AI logo while booting
    emoji_label_ = lv_label_create(screen);
    lv_obj_center(emoji_label_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, FONT_AWESOME_MICROCHIP_AI);
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_EVENT_BUBBLE);
}
#if CONFIG_IDF_TARGET_ESP32P4
#define  MAX_MESSAGES 40
#else
#define  MAX_MESSAGES 20
#endif
void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        return;
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
    
    // 折叠系统消息（如果是系统消息，检查最后一个消息是否也是系统消息）
    if (strcmp(role, "system") == 0) {
        if (child_count > 0) {
            // 获取最后一个消息容器
            lv_obj_t* last_container = lv_obj_get_child(content_, child_count - 1);
            if (last_container != nullptr && lv_obj_get_child_cnt(last_container) > 0) {
                // 获取容器内的气泡
                lv_obj_t* last_bubble = lv_obj_get_child(last_container, 0);
                if (last_bubble != nullptr) {
                    // 检查气泡类型是否为系统消息
                    void* bubble_type_ptr = lv_obj_get_user_data(last_bubble);
                    if (bubble_type_ptr != nullptr && strcmp((const char*)bubble_type_ptr, "system") == 0) {
                        // 如果最后一个消息也是系统消息，则删除它
                        lv_obj_del(last_container);
                    }
                }
            }
        }
    } else {
        // 隐藏居中显示的 AI logo
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }

    //避免出现空的消息框
    if(strlen(content) == 0) {
        return;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();

    // Create a message bubble
    lv_obj_t* msg_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(msg_bubble, 0, 0);
    lv_obj_set_style_pad_all(msg_bubble, lvgl_theme->spacing(4), 0);

    // Create the message text
    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_label_set_text(msg_text, content);
    
    // 计算文本实际宽度
    lv_coord_t text_width = lv_txt_get_width(content, strlen(content), text_font, 0);

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

    // 设置气泡宽度
    lv_obj_set_width(msg_bubble, bubble_width);
    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

    // Set alignment and style based on message role
    if (strcmp(role, "user") == 0) {
        // User messages are right-aligned with green background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->user_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->text_color(), 0);
        
        // 设置自定义属性标记气泡类型
        lv_obj_set_user_data(msg_bubble, (void*)"user");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "assistant") == 0) {
        // Assistant messages are left-aligned with white background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->assistant_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->text_color(), 0);
        
        // 设置自定义属性标记气泡类型
        lv_obj_set_user_data(msg_bubble, (void*)"assistant");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        // System messages are center-aligned with light gray background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->system_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->system_text_color(), 0);
        
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

void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        return;
    }

    if (image == nullptr) {
        return;
    }
    
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    // Create a message bubble for image preview
    lv_obj_t* img_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(img_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(img_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(img_bubble, 0, 0);
    lv_obj_set_style_pad_all(img_bubble, lvgl_theme->spacing(4), 0);
    
    // Set image bubble background color (similar to system message)
    lv_obj_set_style_bg_color(img_bubble, lvgl_theme->assistant_bubble_color(), 0);
    lv_obj_set_style_bg_opa(img_bubble, LV_OPA_70, 0);
    
    // 设置自定义属性标记气泡类型
    lv_obj_set_user_data(img_bubble, (void*)"image");

    // Create the image object inside the bubble
    lv_obj_t* preview_image = lv_image_create(img_bubble);
    
    // Calculate appropriate size for the image
    lv_coord_t max_width = LV_HOR_RES * 70 / 100;  // 70% of screen width
    lv_coord_t max_height = LV_VER_RES * 50 / 100; // 50% of screen height
    
    // Calculate zoom factor to fit within maximum dimensions
    auto img_dsc = image->image_dsc();
    lv_coord_t img_width = img_dsc->header.w;
    lv_coord_t img_height = img_dsc->header.h;
    if (img_width == 0 || img_height == 0) {
        img_width = max_width;
        img_height = max_height;
        ESP_LOGW(TAG, "Invalid image dimensions: %ld x %ld, using default dimensions: %ld x %ld", img_width, img_height, max_width, max_height);
    }
    
    lv_coord_t zoom_w = (max_width * 256) / img_width;
    lv_coord_t zoom_h = (max_height * 256) / img_height;
    lv_coord_t zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
    
    // Ensure zoom doesn't exceed 256 (100%)
    if (zoom > 256) zoom = 256;
    
    // Set image properties
    lv_image_set_src(preview_image, img_dsc);
    lv_image_set_scale(preview_image, zoom);
    
    // Add event handler to clean up LvglImage when image is deleted
    // We need to transfer ownership of the unique_ptr to the event callback
    LvglImage* raw_image = image.release(); // 释放智能指针的所有权
    lv_obj_add_event_cb(preview_image, [](lv_event_t* e) {
        LvglImage* img = (LvglImage*)lv_event_get_user_data(e);
        if (img != nullptr) {
            delete img; // 通过删除 LvglImage 对象来正确释放内存
        }
    }, LV_EVENT_DELETE, (void*)raw_image);
    
    // Calculate actual scaled image dimensions
    lv_coord_t scaled_width = (img_width * zoom) / 256;
    lv_coord_t scaled_height = (img_height * zoom) / 256;
    
    // Set bubble size to be 16 pixels larger than the image (8 pixels on each side)
    lv_obj_set_width(img_bubble, scaled_width + 16);
    lv_obj_set_height(img_bubble, scaled_height + 16);
    
    // Don't grow in flex layout
    lv_obj_set_style_flex_grow(img_bubble, 0, 0);
    
    // Center the image within the bubble
    lv_obj_center(preview_image);
    
    // Left align the image bubble like assistant messages
    lv_obj_align(img_bubble, LV_ALIGN_LEFT_MID, 0, 0);

    // Auto-scroll to the image bubble
    lv_obj_scroll_to_view_recursive(img_bubble, LV_ANIM_ON);
}
#else
void LcdDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_color(container_, lvgl_theme->border_color(), 0);

    /* Status bar */
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // 全透明，让播放器背景透过来
    lv_obj_set_style_text_color(status_bar_, lvgl_theme->text_color(), 0);
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_left(status_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(status_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    
    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_bg_color(content_, lvgl_theme->chat_background_color(), 0);

    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN); // 垂直布局（从上到下）
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER); // 水平居中，垂直底部对齐

    /* 预览图片 - 居中显示 */
    preview_image_ = lv_image_create(content_);
    lv_obj_set_size(preview_image_, width_ / 2, height_ / 2);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    /* 表情显示区域 - 直接放在屏幕上，居中对齐 */
    emoji_box_ = lv_obj_create(screen);
    lv_obj_set_size(emoji_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(emoji_box_, 0, 0);
    lv_obj_set_style_border_width(emoji_box_, 0, 0);
    // 表情居中对齐
    lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);

    emoji_label_ = lv_label_create(emoji_box_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, FONT_AWESOME_MICROCHIP_AI);
    lv_obj_center(emoji_label_);

    emoji_image_ = lv_img_create(emoji_box_);
    lv_obj_center(emoji_image_);
    lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

    /* 聊天文本标签 - 基于屏幕底部定位，深灰色背景 */
    chat_message_label_ = lv_label_create(screen);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, width_ * 0.9);  // 宽度90%
    // 自适应内容高度，但最大不超过屏幕高度的三分之一
    lv_obj_set_height(chat_message_label_, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(chat_message_label_, height_ / 3, 0);
    // 基于屏幕底部定位，根据屏幕尺寸调整间距
    int bottom_margin = (height_ <= 128) ? -5 : -20;  // 小屏幕用更小的间距
    lv_obj_align(chat_message_label_, LV_ALIGN_BOTTOM_MID, 0, bottom_margin);
    lv_obj_set_style_radius(chat_message_label_, 8, 0);  // 圆角
    lv_obj_set_style_border_width(chat_message_label_, 0, 0);  // 无边框
    lv_obj_set_style_pad_all(chat_message_label_, 8, 0);  // 内边距
    lv_obj_set_style_bg_color(chat_message_label_, lvgl_theme->assistant_bubble_color(), 0);  // 跟随主题背景
    lv_obj_set_style_bg_opa(chat_message_label_, LV_OPA_70, 0);  // 半透明背景
    // 根据屏幕尺寸选择合适的文本显示模式
    if (width_ <= 128 && height_ <= 128) {
        // 小屏幕：使用滚动模式，避免文本过长
        lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    } else {
        // 正常屏幕：使用换行显示
        lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    }
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0); // 居中对齐
    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);  // 初始时隐藏

    /* Status bar - 左侧状态/时间，右侧电池和信号 */
    
    /* 状态标签 - 左侧显示 */
    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_font(status_label_, text_font, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    
    /* 通知标签 - 隐藏，与状态标签重叠 */
    notification_label_ = lv_label_create(status_bar_);
    lv_obj_move_to_index(notification_label_, 0);  // 移动到第一个位置，与status_label_重叠
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_font(notification_label_, text_font, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    /* 中间占位符 - 占用剩余空间 */
    lv_obj_t* center_spacer = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(center_spacer, 1);
    lv_label_set_text(center_spacer, "");

    /* 静音标签 - 右侧第三个 */
    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);

    /* 网络信号标签 - 右侧第二个（电池左侧） */
    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_margin_left(network_label_, 8, 0);  // 与前面元素的间距

    /* 电池标签 - 右侧第一个（最右侧） */
    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_margin_left(battery_label_, 8, 0);  // 与网络图标的间距

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);
    
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}

void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        ESP_LOGE(TAG, "Preview image is not initialized");
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        // 清理 LVGL 源与缓存，确保完全释放
        if (preview_image_) {
            const void* src = lv_image_get_src(preview_image_);
            if (src) lv_image_cache_drop(src);
            lv_image_set_src(preview_image_, NULL);
            lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        }
        preview_image_cached_.reset();
        ApplyEmojiVisibility();
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();
    // 设置图片源并显示预览图片
    lv_image_set_src(preview_image_, img_dsc);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // zoom factor 0.5
        lv_image_set_scale(preview_image_, 128 * width_ / img_dsc->header.w);
    }

    // 预览期间隐藏表情
    if (gif_controller_) gif_controller_->Stop();
    if (emoji_box_) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));
}

void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }
    
    // 设置文本内容
    lv_label_set_text(chat_message_label_, content);
    
    // 根据内容是否为空来显示或隐藏聊天标签
    if (content != nullptr && strlen(content) > 0) {
        lv_obj_remove_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);  // 显示标签
    } else {
        lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);     // 隐藏标签
    }
}
#endif

void LcdDisplay::SetEmotion(const char* emotion) {
    // 仅在允许显示表情时响应（聊天态）
    if (!ShouldShowEmojis()) {
        return;
    }
    // Stop any running GIF animation
    if (gif_controller_) {
        DisplayLockGuard lock(this);
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    
    if (emoji_image_ == nullptr) {
        return;
    }

    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
    auto image = emoji_collection != nullptr ? emoji_collection->GetEmojiImage(emotion) : nullptr;
    if (image == nullptr) {
        const char* utf8 = font_awesome_get_utf8(emotion);
        if (utf8 != nullptr && emoji_label_ != nullptr) {
            DisplayLockGuard lock(this);
            lv_label_set_text(emoji_label_, utf8);
            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    DisplayLockGuard lock(this);
    if (image->IsGif()) {
        // Create new GIF controller
        gif_controller_ = std::make_unique<LvglGif>(image->image_dsc());
        
        if (gif_controller_->IsLoaded()) {
            // Set up frame update callback
            gif_controller_->SetFrameCallback([this]() {
                lv_image_set_src(emoji_image_, gif_controller_->image_dsc());
            });
            
            // Set initial frame and start animation
            lv_image_set_src(emoji_image_, gif_controller_->image_dsc());
            gif_controller_->Start();
            
            // Show GIF, hide others
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Failed to load GIF for emotion: %s", emotion);
            gif_controller_.reset();
        }
    } else {
        lv_image_set_src(emoji_image_, image->image_dsc());
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    // Wechat message style中，如果emotion是neutral，则不显示
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (strcmp(emotion, "neutral") == 0 && child_count > 0) {
        // Stop GIF animation if running
        if (gif_controller_) {
            gif_controller_->Stop();
            gif_controller_.reset();
        }
        
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

void LcdDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    
    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    
    // Get the active screen
    lv_obj_t* screen = lv_screen_active();

    // Set font
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    if (text_font->line_height >= 40) {
        lv_obj_set_style_text_font(mute_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(network_label_, large_icon_font, 0);
    } else {
        lv_obj_set_style_text_font(mute_label_, icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, icon_font, 0);
        lv_obj_set_style_text_font(network_label_, icon_font, 0);
    }

    // Set parent text color
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);

    // Set background image
    if (lvgl_theme->background_image() != nullptr) {
        lv_obj_set_style_bg_image_src(container_, lvgl_theme->background_image()->image_dsc(), 0);
    } else {
        lv_obj_set_style_bg_image_src(container_, nullptr, 0);
        lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    }
    
    // Keep status bar background with theme color and glass effect
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // 全透明，让播放器背景透过来
    
    // Update status bar elements
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);

    // Set content background opacity
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);

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
                lv_obj_set_style_bg_color(bubble, lvgl_theme->user_bubble_color(), 0);
            } else if (strcmp(bubble_type, "assistant") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->assistant_bubble_color(), 0); 
            } else if (strcmp(bubble_type, "system") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->system_bubble_color(), 0);
            } else if (strcmp(bubble_type, "image") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->system_bubble_color(), 0);
            }
            
            // Update border color
            lv_obj_set_style_border_color(bubble, lvgl_theme->border_color(), 0);
            
            // Update text color for the message
            if (lv_obj_get_child_cnt(bubble) > 0) {
                lv_obj_t* text = lv_obj_get_child(bubble, 0);
                if (text != nullptr) {
                    // 根据气泡类型设置文本颜色
                    if (strcmp(bubble_type, "system") == 0) {
                        lv_obj_set_style_text_color(text, lvgl_theme->system_text_color(), 0);
                    } else {
                        lv_obj_set_style_text_color(text, lvgl_theme->text_color(), 0);
                    }
                }
            }
        } else {
            ESP_LOGW(TAG, "child[%lu] Bubble type is not found", i);
        }
    }
#else
    // Simple UI mode - just update the main chat message
    if (chat_message_label_ != nullptr) {
        lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    }
    
    if (emoji_label_ != nullptr) {
        lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    }
#endif
    
    // Update low battery popup
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);

    // 更新聊天消息标签的主题
    if (chat_message_label_) {
        lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
        lv_obj_set_style_bg_color(chat_message_label_, lvgl_theme->assistant_bubble_color(), 0);
    }
    
    // 更新音乐播放器UI的主题
    if (music_player_ui_) {
        music_player_ui_->UpdateTheme(lvgl_theme);
    }
    
    // No errors occurred. Save theme to settings
    Display::SetTheme(lvgl_theme);
}

void LcdDisplay::UpdateMusicState(const char* title, const char* artist, bool is_playing) {
    bool request_show_clock = false;
    {
        DisplayLockGuard lock(this);
    
    #if CONFIG_USE_WECHAT_MESSAGE_STYLE
        // 微信界面模式：始终在聊天界面显示音乐信息
        if (is_playing && title && strlen(title) > 0) {
            // 构建显示文本
            std::string display_text = title;
            if (artist && strlen(artist) > 0) {
                display_text += " - " + std::string(artist);
            }
            
            // 显示音乐信息（表情显示由 ApplyEmojiVisibility 统一控制）
            if (chat_message_label_) {
                lv_obj_remove_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(chat_message_label_, display_text.c_str());
                lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
            }
            
            ESP_LOGI(TAG, "WeChat mode: Set music info: %s", display_text.c_str());
        } else {
            // 播放结束，清空音乐信息（表情显示由 ApplyEmojiVisibility 统一控制）
            if (chat_message_label_) {
                lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(chat_message_label_, "");
            }
            ESP_LOGI(TAG, "WeChat mode: Cleared music info");
        }
        // 表情显示由 ApplyEmojiVisibility 统一控制，这里不直接操作
        return;
    #endif
    
    // 默认界面模式：检查是否启用音乐播放器UI
    if (!IsMusicPlayerStyleEnabled()) {
        // 未启用播放器UI，在聊天界面显示音乐信息
        if (is_playing && title && strlen(title) > 0) {
            std::string display_text = title;
            if (artist && strlen(artist) > 0) {
                display_text += " - " + std::string(artist);
            }
            
            if (chat_message_label_) {
                lv_obj_remove_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(chat_message_label_, display_text.c_str());
                lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
            }
            
            ESP_LOGI(TAG, "Default mode: Set music info: %s", display_text.c_str());
        } else {
            if (chat_message_label_) {
                lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(chat_message_label_, "");
            }
            ESP_LOGI(TAG, "Default mode: Cleared music info");
        }
        // 表情显示由 ApplyEmojiVisibility 统一控制
        return;
    }
    
    // 音乐播放器UI模式
    if (is_playing && title && strlen(title) > 0) {
        ESP_LOGI(TAG, "Player UI mode: Setting music details: '%s' by '%s'", 
                 title, artist ? artist : "Unknown Artist");
        
        // 确保音乐播放器UI存在
        if (!music_player_ui_) {
            auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
            if (!lvgl_theme) {
                ESP_LOGE(TAG, "Theme is not initialized");
                return;
            }
            
            music_player_ui_ = std::make_unique<MusicPlayerUI>(lv_screen_active(), width_, height_, lvgl_theme);
            if (!music_player_ui_) {
                ESP_LOGE(TAG, "Failed to create music player UI");
                return;
            }
            ESP_LOGI(TAG, "Created music player UI");
        }

        // 每次更新都设置回调，避免 UI 在其它路径创建时未设置回调
        music_player_ui_->SetVolumeCallback([](int volume, void* user_data) {
            auto& board = Board::GetInstance();
            auto audio_codec = board.GetAudioCodec();
            if (audio_codec) {
                audio_codec->SetOutputVolume(volume);
                ESP_LOGI("LcdDisplay", "Set audio codec volume to %d", volume);
            }
        }, nullptr);
        
        music_player_ui_->SetPlayPauseCallback([](void* user_data) {
                auto& board = Board::GetInstance();
                auto music = board.GetMusic();
                if (music) {
                    auto esp32_music = dynamic_cast<Esp32Music*>(music);
                    if (esp32_music) {
                        if (esp32_music->IsPlaying() && !esp32_music->IsPaused()) {
                            bool success = esp32_music->PauseStreaming();
                            ESP_LOGI("LcdDisplay", "Music pause result: %s", success ? "success" : "failed");
                            if (success) {
                                auto display = board.GetDisplay();
                                if (auto lcd_display = static_cast<LcdDisplay*>(display)) {
                                    if (lcd_display->music_player_ui_) {
                                        lcd_display->music_player_ui_->SetPlayState(MusicPlayerUI::PAUSED);
                                    }
                                }
                            }
                        } else if (esp32_music->IsPlaying() && esp32_music->IsPaused()) {
                            bool success = esp32_music->ResumeStreaming();
                            ESP_LOGI("LcdDisplay", "Music resume result: %s", success ? "success" : "failed");
                            if (success) {
                                auto display = board.GetDisplay();
                                if (auto lcd_display = static_cast<LcdDisplay*>(display)) {
                                    if (lcd_display->music_player_ui_) {
                                        lcd_display->music_player_ui_->SetPlayState(MusicPlayerUI::PLAYING);
                                    }
                                }
                            }
                        }
                    }
                }
            }, nullptr);
        
        // 显示音乐播放器并更新信息
        music_player_ui_->Show();
        music_player_ui_->SetSongTitle(title);
        if (artist && strlen(artist) > 0) {
            music_player_ui_->SetArtist(artist);
        }
        music_player_ui_->SetPlayState(MusicPlayerUI::PLAYING);
        
        // 设置当前音量
        auto& board = Board::GetInstance();
        auto audio_codec = board.GetAudioCodec();
        if (audio_codec) {
            music_player_ui_->SetVolume(audio_codec->output_volume());
        }
        
        // 初始化播放状态
        music_player_ui_->SetCurrentTime("00:00");
        music_player_ui_->SetDuration("--:--");
        music_player_ui_->SetProgress(0.0f);
        music_player_ui_->SetLyrics("正在加载歌词...");
        
        // 启动进度更新
        EnableTouchVolumeControl(false);
        StartMusicProgressUpdate();
        
        // 隐藏聊天界面元素（表情显示由 ApplyEmojiVisibility 统一控制）
        if (chat_message_label_) lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        if (preview_image_) lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        
        ESP_LOGI(TAG, "Music player UI is now showing");
        
    } else {
        // 播放结束或暂停
        ESP_LOGI(TAG, "Music playback ended - hiding music player UI");
        
        if (music_player_ui_) {
            music_player_ui_->Hide();
            EnableTouchVolumeControl(true);
            StopMusicProgressUpdate();
        }
        
        // 清空聊天界面的音乐信息（如果有）
        if (chat_message_label_) {
            lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(chat_message_label_, "");
        }
        
        ESP_LOGI(TAG, "Music player UI is now hidden");
        // 播放结束后的界面切换：
        // 如果当前处于交互流程中（非空闲态，如被唤醒/手动打断进入连接/聆听/说话），
        // 直接进入聊天界面，避免先闪一下时钟界面再进入聊天界面。
        // 仅在仍处于空闲态时，才根据条件显示时钟界面。
        request_show_clock = (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) && CanShowClockFace();
    }
    
    // 统一更新表情显示状态（根据当前界面自动决定是否显示表情）
    ApplyEmojiVisibility();
    }
    // 避免二次加锁导致死锁：在释放显示锁后再切换界面
    if (request_show_clock) {
        ShowClockFace();
    } else if (!is_playing) {
        ShowChatInterface();
    }
}

// 音乐播放器功能实现
void LcdDisplay::ShowMusicPlayer() {
    if (!IsMusicPlayerStyleEnabled()) {
        ESP_LOGW(TAG, "Using traditional music display mode");
        ShowChatInterface();  // 切换到聊天界面以显示音乐信息
        return;
    }
    
    DisplayLockGuard lock(this);
    
    try {
        // 清理其他界面资源
        HideClockFace();
        
        // 创建音乐播放器UI
        if (!music_player_ui_) {
            auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
            if (!lvgl_theme) {
                ESP_LOGE(TAG, "Theme is not initialized");
                return;
            }
            
            music_player_ui_ = std::make_unique<MusicPlayerUI>(lv_screen_active(), width_, height_, lvgl_theme);
            if (!music_player_ui_) {
                ESP_LOGE(TAG, "Failed to create music player UI");
                return;
            }
            
            ESP_LOGI(TAG, "Created music player UI");
        }
        
        // 显示播放器
        music_player_ui_->Show();
        EnableTouchVolumeControl(false);
        ApplyEmojiVisibility();
        
        // 隐藏其他界面元素
        if (emoji_box_) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        if (chat_message_label_) lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        
        ESP_LOGI(TAG, "Music player UI is now visible");
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error showing music player: %s", e.what());
        music_player_ui_.reset();
        ShowChatInterface();  // 错误恢复：切换到聊天界面
    }
}

void LcdDisplay::HideMusicPlayer() {
    DisplayLockGuard lock(this);
    
    if (!music_player_ui_) {
        return;
    }
    
    try {
        // 停止进度更新
        StopMusicProgressUpdate();
        
        // 隐藏并销毁UI
        music_player_ui_->Hide();
        music_player_ui_.reset();
        
        // 恢复触摸控制和表情显示
        EnableTouchVolumeControl(true);
        ApplyEmojiVisibility();
        
        ESP_LOGI(TAG, "Music player UI is now hidden and destroyed");
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error hiding music player: %s", e.what());
        // 确保即使出错也能清理资源
        music_player_ui_.reset();
    }
}

void LcdDisplay::UpdateMusicProgress(float progress) {
    if (!music_player_ui_) {
        return;
    }
    
    // 验证进度值
    if (progress < 0.0f || progress > 1.0f) {
        ESP_LOGW(TAG, "Invalid progress value: %.2f", progress);
        progress = std::clamp(progress, 0.0f, 1.0f);
    }
    
    try {
        music_player_ui_->SetProgress(progress);
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error updating music progress: %s", e.what());
    }
}

void LcdDisplay::UpdateMusicLyrics(const char* lyrics) {
    if (!lyrics) {
        ESP_LOGW(TAG, "Null lyrics pointer");
        return;
    }
    
    // 音乐播放器模式
    if (music_player_ui_) {
        try {
            music_player_ui_->SetLyrics(lyrics);
            return;
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Error updating lyrics in player UI: %s", e.what());
            return;
        }
    }
    
    // 传统模式
    static uint32_t last_update_time = 0;
    uint32_t current_time = esp_timer_get_time() / 1000;
    
    // 限制更新频率
    if (current_time - last_update_time < 500) {
        return;
    }
    last_update_time = current_time;
    
    if (!chat_message_label_) {
        ESP_LOGW(TAG, "Chat message label is not initialized");
        return;
    }
    
    // 检查是否正在播放音乐
    bool is_music_playing = emoji_box_ && lv_obj_has_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    if (!is_music_playing) {
        return;
    }
    
    try {
        DisplayLockGuard lock(this);
        
        // 小屏幕模式
        if (width_ <= 128 && height_ <= 128) {
            lv_label_set_text(chat_message_label_, lyrics);
            lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
            ESP_LOGI(TAG, "Small screen lyrics: %.15s%s", 
                     lyrics, strlen(lyrics) > 15 ? "..." : "");
            return;
        }
        
        // 正常屏幕模式
        const char* current_text = lv_label_get_text(chat_message_label_);
        if (!current_text) {
            lv_label_set_text(chat_message_label_, lyrics);
            lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
            return;
        }
        
        // 更新歌词
        std::string display_text;
        if (strstr(current_text, " - ")) {
            // 保留歌曲信息
            std::string current_str(current_text);
            size_t newline_pos = current_str.find('\n');
            if (newline_pos != std::string::npos) {
                display_text = current_str.substr(0, newline_pos);
            } else {
                display_text = current_str;
            }
            display_text += "\n" + std::string(lyrics);
        } else {
            display_text = lyrics;
        }
        
        lv_label_set_text(chat_message_label_, display_text.c_str());
        lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
        
        ESP_LOGI(TAG, "Updated lyrics: %.30s%s", 
                 lyrics, strlen(lyrics) > 30 ? "..." : "");
                 
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error updating lyrics in traditional mode: %s", e.what());
    }
}

void LcdDisplay::EnsureClockFaceInitialized() {
    if (pixel_thinking_clock_ != nullptr) return;
    // 惰性创建（需要在LVGL任务内加锁）
    DisplayLockGuard lock(this);
    pixel_thinking_clock_ = clock_face_create(lv_screen_active(), width_, height_);
    // 背景与循环配置由 face.json 驱动，这里不再硬编码
}

void LcdDisplay::ShowClockFace() {
    if (!CanShowClockFace()) {
        ESP_LOGW(TAG, "Cannot show clock face, falling back to chat interface");
        ShowChatInterface();
        return;
    }
    
    DisplayLockGuard lock(this);
    
    try {
        // 清理其他界面资源
        HideMusicPlayer();
        
        // 初始化并显示时钟
        EnsureClockFaceInitialized();
        if (!clock_visible_) {
            clock_face_show(pixel_thinking_clock_);
            clock_visible_ = true;
            
            // 配置界面状态
            ApplyEmojiVisibility();
            EnableTouchVolumeControl(false);
            
            // 隐藏其他界面元素
            if (emoji_box_) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
            if (chat_message_label_) lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
            if (status_bar_) lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
            
            ESP_LOGI(TAG, "Clock face is now visible");
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error showing clock face: %s", e.what());
        ShowChatInterface();  // 错误恢复：切换到聊天界面
    }
}

void LcdDisplay::HideClockFace() {
    DisplayLockGuard lock(this);
    if (pixel_thinking_clock_) {
        clock_face_hide(pixel_thinking_clock_);
    }
    clock_visible_ = false;
    EnableTouchVolumeControl(true);
    // 退出时钟界面时恢复状态栏
    if (status_bar_) lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    // 根据状态统一处理表情
    ApplyEmojiVisibility();
    // 不主动切换表情，遵循显隐规则
}

void LcdDisplay::UpdateStatusBar(bool update_all) {
    // 先调用父类，完成电池/网络/时间文本更新
    LvglDisplay::UpdateStatusBar(update_all);
}

// 显示聊天界面
void LcdDisplay::ShowChatInterface() {
    DisplayLockGuard lock(this);
    
    try {
        // 隐藏其他界面
        HideMusicPlayer();
        HideClockFace();
        
        // 显示状态栏
        if (status_bar_) {
            lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
        }
        
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        if (!lvgl_theme) {
            ESP_LOGE(TAG, "Theme is not initialized");
            return;
        }
        
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
        // 微信风格界面特殊处理
        if (chat_message_label_) {
            lv_obj_set_style_bg_color(chat_message_label_, lvgl_theme->chat_background_color(), 0);
            lv_obj_set_style_radius(chat_message_label_, 8, 0);
            lv_obj_set_style_pad_all(chat_message_label_, 8, 0);
        }
#elif CONFIG_USE_EMOTE_MESSAGE_STYLE
        // 表情风格界面特殊处理
        if (emoji_box_) {
            lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, 0);
        }
#endif
        
        // 根据状态显示表情和聊天消息
        ApplyEmojiVisibility();
        
        // 启用触摸音量控制
        EnableTouchVolumeControl(true);
        
        ESP_LOGI(TAG, "Switched to chat interface");
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error showing chat interface: %s", e.what());
        // 聊天界面是最基础的界面，如果显示失败，记录错误但不尝试恢复
    }
}

// 检查是否可以显示时钟界面
bool LcdDisplay::CanShowClockFace() const {
    // 检查SD卡
    if (!IsSdMounted()) {
        ESP_LOGI(TAG, "Cannot show clock: SD card not mounted");
        return false;
    }
    
    // 检查WiFi状态(仅WiFi和双模式板)
    auto& board = Board::GetInstance();
    if (board.GetBoardType() == "wifi" || board.GetBoardType() == "dual") {
        auto icon = board.GetNetworkStateIcon();
        if (icon && std::string(icon) == FONT_AWESOME_WIFI_SLASH) {
            ESP_LOGI(TAG, "Cannot show clock: WiFi not connected");
            return false;
        }
    }
    
    return true;
}

void LcdDisplay::UpdateMusicTime(const char* current_time, const char* duration) {
    if (!music_player_ui_) {
        return;
    }
    
    if (!current_time || !duration) {
        ESP_LOGW(TAG, "Invalid time parameters");
        return;
    }
    
    try {
        music_player_ui_->SetCurrentTime(current_time);
        music_player_ui_->SetDuration(duration);
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error updating music time: %s", e.what());
    }
}

// 统一接口：设置歌曲标题、歌手与播放状态
void LcdDisplay::SetMusicDetails(const char* song_title, const char* artist, bool is_playing) {
    // 委托到统一的音乐状态接口，确保进度条与时间定时器开启
    UpdateMusicState(song_title, artist, is_playing);
}


void LcdDisplay::SetVolume(int volume) {
    if (music_player_ui_) {
        music_player_ui_->SetVolume(volume);
    }
}

int LcdDisplay::GetVolume() const {
    if (music_player_ui_) {
        return music_player_ui_->GetVolume();
    }
    return 50;  // 默认音量
}

void LcdDisplay::EnableTouchVolumeControl(bool enable) {
    static bool last_state = !enable;
    if (last_state != enable) {
        last_state = enable;
        ESP_LOGD(TAG, "Touch volume control %s", enable ? "enabled" : "disabled");
    }
}

bool LcdDisplay::IsTouchVolumeControlEnabled() const {
    return !music_player_ui_ || !music_player_ui_->IsVisible();
}

void LcdDisplay::StartMusicProgressUpdate() {
    if (music_progress_timer_ != nullptr) {
        return;
    }
    
    const esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            auto display = static_cast<LcdDisplay*>(arg);
            auto& board = Board::GetInstance();
            auto music = board.GetMusic();
            if (!music) return;
            
            auto esp32_music = dynamic_cast<Esp32Music*>(music);
            if (!esp32_music || !esp32_music->IsPlaying() || !display->music_player_ui_) {
                return;
            }
            
            // 获取播放时间和总时长
            int64_t current_time_ms = esp32_music->GetCurrentPlayTimeMs();
            int64_t total_duration_ms = esp32_music->GetTotalDurationMs();
            
            // 更新UI（需要在LVGL任务中执行）
            DisplayLockGuard lock(display);
            
            // 更新当前时间
            int total_seconds = current_time_ms / 1000;
            char time_str[16];
            snprintf(time_str, sizeof(time_str), "%02d:%02d", total_seconds / 60, total_seconds % 60);
            display->music_player_ui_->SetCurrentTime(time_str);
            
            // 更新进度和总时长
            float progress = 0.0f;
            if (total_duration_ms > 0) {
                progress = (float)current_time_ms / (float)total_duration_ms;
                if (progress > 1.0f) progress = 1.0f;
                
                total_seconds = total_duration_ms / 1000;
                snprintf(time_str, sizeof(time_str), "%02d:%02d", total_seconds / 60, total_seconds % 60);
                display->music_player_ui_->SetDuration(time_str);
            } else {
                progress = (current_time_ms / 1000.0f) / 240.0f;  // 估算4分钟
                if (progress > 1.0f) progress = 1.0f;
                display->music_player_ui_->SetDuration("--:--");
            }
            
            display->music_player_ui_->SetProgress(progress);
        },
        .arg = this,
        .name = "music_progress_timer"
    };
    
    esp_timer_create(&timer_args, &music_progress_timer_);
    esp_timer_start_periodic(music_progress_timer_, 1000000);  // 每1秒更新一次
    ESP_LOGI(TAG, "Music progress update timer started");
}

void LcdDisplay::StopMusicProgressUpdate() {
    if (music_progress_timer_ != nullptr) {
        esp_timer_stop(music_progress_timer_);
        esp_timer_delete(music_progress_timer_);
        music_progress_timer_ = nullptr;
        ESP_LOGI(TAG, "Music progress update timer stopped");
    }
}

bool LcdDisplay::IsMusicPlayerStyleEnabled() const {
#ifdef CONFIG_USE_MUSIC_PLAYER_UI
    return true;
#else
    return false;
#endif
}

