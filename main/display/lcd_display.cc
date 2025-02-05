#include "lcd_display.h"
#include "font_awesome_symbols.h"

#include <esp_log.h>
#include <esp_err.h>
#include <driver/ledc.h>
#include <vector>

#define TAG "LcdDisplay"
#define LCD_LEDC_CH LEDC_CHANNEL_0

#define LCD_LVGL_TICK_PERIOD_MS 2
#define LCD_LVGL_TASK_MAX_DELAY_MS 20
#define LCD_LVGL_TASK_MIN_DELAY_MS 1
#define LCD_LVGL_TASK_STACK_SIZE (4 * 1024)
#define LCD_LVGL_TASK_PRIORITY 10

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_30_1);
LV_FONT_DECLARE(font_awesome_14_1);
LV_FONT_DECLARE(font_dingding);

static lv_disp_drv_t disp_drv;
static void lcd_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    lv_disp_flush_ready(&disp_drv);
}

/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
static void lcd_lvgl_port_update_callback(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;

    switch (drv->rotated)
    {
    case LV_DISP_ROT_NONE:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    case LV_DISP_ROT_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISP_ROT_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISP_ROT_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;
    }
}

void LcdDisplay::LvglTask() {
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = LCD_LVGL_TASK_MAX_DELAY_MS;
    while (1)
    {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (Lock())
        {
            task_delay_ms = lv_timer_handler();
            Unlock();
        }
        if (task_delay_ms > LCD_LVGL_TASK_MAX_DELAY_MS)
        {
            task_delay_ms = LCD_LVGL_TASK_MAX_DELAY_MS;
        }
        else if (task_delay_ms < LCD_LVGL_TASK_MIN_DELAY_MS)
        {
            task_delay_ms = LCD_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}


LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           gpio_num_t backlight_pin, bool backlight_output_invert,
                           int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
    : panel_io_(panel_io), panel_(panel), backlight_pin_(backlight_pin), backlight_output_invert_(backlight_output_invert),
      mirror_x_(mirror_x), mirror_y_(mirror_y), swap_xy_(swap_xy) {
    width_ = width;
    height_ = height;
    offset_x_ = offset_x;
    offset_y_ = offset_y;

    
    InitializeBacklight(backlight_pin);

    // 设置初始黑色背景
    std::vector<uint16_t> buffer(width_, 0x0000);  // 0x0000 是黑色
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(width_ * 10 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(width_ * 10 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, width_ * 10);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = width_;
    disp_drv.ver_res = height_;
    disp_drv.offset_x = offset_x_;
    disp_drv.offset_y = offset_y_;
    disp_drv.flush_cb = lcd_lvgl_flush_cb;
    disp_drv.drv_update_cb = lcd_lvgl_port_update_callback;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_;

    lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = [](void* arg) {
            lv_tick_inc(LCD_LVGL_TICK_PERIOD_MS);
        },
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "LVGL Tick Timer",
        .skip_unhandled_events = false
    };
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer_, LCD_LVGL_TICK_PERIOD_MS * 1000));

    lvgl_mutex_ = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mutex_ != nullptr);
    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreate([](void *arg) {
        static_cast<LcdDisplay*>(arg)->LvglTask();
        vTaskDelete(NULL);
    }, "LVGL", LCD_LVGL_TASK_STACK_SIZE, this, LCD_LVGL_TASK_PRIORITY, NULL);

    SetBacklight(100);

    SetupUI();
}

LcdDisplay::~LcdDisplay() {
    ESP_ERROR_CHECK(esp_timer_stop(lvgl_tick_timer_));
    ESP_ERROR_CHECK(esp_timer_delete(lvgl_tick_timer_));

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

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    vSemaphoreDelete(lvgl_mutex_);
}

void LcdDisplay::InitializeBacklight(gpio_num_t backlight_pin) {
    if (backlight_pin == GPIO_NUM_NC) {
        return;
    }

    // Setup LEDC peripheral for PWM backlight control
    const ledc_channel_config_t backlight_channel = {
        .gpio_num = backlight_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = backlight_output_invert_,
        }
    };
    const ledc_timer_config_t backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };

    ESP_ERROR_CHECK(ledc_timer_config(&backlight_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&backlight_channel));
}

void LcdDisplay::SetBacklight(uint8_t brightness) {
    if (backlight_pin_ == GPIO_NUM_NC) {
        return;
    }

    if (brightness > 100) {
        brightness = 100;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness);
    // LEDC resolution set to 10bits, thus: 100% = 1023
    uint32_t duty_cycle = (1023 * brightness) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));
}

bool LcdDisplay::Lock(int timeout_ms) {
    // Convert timeout in milliseconds to FreeRTOS ticks
    // If `timeout_ms` is set to 0, the program will block until the condition is met
    const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mutex_, timeout_ticks) == pdTRUE;
}

