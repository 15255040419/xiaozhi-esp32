#ifndef ST7789_DISPLAY_H
#define ST7789_DISPLAY_H

// 显示缓冲区大小
#define DISPLAY_BUFFER_SIZE (240 * 10)  // 10行缓冲

// 显示分辨率
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 240

#include "display.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>

class St7789Display : public Display {
private:
    static void FlushDisplay(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map) {
        auto panel = (esp_lcd_panel_handle_t)drv->user_data;
        int offsetx1 = area->x1;
        int offsetx2 = area->x2;
        int offsety1 = area->y1;
        int offsety2 = area->y2;
        esp_lcd_panel_draw_bitmap(panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
        lv_disp_flush_ready(drv);
    }

    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    gpio_num_t backlight_pin_ = GPIO_NUM_NC;
    bool backlight_output_invert_ = false;
    bool mirror_x_ = false;
    bool mirror_y_ = false;
    bool swap_xy_ = false;
    int offset_x_ = 0;
    int offset_y_ = 0;
    SemaphoreHandle_t lvgl_mutex_ = nullptr;
    esp_timer_handle_t lvgl_tick_timer_ = nullptr;
    
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;

    void InitializeBacklight(gpio_num_t backlight_pin);
    void SetBacklight(uint8_t brightness);
    void SetupUI();
    void LvglTask();
    void InitializeLvgl();

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

public:
    St7789Display(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  gpio_num_t backlight_pin, bool backlight_output_invert,
                  int width, int height,  int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);
    ~St7789Display();

    void StartLvglTask() {
        xTaskCreatePinnedToCore([](void* arg) {
            auto display = static_cast<St7789Display*>(arg);
            vTaskPrioritySet(nullptr, 5);
            display->LvglTask();
        }, "lvgl", 8192, this, 5, &task_handle_, 1);
    }
};

#endif // ST7789_DISPLAY_H
