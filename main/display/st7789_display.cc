#include "st7789_display.h"
#include "font_awesome_symbols.h"
#include "esp_random.h"

#include <esp_log.h>
#include <esp_err.h>
#include <driver/ledc.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "St7789Display"
#define LCD_LEDC_CH LEDC_CHANNEL_0

#define ST7789_LVGL_TICK_PERIOD_MS 2
#define ST7789_LVGL_TASK_MAX_DELAY_MS 20
#define ST7789_LVGL_TASK_MIN_DELAY_MS 1
#define ST7789_LVGL_TASK_STACK_SIZE (4 * 1024)
#define ST7789_LVGL_TASK_PRIORITY 10

#define EXPRESSION_CHANGE_INTERVAL 2000

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_30_1);
LV_FONT_DECLARE(font_awesome_14_1);

static lv_disp_drv_t disp_drv;
static void st7789_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
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
static void st7789_lvgl_port_update_callback(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;

    switch (drv->rotated)
    {
    case LV_DISP_ROT_NONE:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
#if CONFIG_ST7789_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    case LV_DISP_ROT_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
#if CONFIG_ST7789_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    case LV_DISP_ROT_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
#if CONFIG_ST7789_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    case LV_DISP_ROT_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
#if CONFIG_ST7789_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    }
}

static constexpr lv_style_selector_t GetStyleSelector() {
    return static_cast<lv_style_selector_t>(static_cast<uint32_t>(LV_PART_MAIN) | static_cast<uint32_t>(LV_STATE_DEFAULT));
}

void St7789Display::LvglTask() {
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = LV_DISP_DEF_REFR_PERIOD;  // Use refresh period from lv_conf.h
    while (1)
    {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (Lock())
        {
            task_delay_ms = lv_timer_handler();
            Unlock();
        }
        // Ensure we don't delay longer than our configured refresh period
        if (task_delay_ms > LV_DISP_DEF_REFR_PERIOD)
        {
            task_delay_ms = LV_DISP_DEF_REFR_PERIOD;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}


St7789Display::St7789Display(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           gpio_num_t backlight_pin, bool backlight_output_invert,
                           int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
    : panel_io_(panel_io), panel_(panel), backlight_pin_(backlight_pin), backlight_output_invert_(backlight_output_invert),
      mirror_x_(mirror_x), mirror_y_(mirror_y), swap_xy_(swap_xy) {
    width_ = width;
    height_ = height;
    offset_x_ = offset_x;
    offset_y_ = offset_y;

    
    InitializeBacklight(backlight_pin);

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
    // alloc draw buffers used by LVGL
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    // Use canvas size buffer as defined in lv_conf.h
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(LV_DISP_DRAW_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(buf1);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(LV_DISP_DRAW_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(buf2);
    // initialize LVGL draw buffers with canvas size
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LV_DISP_DRAW_BUF_SIZE);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = width_;
    disp_drv.ver_res = height_;
    disp_drv.offset_x = offset_x_;
    disp_drv.offset_y = offset_y_;
    disp_drv.flush_cb = st7789_lvgl_flush_cb;
    disp_drv.drv_update_cb = st7789_lvgl_port_update_callback;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_;
    disp_drv.dpi = 240;  // 设置适当的DPI值
    disp_drv.direct_mode = 1;  // 启用直接模式以提高性能

    lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = [](void* arg) {
            lv_tick_inc(ST7789_LVGL_TICK_PERIOD_MS);
        },
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "LVGL Tick Timer",
        .skip_unhandled_events = false
    };
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer_, ST7789_LVGL_TICK_PERIOD_MS * 1000));

    lvgl_mutex_ = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mutex_ != nullptr);
    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreate([](void *arg) {
        static_cast<St7789Display*>(arg)->LvglTask();
        vTaskDelete(NULL);
    }, "LVGL", ST7789_LVGL_TASK_STACK_SIZE, this, ST7789_LVGL_TASK_PRIORITY, NULL);

    SetBacklight(100);

    // Set default screen background to black
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), GetStyleSelector());
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, GetStyleSelector());

    SetupUI();

    xTaskCreate(expression_demo_task, "expression_demo", 4096, this, 5, NULL);
}