void LcdDisplay::Unlock() {
    xSemaphoreGiveRecursive(lvgl_mutex_);
}

void LcdDisplay::SetupUI() {
    DisplayLockGuard lock(this);

    // 设置黑色背景
    auto screen = lv_disp_get_scr_act(lv_disp_get_default());
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    
    // 状态栏
    status_bar_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(status_bar_, LV_HOR_RES - 40, 40);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_align(status_bar_, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(status_bar_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);

    // 表情图标
    emotion_label_ = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_1, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);
    lv_obj_set_align(emotion_label_, LV_ALIGN_TOP_MID);
    lv_obj_set_y(emotion_label_, 50);
    lv_obj_set_style_text_color(emotion_label_, lv_palette_main(LV_PALETTE_GREEN), 0);

    // 聊天消息标签
    chat_message_label_ = lv_label_create(lv_scr_act());
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.8);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(chat_message_label_, &font_dingding, 0);
    lv_label_set_text(chat_message_label_, "AI聊天助手\n廖翎羽\nLIAO LING YU");
    lv_obj_set_style_text_color(chat_message_label_, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_align(chat_message_label_, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(chat_message_label_, 10);  // 调整底部距离
    lv_obj_set_height(chat_message_label_, 120);  // 设置标签的高度，根据需要调整数值

    // 设置文本垂直居中对齐
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_align(chat_message_label_, LV_ALIGN_CENTER, LV_PART_MAIN);

    // 状态栏内容
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);

    // WiFi图标
    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_y(network_label_, 10);
    lv_obj_set_style_text_font(network_label_, &font_awesome_14_1, 0);
    lv_obj_set_style_text_color(network_label_, lv_palette_main(LV_PALETTE_GREEN), 0);

    // 通知标签
    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(notification_label_, &font_dingding, 0);
    lv_obj_set_style_text_color(notification_label_, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_label_set_text(notification_label_, "通知");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    // 状态标签
    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(status_label_, &font_dingding, 0);
    lv_obj_set_style_text_color(status_label_, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_label_set_text(status_label_, "正在初始化");

    // 电池图标
    //battery_label_ = lv_label_create(status_bar_);
    //lv_label_set_text(battery_label_, "");
    //lv_obj_set_style_text_font(battery_label_, &font_awesome_14_1, 0);
    //lv_obj_set_style_text_color(battery_label_, lv_palette_main(LV_PALETTE_GREEN), 0);
    //lv_obj_set_align(battery_label_, LV_ALIGN_TOP_RIGHT);

    // 静音图标
    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, &font_awesome_14_1, 0);
    lv_obj_set_style_text_color(mute_label_, lv_palette_main(LV_PALETTE_GREEN), 0);
}

void LcdDisplay::SetChatMessage(const std::string &role, const std::string &content) {
    if (chat_message_label_ == nullptr) {
        return;
    }
    
    // 替换中文标点
    std::string display_text = content;
    std::string::size_type pos = 0;
    
    const std::vector<std::pair<std::string, std::string>> replacements = {
        {"：", ":"},
    };
    
    for (const auto& [from, to] : replacements) {
        while ((pos = display_text.find(from, pos)) != std::string::npos) {
            display_text.replace(pos, from.length(), to);
            pos += to.length();
        }
    }

    // 重置打字机状态
    full_message_ = display_text;
    current_message_ = "";
    char_index_ = 0;
    
    // 删除旧的定时器
    if (typewriter_timer_) {
        lv_timer_del(typewriter_timer_);
    }
    
    // 创建新的定时器
    typewriter_timer_ = lv_timer_create(TypewriterTimerCb, TYPEWRITER_DELAY, this);
}

void LcdDisplay::TypewriterTimerCb(lv_timer_t* timer) {
    LcdDisplay* display = static_cast<LcdDisplay*>(timer->user_data);
    display->UpdateTypewriterText();
}

void LcdDisplay::UpdateTypewriterText() {
    if (char_index_ >= full_message_.length()) {
        // 完成打字，删除定时器
        lv_timer_del(typewriter_timer_);
        typewriter_timer_ = nullptr;
        return;
    }
    
    // UTF-8字符处理
    size_t char_len = 1;
    unsigned char first_byte = full_message_[char_index_];
    if ((first_byte & 0xE0) == 0xE0) {
        char_len = 3;  // 中文字符
    } else if ((first_byte & 0xC0) == 0xC0) {
        char_len = 2;  // 双字节字符
    }
    
    current_message_ += full_message_.substr(char_index_, char_len);
    char_index_ += char_len;
    
    lv_label_set_text(chat_message_label_, current_message_.c_str());
}