St7789Display::~St7789Display() {
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

void St7789Display::InitializeBacklight(gpio_num_t backlight_pin) {
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

void St7789Display::SetBacklight(uint8_t brightness) {
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

bool St7789Display::Lock(int timeout_ms) {
    // Convert timeout in milliseconds to FreeRTOS ticks
    // If `timeout_ms` is set to 0, the program will block until the condition is met
    const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mutex_, timeout_ticks) == pdTRUE;
}

void St7789Display::Unlock() {
    xSemaphoreGiveRecursive(lvgl_mutex_);
}

void St7789Display::SetupUI() {
    DisplayLockGuard lock(this);

    // Set screen background color to black
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), GetStyleSelector());
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, GetStyleSelector());

    // Create status bar with black background
    status_bar_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(status_bar_);
    lv_obj_set_size(status_bar_, width_, 20);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, lv_color_black(), GetStyleSelector());
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_COVER, GetStyleSelector());
    lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_SCROLLABLE);

    // Create status label with white text
    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_color(status_label_, lv_color_white(), GetStyleSelector());
    lv_label_set_text(status_label_, "");
    lv_obj_align(status_label_, LV_ALIGN_LEFT_MID, 0, 0);

    // Create notification label with white text
    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_color(notification_label_, lv_color_white(), GetStyleSelector());
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    // Create battery label with white text
    battery_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_color(battery_label_, lv_color_white(), GetStyleSelector());
    lv_label_set_text(battery_label_, "");
    lv_obj_align(battery_label_, LV_ALIGN_RIGHT_MID, 0, 0);

    // Create network label with white text
    network_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_color(network_label_, lv_color_white(), GetStyleSelector());
    lv_label_set_text(network_label_, "");
    lv_obj_align(network_label_, LV_ALIGN_RIGHT_MID, -20, 0);

    // Create mute label with white text
    mute_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_color(mute_label_, lv_color_white(), GetStyleSelector());
    lv_label_set_text(mute_label_, "");
    lv_obj_align(mute_label_, LV_ALIGN_RIGHT_MID, -40, 0);

    // Create face display with white color
    face_display_ = new FaceDisplay(lv_scr_act(), width_, height_ - 20);  // 减去状态栏高度
    face_display_->setExpression(Expression::Normal);
}

void St7789Display::SetFaceExpression(Expression exp) {
    if (face_display_) {
        face_display_->setExpression(exp);
    }
}

void St7789Display::DoBlink() {
    if (face_display_) {
        face_display_->doBlink();
    }
}

void St7789Display::expression_demo_task(void* arg) {
    St7789Display* display = static_cast<St7789Display*>(arg);
    
    const Expression expressions[] = {
        Expression::Normal,
        Expression::Happy,
        Expression::Sad,
        Expression::Angry,
        Expression::Surprised,
        Expression::Sleepy,
        Expression::Glee,
        Expression::Worried,
        Expression::Focused,
        Expression::Annoyed,
        Expression::Skeptic,
        Expression::Frustrated,
        Expression::Unimpressed,
        Expression::Suspicious,
        Expression::Squint,
        Expression::Furious,
        Expression::Scared,
        Expression::Awe,
        Expression::LookLeft,
        Expression::LookRight
    };
    
    const int total_expressions = sizeof(expressions) / sizeof(expressions[0]);
    int current_index = 0;
    
    while (true) {
        if (display->face_display_) {
            // 设置当前表情
            display->face_display_->setExpression(expressions[current_index]);
            
            // 等待3秒
            vTaskDelay(pdMS_TO_TICKS(3000));
            
            // 随机触发眨眼
            if (esp_random() % 3 == 0) {  // 1/3的概率眨眼
                display->face_display_->doBlink();
                vTaskDelay(pdMS_TO_TICKS(100));  // 等待眨眼动画完成
            }
            
            // 移动到下一个表情
            current_index = (current_index + 1) % total_expressions;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));  // 防止看门狗超时
    }
}
