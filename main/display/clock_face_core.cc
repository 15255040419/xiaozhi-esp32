#include "lvgl.h"
#include <esp_timer.h>
#include <esp_log.h>
#include <string>
#include <memory>
#include <array>
#include <cJSON.h>

#include "assets.h"
#include "display/lvgl_display/lvgl_image.h"
#include "display/lvgl_display/gif/lvgl_gif.h"
#include "settings.h"

// PixelThinking 风格的时钟界面（核心实现，C 接口见底部）

static bool g_clock_face_active = false;

extern "C" bool clock_face_is_active() {
    return g_clock_face_active;
}

class ClockFacePixelThinkingCore {
public:
    enum LoopMode {
        LOOP_NONE = 0,
        LOOP_ONCE = 1,
        LOOP_INFINITE = -1
    };

    ClockFacePixelThinkingCore(lv_obj_t* parent, int width, int height)
        : parent_(parent), width_(width), height_(height) {
        // 先加载配置（仅解析JSON，不加载资源）
        LoadFaceConfig();
        CreateUI();
    }

    ~ClockFacePixelThinkingCore() {
        Stop();
        if (gif_) {
            gif_.reset();
        }
        StopTick();
        if (container_) lv_obj_del(container_);
    }

    void Show() {
        if (container_) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(container_);
        }
        g_clock_face_active = true;
        // 同时支持两种模式：
        // 1) face.json 提供 4 个坐标 -> 使用绝对定位（隐藏 flex 容器）
        // 2) 未提供 -> 使用原来的水平 FLEX 布局
        // 从设置读取默认主题
        if (!loaded_from_settings_) {
            Settings s("display", false);
            std::string face = s.GetString("clock_face", active_face_name_);
            if (!face.empty()) active_face_name_ = face;
            loaded_from_settings_ = true;
        }
        LoadFaceConfig();
        if (use_absolute_positions_) {
            if (time_container_) lv_obj_add_flag(time_container_, LV_OBJ_FLAG_HIDDEN);
            // 确保数字对象在根容器并按坐标定位
            for (int i = 0; i < 4; ++i) {
                if (digit_img_[i]) {
                    if (lv_obj_get_parent(digit_img_[i]) != container_) lv_obj_set_parent(digit_img_[i], container_);
                    // 取消可能残留的自动布局影响
                    lv_obj_remove_style_all(digit_img_[i]);
                }
            }
            ApplyPositions();
        } else {
            // 使用 FLEX：把数字放进 time_container_，让其水平居中排列
            if (time_container_) lv_obj_clear_flag(time_container_, LV_OBJ_FLAG_HIDDEN);
            for (int i = 0; i < 4; ++i) {
                if (digit_img_[i]) {
                    if (lv_obj_get_parent(digit_img_[i]) != time_container_) lv_obj_set_parent(digit_img_[i], time_container_);
                    // FLEX 模式下不手动定位
                }
            }
        }
        // 加载背景资源（首次显示时）
        LoadBackgroundIfNeeded();
        if (gif_) gif_->Start();
        EnsureDigitsLoaded();
        StartTick();
        UpdateTime();
    }

    void Hide() {
        StopTick();
        if (gif_) gif_->Pause();
        if (container_) {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
        g_clock_face_active = false;
    }

    void Stop() {
        StopTick();
        if (gif_) gif_->Stop();
    }

    void SetLoopMode(LoopMode mode) {
        loop_mode_ = mode;
        if (gif_) {
            if (mode == LOOP_NONE) gif_->SetLoopCount(1);
            else if (mode == LOOP_ONCE) gif_->SetLoopCount(1);
            else gif_->SetLoopCount(0); // 无限
        }
    }

    // 从 assets 加载壁纸
    bool LoadBackground(const std::string& bg_name) {
        void* ptr = nullptr; size_t size = 0;
        auto& assets = Assets::GetInstance();
        if (bg_name.empty()) {
            lv_obj_add_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
            return true;
        }
        std::string matched_key;
        auto try_load = [&](const std::string& name)->bool{
            // 兼容多种打包键名：完整路径、@前缀、斜杠改下划线、以及 basename（最后兜底）
            std::string n1 = name;
            for (auto &ch : n1) if (ch == '\\') ch = '/';
            if (assets.GetAssetData(n1, ptr, size)) { matched_key = n1; return true; }
            std::string n2 = std::string("@") + n1;
            if (assets.GetAssetData(n2, ptr, size)) { matched_key = n2; return true; }
            std::string n3 = n1; for (auto &ch : n3) if (ch == '/') ch = '_';
            if (assets.GetAssetData(n3, ptr, size)) { matched_key = n3; return true; }
            return false;
        };
        if (!try_load(bg_name)) {
            ESP_LOGW(TAG, "Background asset not found (full path): %s", bg_name.c_str());
            // 如果是 GIF 失败，尝试降级到 PNG
            if (bg_name.find(".gif") != std::string::npos) {
                std::string png_name = bg_name;
                size_t pos = png_name.rfind(".gif");
                if (pos != std::string::npos) {
                    png_name.replace(pos, 4, ".png");
                    ESP_LOGI(TAG, "Trying fallback to PNG: %s", png_name.c_str());
                    return LoadBackground(png_name);
                }
            }
            return false;
        }

        // 尝试作为 GIF
        std::unique_ptr<LvglImage> raw = std::make_unique<LvglRawImage>(ptr, size);
        if (raw->IsGif()) {
            gif_ = std::make_unique<LvglGif>(raw->image_dsc());
            if (gif_->IsLoaded()) {
                gif_->SetFrameCallback([this]() {
                    if (background_img_) {
                        lv_image_set_src(background_img_, gif_->image_dsc());
                    }
                });
                // 初始帧
                lv_image_set_src(background_img_, gif_->image_dsc());
                lv_obj_clear_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
                // 循环策略
                SetLoopMode(loop_mode_);
                gif_->Start();
                // 记录尺寸
                const lv_image_dsc_t* d = gif_->image_dsc();
                if (d) {
                    ESP_LOGI(TAG, "Background GIF loaded successfully, key=%s size=%dx%d cf=%d", matched_key.c_str(), (int)d->header.w, (int)d->header.h, (int)d->header.cf);
                } else {
                    ESP_LOGI(TAG, "Background GIF loaded successfully, key=%s", matched_key.c_str());
                }
                return true;
            } else {
                ESP_LOGW(TAG, "Failed to load GIF, trying as static image");
                gif_.reset();
            }
        }

        // 普通图片（PNG/JPG）：拷贝到可释放缓冲区并让 LVGL 解码
        void* copy_buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (copy_buf) memcpy(copy_buf, ptr, size);
        background_image_ = std::make_unique<LvglAllocatedImage>(copy_buf, size);
        if (background_image_->image_dsc()) {
            lv_image_set_src(background_img_, background_image_->image_dsc());
            // 恢复为不缩放，按原始尺寸显示
            lv_image_set_scale(background_img_, 256);
            lv_obj_clear_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
            const lv_image_dsc_t* d = background_image_->image_dsc();
            ESP_LOGI(TAG, "Background PNG loaded successfully, key=%s size=%dx%d cf=%d", matched_key.c_str(), (int)d->header.w, (int)d->header.h, (int)d->header.cf);
            return true;
        } else {
            ESP_LOGE(TAG, "Failed to decode background image");
            return false;
        }
    }

private:
    // 加载配置（仅解析JSON，不加载资源）
    void LoadFaceConfig() {
        // 允许多次调用，以便在 assets 就绪后能生效
        std::array<int, 2> pos_h_a{0,0}, pos_h_b{0,0}, pos_m_a{0,0}, pos_m_b{0,0}, pos_colon{0,0};

        void* ptr = nullptr; size_t size = 0;
        auto& assets = Assets::GetInstance();
        auto try_get = [&](const std::string& name)->bool{
            std::string n1 = name; for (auto &ch : n1) if (ch == '\\') ch = '/';
            if (assets.GetAssetData(n1, ptr, size)) return true;
            std::string n2 = std::string("@") + n1;
            if (assets.GetAssetData(n2, ptr, size)) return true;
            std::string n3 = n1; for (auto &ch : n3) if (ch == '/') ch = '_';
            if (assets.GetAssetData(n3, ptr, size)) return true;
            auto p = n1.find_last_of('/');
            if (p != std::string::npos) {
                std::string base = n1.substr(p + 1);
                if (assets.GetAssetData(base, ptr, size)) return true;
            }
            return false;
        };
        const std::string face_path = std::string("clock_faces/") + active_face_name_ + "/face.json";
        if (try_get(face_path)) {
            std::string json_str((const char*)ptr, size);
            cJSON* root = cJSON_ParseWithLength(json_str.c_str(), json_str.size());
            if (root) {
                // use_gif_background
                cJSON* gif_bg = cJSON_GetObjectItem(root, "use_gif_background");
                if (cJSON_IsString(gif_bg)) {
                    std::string v = gif_bg->valuestring;
                    for (auto &ch : v) ch = (char)tolower((unsigned char)ch);
                    use_gif_background_ = (v == "yes" || v == "true" || v == "1");
                }
                // background_loop: "infinite"|"once"|"none" 或数值 -1/1/0
                cJSON* loop = cJSON_GetObjectItem(root, "background_loop");
                if (cJSON_IsString(loop)) {
                    std::string v = loop->valuestring;
                    for (auto &ch : v) ch = (char)tolower((unsigned char)ch);
                    if (v == "once") loop_mode_ = LOOP_ONCE; 
                    else if (v == "none") loop_mode_ = LOOP_NONE; 
                    else loop_mode_ = LOOP_INFINITE;
                } else if (cJSON_IsNumber(loop)) {
                    int n = loop->valueint;
                    loop_mode_ = (n == 0 ? LOOP_NONE : (n == 1 ? LOOP_ONCE : LOOP_INFINITE));
                }
                // use_text_time: yes/no/true/false/1/0
                cJSON* txt = cJSON_GetObjectItem(root, "use_text_time");
                if (cJSON_IsString(txt)) {
                    std::string v = txt->valuestring;
                    for (auto &ch : v) ch = (char)tolower((unsigned char)ch);
                    use_text_time_ = (v == "yes" || v == "true" || v == "1");
                } else if (cJSON_IsBool(txt)) {
                    use_text_time_ = cJSON_IsTrue(txt);
                } else if (cJSON_IsNumber(txt)) {
                    use_text_time_ = (txt->valueint != 0);
                }
                // text layout parameters (optional)
                auto read_int = [&](const char* key, int &out){ cJSON* n = cJSON_GetObjectItem(root, key); if (cJSON_IsNumber(n)) out = n->valueint; };
                read_int("text_time_zoom", time_zoom_);      // e.g., 256=1.0x, 384=1.5x, 512=2.0x
                read_int("text_date_zoom", date_zoom_);      // default 256
                read_int("text_time_y", time_y_offset_);     // y offset for time label
                read_int("text_date_gap", date_gap_);        // gap between time & date
                // positions
                auto read_xy = [&](const char* key, std::array<int,2>& out) -> bool {
                    cJSON* a = cJSON_GetObjectItem(root, key);
                    if (!a || !cJSON_IsArray(a) || cJSON_GetArraySize(a) != 2) return false;
                    cJSON* x = cJSON_GetArrayItem(a, 0);
                    cJSON* y = cJSON_GetArrayItem(a, 1);
                    if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) return false;
                    out[0] = x->valueint; out[1] = y->valueint; return true;
                };
                bool ok = true;
                ok &= read_xy("pos_clock_hour_a", pos_h_a);
                ok &= read_xy("pos_clock_hour_b", pos_h_b);
                ok &= read_xy("pos_clock_min_a", pos_m_a);
                ok &= read_xy("pos_clock_min_b", pos_m_b);
                // optional colon position
                has_colon_pos_ = read_xy("pos_clock_colon", pos_colon);
                if (ok) {
                    use_absolute_positions_ = true;
                    // 4位数字布局：小时a, 小时b, 分钟a, 分钟b
                    pos_[0] = pos_h_a; 
                    pos_[1] = pos_h_b; 
                    pos_[2] = pos_m_a; 
                    pos_[3] = pos_m_b;
                    if (has_colon_pos_) pos_[4] = pos_colon;
                    ESP_LOGI(TAG, "Loaded face.json positions: HA(%d,%d) HB(%d,%d) MA(%d,%d) MB(%d,%d)",
                             pos_[0][0], pos_[0][1], pos_[1][0], pos_[1][1], pos_[2][0], pos_[2][1], pos_[3][0], pos_[3][1]);
                } else {
                    use_absolute_positions_ = false;
                    ESP_LOGI(TAG, "face.json positions missing or invalid, using auto layout");
                }
                cJSON_Delete(root);
            }
        }
    }

    // 不再支持自适应布局

    // 加载背景资源
    void LoadBackgroundIfNeeded() {
        if (background_loaded_) return;
        background_loaded_ = true;
        
        // 根据配置加载背景（支持多种文件名）
        std::string base = std::string("clock_faces/") + active_face_name_ + "/background/";
        if (use_gif_background_) {
            if (!LoadBackground(base + "bg.gif")) {
                LoadBackground(base + "background.gif");
            }
        } else {
            if (!LoadBackground(base + "bg.png")) {
                LoadBackground(base + "background.png");
            }
        }
        // 设置循环模式
        SetLoopMode(loop_mode_);
    }

    void CreateUI() {
        container_ = lv_obj_create(parent_);
        lv_obj_set_size(container_, width_, height_);
        lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container_, 0, 0);
        lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(container_, LV_DIR_NONE);
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(container_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(container_, LV_OBJ_FLAG_PRESS_LOCK); // 保证长按事件锁定在本对象
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

        // 背景层
        background_img_ = lv_image_create(container_);
        // 原图多大就多大：让图片控件使用内容尺寸
        lv_obj_set_size(background_img_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_align(background_img_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_scrollbar_mode(background_img_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(background_img_, LV_DIR_NONE);
        lv_obj_clear_flag(background_img_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(background_img_, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(background_img_, LV_OBJ_FLAG_CLICKABLE); // 使其产生按压/长按事件

        // 时间图片层（保留但默认隐藏；我们改为只用 JSON 绝对定位）
        time_container_ = lv_obj_create(container_);
        lv_obj_set_style_bg_opa(time_container_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(time_container_, 0, 0);
        lv_obj_set_width(time_container_, LV_SIZE_CONTENT);
        lv_obj_set_height(time_container_, LV_SIZE_CONTENT);
        lv_obj_align(time_container_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_flex_flow(time_container_, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(time_container_, 0, 0);
        lv_obj_set_style_pad_column(time_container_, width_ / 40 + 2, 0);
        lv_obj_set_scrollbar_mode(time_container_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(time_container_, LV_DIR_NONE);
        lv_obj_clear_flag(time_container_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(time_container_, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(time_container_, LV_OBJ_FLAG_CLICKABLE);

        for (int i = 0; i < 5; ++i) {
            digit_img_[i] = lv_image_create(container_);
            lv_obj_add_flag(digit_img_[i], LV_OBJ_FLAG_EVENT_BUBBLE);
            lv_obj_add_flag(digit_img_[i], LV_OBJ_FLAG_CLICKABLE);
            // 初始暂不定位，具体在 Show() 按模式设置
        }
        // 进入/退出切换模式的视觉效果容器就是 container_
        // 注意：确认由 3 秒自动定时器完成，此处不创建按钮
        // 长按与滑动手势（并拦截手势冒泡，避免触发系统级手势如触摸音量）
        lv_obj_add_event_cb(container_, [](lv_event_t* e){
            auto self = static_cast<ClockFacePixelThinkingCore*>(lv_event_get_user_data(e));
            if (!self) return;
            lv_event_code_t code = lv_event_get_code(e);
            // 在切换模式中，不拦截触摸；维护 touch_active_ 并重置无触摸计时
            if (self->switch_mode_) {
                if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
                    code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
                    self->touch_active_ = true;
                    if (self->auto_confirm_timer_) lv_timer_reset(self->auto_confirm_timer_);
                } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED) {
                    self->touch_active_ = false; // 松手后开始无触摸倒计时
                    if (self->auto_confirm_timer_) lv_timer_reset(self->auto_confirm_timer_);
                }
                return;
            }
            if (code == LV_EVENT_PRESSED) {
                // 标记为点击候选，等待 RELEASED 再决定是否重播
                self->tap_pending_ = true;
                self->long_pressed_detected_ = false;
            } else if (code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
                // 长按：进入主题切换模式，并取消点击候选，防止误触发重播
                self->long_pressed_detected_ = true;
                self->tap_pending_ = false;
                self->EnterSwitchMode();
                lv_event_stop_bubbling(e);
                lv_event_stop_processing(e);
            } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED) {
                // 短按释放：仅当非长按时才重播 GIF
                if (self->tap_pending_ && !self->long_pressed_detected_) {
                    if (self->gif_ && self->gif_->IsLoaded()) {
                        self->gif_->Stop();
                        self->SetLoopMode(self->loop_mode_);
                        self->gif_->Start();
                    }
                }
                self->tap_pending_ = false;
                // 拦截点击/释放事件，避免外部处理
                lv_event_stop_bubbling(e);
                lv_event_stop_processing(e);
            } else if (code == LV_EVENT_PRESSING) {
                // 在时钟界面内拦截触摸事件，避免外部处理（例如触摸音量）
                lv_event_stop_bubbling(e);
                lv_event_stop_processing(e);
            }
        }, LV_EVENT_ALL, this);
    }

    void StartTick() {
        if (tick_timer_) return;
        // 使用 LVGL 定时器，在 LVGL 任务中刷新，先对齐到下一个整分钟再每60秒刷新
        timer_aligned_ = false;
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        uint32_t until_next_min_ms = 60000;
        if (tm_info) {
            int sec = tm_info->tm_sec;
            if (sec >= 0 && sec < 60) until_next_min_ms = static_cast<uint32_t>((60 - sec) * 1000);
        }
        tick_timer_ = lv_timer_create(
            [](lv_timer_t* timer){
                auto self = static_cast<ClockFacePixelThinkingCore*>(lv_timer_get_user_data(timer));
                if (!self) return;
                self->UpdateTime();
                if (!self->timer_aligned_) {
                    self->timer_aligned_ = true;
                    lv_timer_set_period(timer, 60000); // 之后每60秒刷新一次
                }
            },
            until_next_min_ms,
            this
        );
    }

    void StopTick() {
        if (tick_timer_) {
            lv_timer_del(tick_timer_);
            tick_timer_ = nullptr;
        }
    }

    void EnsureDigitsLoaded() {
        if (digits_loaded_) return;
        if (use_text_time_) { // 文本时间模式：不加载数字图片，避免告警
            digits_loaded_ = true;
            return;
        }
        // 期望资源路径：clock_faces/<face>/number/0.png ... 9.png
        bool all_loaded = true;
        for (int d = 0; d <= 9; ++d) {
            if (!LoadImageTo(digit_images_[d], MakeNumberPath(d))) {
                ESP_LOGW(TAG, "Failed to load digit %d", d);
                all_loaded = false;
            }
        }
        // 仅 green 尝试加载冒号 PNG
        if (active_face_name_ == "green") {
            if (!LoadImageTo(colon_image_, MakeColonPath())) {
                // optional
            }
        }
        if (all_loaded) {
            // 记录一个数字的尺寸（假设同一主题数字尺寸一致）
            const lv_image_dsc_t* d = digit_images_[0] ? digit_images_[0]->image_dsc() : nullptr;
            if (d) {
                ESP_LOGI(TAG, "All digit images loaded successfully, digit size=%dx%d cf=%d", (int)d->header.w, (int)d->header.h, (int)d->header.cf);
            } else {
                ESP_LOGI(TAG, "All digit images loaded successfully");
            }
        } else {
            ESP_LOGW(TAG, "Some digit images failed to load, font fallback may be used");
        }
        // 加载完成（冒号按需加载，仅 green）
        digits_loaded_ = true;
    }

    std::string MakeNumberPath(int d) const {
        char buf[128];
        snprintf(buf, sizeof(buf), "clock_faces/%s/number/%d.png", active_face_name_.c_str(), d);
        return std::string(buf);
    }
    std::string MakeColonPath() const {
        return std::string("clock_faces/") + active_face_name_ + "/number/colon.png";
    }

    static bool LoadImageTo(std::unique_ptr<LvglImage>& out_img, const std::string& path_png) {
        void* ptr = nullptr; size_t size = 0; auto& assets = Assets::GetInstance();
        // 尝试完整路径 -> @前缀 -> 斜杠转下划线 -> basename
        std::string n1 = path_png; for (auto &ch : n1) if (ch == '\\') ch = '/';
        std::string matched_key;
        auto try_key = [&](const std::string& key)->bool{ if (assets.GetAssetData(key, ptr, size)) { matched_key = key; return true; } return false; };
        bool ok = try_key(n1) || try_key(std::string("@") + n1) || ({ std::string t=n1; for (auto &c:t) if (c=='/') c='_'; try_key(t); }) || ({ auto p=n1.find_last_of('/'); p!=std::string::npos && try_key(n1.substr(p+1)); });
        if (ok) {
            try {
                // 将数字图片也改为可释放缓冲，避免跨主题缓存/指针残留
                void* copy_buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (copy_buf) memcpy(copy_buf, ptr, size);
                out_img = std::make_unique<LvglAllocatedImage>(copy_buf, size);
                ESP_LOGI(TAG, "Digit image loaded, key=%s", matched_key.c_str());
                return true;
            } catch (...) {
                out_img.reset();
            }
        }
        ESP_LOGW(TAG, "Digit asset not found (compat search): %s", path_png.c_str());
        return false;
    }

    void UpdateTime() {
        EnsureDigitsLoaded();
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        if (!tm_info || tm_info->tm_year < 2025 - 1900) return;
        int h = tm_info->tm_hour;
        int m = tm_info->tm_min;
        int d0 = h / 10;
        int d1 = h % 10;
        int d2 = m / 10;
        int d3 = m % 10;

        // 检查4位数字是否都可用（不需要冒号）
        bool digits_ok = digit_images_[d0] && digit_images_[d1] && digit_images_[d2] && digit_images_[d3];
        if (digits_ok && !use_text_time_) {
            // 图片渲染：仅显示4位数字，不显示冒号
            SetDigit(0, d0);
            SetDigit(1, d1);
            SetDigit(2, d2);
            SetDigit(3, d3);
            // 确保前4个位置可见
            for (int i = 0; i < 4; ++i) {
                if (digit_img_[i]) lv_obj_clear_flag(digit_img_[i], LV_OBJ_FLAG_HIDDEN);
            }
            // green: 显示冒号 PNG（若已加载）；其他主题隐藏
            if (active_face_name_ == "green" && colon_image_ && colon_image_->image_dsc()) {
                SetColon(4);
                lv_obj_clear_flag(digit_img_[4], LV_OBJ_FLAG_HIDDEN);
                if (use_absolute_positions_) {
                    if (!has_colon_pos_) {
                        int cx = (pos_[1][0] + pos_[2][0]) / 2;
                        int cy = (pos_[1][1] + pos_[2][1]) / 2;
                        lv_obj_align(digit_img_[4], LV_ALIGN_CENTER, cx, cy);
                    }
                } else {
                    lv_obj_align(digit_img_[4], LV_ALIGN_CENTER, 0, 0);
                }
            } else {
                if (digit_img_[4]) lv_obj_add_flag(digit_img_[4], LV_OBJ_FLAG_HIDDEN);
            }
            // 隐藏可能的时间文本层
            if (time_text_) {
                lv_obj_add_flag(time_text_, LV_OBJ_FLAG_HIDDEN);
            }
            // green 主题：显示日期（默认字体），位于时间数字下方
            if (active_face_name_ == "green") UpdateAndShowDate(tm_info, /*is_digits_mode=*/true); else { if (date_text_) lv_obj_add_flag(date_text_, LV_OBJ_FLAG_HIDDEN); if (date_line_) lv_obj_add_flag(date_line_, LV_OBJ_FLAG_HIDDEN); }
        } else {
            // 降级到字体渲染，确保不空白
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
            // 文本渲染：使用可配置缩放与位置，并确保日期可见
            if (!time_text_) {
                time_text_ = lv_label_create(container_);
                lv_obj_set_style_text_align(time_text_, LV_TEXT_ALIGN_CENTER, 0);
                // 使用默认字体并放大
                lv_obj_set_style_text_font(time_text_, LV_FONT_DEFAULT, 0);
                lv_obj_set_style_transform_zoom(time_text_, time_zoom_, 0);
            }
            // 每次更新时间时重新对齐，避免缩放后偏移
            lv_obj_align(time_text_, LV_ALIGN_CENTER, 0, time_y_offset_);
            lv_label_set_text(time_text_, buf);
            lv_obj_clear_flag(time_text_, LV_OBJ_FLAG_HIDDEN);
            // 日期标签：仅 green 主题显示；位于时间下方
            if (active_face_name_ == "green") UpdateAndShowDate(tm_info, /*is_digits_mode=*/false); else { if (date_text_) lv_obj_add_flag(date_text_, LV_OBJ_FLAG_HIDDEN); if (date_line_) lv_obj_add_flag(date_line_, LV_OBJ_FLAG_HIDDEN); }
            // 隐藏图片层以避免覆盖
            for (int i = 0; i < 5; ++i) {
                if (digit_img_[i]) lv_obj_add_flag(digit_img_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // 统一更新/显示日期（仅 green 调用）
    void UpdateAndShowDate(const struct tm* tm_info, bool is_digits_mode) {
        if (!date_text_) {
            date_text_ = lv_label_create(container_);
            lv_obj_set_style_text_align(date_text_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(date_text_, lv_color_hex(0xDBC038), 0);
            lv_obj_set_style_transform_zoom(date_text_, date_zoom_, 0);
        }
        const char* w = WeekdayCnSafe(tm_info ? tm_info->tm_wday : 0);
        char date_buf[32];
        snprintf(date_buf, sizeof(date_buf), "%d/%d %s", (tm_info ? tm_info->tm_mon + 1 : 1), (tm_info ? tm_info->tm_mday : 1), w);
        lv_label_set_text(date_text_, date_buf);
        // 创建并更新日期上方细线
        if (!date_line_) {
            date_line_ = lv_obj_create(container_);
            lv_obj_set_style_bg_color(date_line_, lv_color_hex(0xDBC038), 0);
            lv_obj_set_style_bg_opa(date_line_, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(date_line_, 0, 0);
            lv_obj_set_style_radius(date_line_, 0, 0);
            lv_obj_set_height(date_line_, 1);
        }
        if (is_digits_mode) {
            if (use_absolute_positions_) {
                int bottom_y = pos_[0][1];
                for (int i = 1; i < 4; ++i) bottom_y = bottom_y > pos_[i][1] ? bottom_y : pos_[i][1];
                lv_obj_align(date_text_, LV_ALIGN_CENTER, 0, bottom_y + date_gap_ + 36);
            } else if (time_container_) {
                lv_obj_align_to(date_text_, time_container_, LV_ALIGN_OUT_BOTTOM_MID, 0, date_gap_ + 36);
            } else {
                lv_obj_align(date_text_, LV_ALIGN_CENTER, 0, date_gap_ + 36);
            }
        } else {
            // 文本时间：跟随 time_text_
            if (time_text_) lv_obj_align_to(date_text_, time_text_, LV_ALIGN_OUT_BOTTOM_MID, 0, date_gap_ + 36);
            else lv_obj_align(date_text_, LV_ALIGN_CENTER, 0, date_gap_ + 36);
        }
        // 设置细线长度略长于日期文本
        lv_coord_t wtxt = lv_obj_get_width(date_text_);
        lv_coord_t line_w = wtxt + 30; // 日期文本宽度 + 30 像素
        if (line_w < 30) line_w = 30;
        lv_obj_set_width(date_line_, line_w);
        // 细线放在日期正上方，间隔 4px
        lv_obj_align_to(date_line_, date_text_, LV_ALIGN_OUT_TOP_MID, 0, -4);
        lv_obj_clear_flag(date_line_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(date_text_, LV_OBJ_FLAG_HIDDEN);
    }

    static const char* WeekdayCnSafe(int wday) {
        static const char* kWeekdayCn[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        if (wday < 0 || wday > 6) return kWeekdayCn[0];
        return kWeekdayCn[wday];
    }

    void SetDigit(int pos, int d) {
        if (pos < 0 || pos >= 5 || d < 0 || d > 9) return;
        if (!digit_img_[pos]) return;  // 确保 UI 对象存在
        if (!digit_images_[d]) {
            ESP_LOGW(TAG, "Digit image %d not loaded, hiding position %d", d, pos);
            lv_obj_add_flag(digit_img_[pos], LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if (!digit_images_[d]->image_dsc()) {
            ESP_LOGW(TAG, "Digit image %d has invalid descriptor", d);
            lv_obj_add_flag(digit_img_[pos], LV_OBJ_FLAG_HIDDEN);
            return;
        }
        lv_image_set_src(digit_img_[pos], digit_images_[d]->image_dsc());
        if (use_absolute_positions_) ApplyPositions();
    }

    void SetColon(int pos) {
        if (pos < 0 || pos >= 5) return;
        if (!colon_image_) return;
        lv_image_set_src(digit_img_[pos], colon_image_->image_dsc());
        if (use_absolute_positions_) ApplyPositions();
    }

    void ApplyPositions() {
        // 将 pos_ 中的相对中心位移应用到每个 digit_img_
        for (int i = 0; i < 5; ++i) {
            lv_obj_align(digit_img_[i], LV_ALIGN_CENTER, pos_[i][0], pos_[i][1]);
        }
    }

    // 取消数字缩放：保持图片原始尺寸

private:
    static constexpr const char* TAG = "ClockFacePixelThinking";
    // 切换模式状态
    bool switch_mode_ = false;
    std::vector<std::string> faces_;
    int current_face_index_ = -1;     // 正在使用
    int preview_face_index_ = -1;     // 预览中的
    bool touch_active_ = false;       // 切换界面内是否有手指按住/滑动
    uint32_t last_interaction_tick_ = 0; // 最近一次交互的 tick
    bool tap_pending_ = false;        // 判断是否是一次短按
    bool long_pressed_detected_ = false; // 是否检测到长按
    lv_obj_t* confirm_btn_ = nullptr;
    lv_obj_t* cancel_btn_ = nullptr;  // 预留（当前未显示）
    lv_obj_t* face_name_label_ = nullptr; // 预留（当前未使用）
    lv_obj_t* roller_ = nullptr;      // 主题滚轮
    lv_timer_t* auto_confirm_timer_ = nullptr; // 3s 无操作自动确认
    std::string active_before_switch_;

    void EnterSwitchMode() {
        if (switch_mode_) return;
        switch_mode_ = true;
        ESP_LOGI(TAG, "EnterSwitchMode: open theme roller");
        active_before_switch_ = active_face_name_;
        // 不再缩放容器，保持原尺寸
        // 去除阴影框，避免出现“两个框”的观感
        lv_obj_set_style_shadow_width(container_, 0, 0);
        // 进入主题切换模式：背景改为黑色，字体改为白色（在滚轮样式中设置）
        lv_obj_set_style_bg_color(container_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(container_, 0, 0); // 去掉圆角，避免四角露白
        // 进入列表选择模式：不使用确认按钮（由自动确认负责）
        // 隐藏背景与时间
        if (gif_) gif_->Pause();
        if (background_img_) lv_obj_add_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 5; ++i) if (digit_img_[i]) lv_obj_add_flag(digit_img_[i], LV_OBJ_FLAG_HIDDEN);
        if (time_text_) lv_obj_add_flag(time_text_, LV_OBJ_FLAG_HIDDEN);
        if (date_text_) lv_obj_add_flag(date_text_, LV_OBJ_FLAG_HIDDEN);
        if (date_line_) lv_obj_add_flag(date_line_, LV_OBJ_FLAG_HIDDEN);
        // 枚举主题
        faces_ = Assets::GetInstance().ListClockFaces();
        // 记录可用主题，便于诊断资源是否打包完整
        {
            std::string joined;
            for (size_t i = 0; i < faces_.size(); ++i) {
                if (i) joined += ", ";
                joined += faces_[i];
            }
            ESP_LOGI(TAG, "Available clock faces: %d [%s]", (int)faces_.size(), joined.c_str());
        }
        if (faces_.empty()) {
            // 至少包含当前主题，避免滑动无效
            faces_.push_back(active_face_name_);
        }
        // 定位当前主题索引
        std::string now = active_face_name_;
        current_face_index_ = 0;
        for (int i = 0; i < (int)faces_.size(); ++i) if (faces_[i] == now) { current_face_index_ = i; break; }
        preview_face_index_ = current_face_index_;

        // 创建滚轮控件（仅显示主题名，不加载图片）
        if (roller_) { lv_obj_del(roller_); roller_ = nullptr; }
        roller_ = lv_roller_create(container_);
        // 拼接选项
        {
            std::string opts;
            for (size_t i = 0; i < faces_.size(); ++i) {
                if (i) opts += '\n';
                opts += faces_[i];
            }
            lv_roller_set_options(roller_, opts.c_str(), LV_ROLLER_MODE_INFINITE);
        }
        lv_obj_set_width(roller_, width_); // 选中条与屏幕同宽
        lv_roller_set_visible_row_count(roller_, 5);
        lv_obj_align(roller_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_move_foreground(roller_);
        // 样式：主区透明（容器背景已是黑色），文本白色；选中行为半透明黑条并高亮文字
        lv_obj_set_style_bg_opa(roller_, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(roller_, 0, LV_PART_MAIN);
        lv_obj_set_style_outline_width(roller_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(roller_, 0, LV_PART_MAIN);
        lv_obj_set_style_text_color(roller_, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_opa(roller_, LV_OPA_COVER, LV_PART_MAIN);
        // 选中行：半透明黑底+纯白字，确保对比度
        lv_obj_set_style_bg_color(roller_, lv_color_hex(0xB0B0B0), LV_PART_SELECTED); // 浅灰
        lv_obj_set_style_bg_opa(roller_, LV_OPA_60, LV_PART_SELECTED); // 半透明
        lv_obj_set_style_border_width(roller_, 0, LV_PART_SELECTED);
        lv_obj_set_style_outline_width(roller_, 0, LV_PART_SELECTED);
        lv_obj_set_style_radius(roller_, 0, LV_PART_SELECTED);
        lv_obj_set_style_text_color(roller_, lv_color_hex(0xFFFFFF), LV_PART_SELECTED);
        lv_obj_set_style_text_opa(roller_, LV_OPA_COVER, LV_PART_SELECTED);
        // 文本排版：避免因字距/行距导致的观感模糊
        lv_obj_set_style_text_letter_space(roller_, 0, LV_PART_MAIN);
        lv_obj_set_style_text_letter_space(roller_, 0, LV_PART_SELECTED);
        lv_roller_set_selected(roller_, preview_face_index_, LV_ANIM_OFF);
        lv_obj_update_layout(container_);
        // 事件：滚动时更新索引并重置自动确认计时
        lv_obj_add_event_cb(roller_, [](lv_event_t* e){
            auto self = static_cast<ClockFacePixelThinkingCore*>(lv_event_get_user_data(e));
            if (!self) return;
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
                code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT || code == LV_EVENT_RELEASED) {
                int sel = lv_roller_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e)));
                self->preview_face_index_ = (sel % (int)self->faces_.size() + (int)self->faces_.size()) % (int)self->faces_.size();
                if (self->auto_confirm_timer_) lv_timer_reset(self->auto_confirm_timer_);
            }
        }, LV_EVENT_ALL, this);
        // 无触摸3s自动确认：仅在 touch_active_ 为 false 时才触发
        if (auto_confirm_timer_) { lv_timer_del(auto_confirm_timer_); auto_confirm_timer_ = nullptr; }
        auto_confirm_timer_ = lv_timer_create([](lv_timer_t* t){
            auto self = static_cast<ClockFacePixelThinkingCore*>(lv_timer_get_user_data(t));
            if (!self) return;
            if (!self->touch_active_) {
                self->ConfirmSwitch();
            } else {
                // 仍在触摸中，继续等待
            }
        }, 3000, this);
    }

    void ExitSwitchMode() {
        switch_mode_ = false;
        // 无需复位缩放
        lv_obj_set_style_shadow_width(container_, 0, 0);
        // 不再使用确认按钮
        if (roller_) { lv_obj_del(roller_); roller_ = nullptr; }
        if (auto_confirm_timer_) { lv_timer_del(auto_confirm_timer_); auto_confirm_timer_ = nullptr; }
        // 恢复原背景可见（由确认流程最终决定显示内容）
    }

    void NextFace() { if (!switch_mode_ || faces_.empty()) return; preview_face_index_ = (preview_face_index_ + 1) % faces_.size(); PreviewFace(faces_[preview_face_index_]); }
    void PrevFace() { if (!switch_mode_ || faces_.empty()) return; preview_face_index_ = (preview_face_index_ - 1 + faces_.size()) % faces_.size(); PreviewFace(faces_[preview_face_index_]); }

    void PreviewFace(const std::string& name) {
        // 改为滚轮 UI，不做图片预览，仅更新选中索引由滚轮事件处理。
        (void)name;
    }

    void ConfirmSwitch() {
        if (!switch_mode_) return;
        if (roller_) {
            int sel = lv_roller_get_selected(roller_);
            preview_face_index_ = (sel % (int)faces_.size() + (int)faces_.size()) % (int)faces_.size();
        }
        if (preview_face_index_ >= 0 && preview_face_index_ < (int)faces_.size()) {
            active_face_name_ = faces_[preview_face_index_];
            Settings s("display", true);
            s.SetString("clock_face", active_face_name_);
            // 立即重新加载并显示
            // 彻底释放旧的 LVGL 对象源，避免缓存持有旧指针
            if (gif_) gif_->Stop();
            if (background_img_) { lv_image_set_src(background_img_, NULL); lv_obj_add_flag(background_img_, LV_OBJ_FLAG_HIDDEN); }
            for (int i = 0; i < 5; ++i) {
                if (digit_img_[i]) { lv_image_set_src(digit_img_[i], NULL); lv_obj_add_flag(digit_img_[i], LV_OBJ_FLAG_HIDDEN); }
            }
            face_loaded_ = false; background_loaded_ = false; gif_.reset(); background_image_.reset();
            digits_loaded_ = false;
            for (auto &img : digit_images_) img.reset();
            // 丢弃图像缓存以避免跨主题重用
            lv_image_cache_drop(lv_image_get_src(background_img_));
            for (int i = 0; i < 5; ++i) {
                if (digit_img_[i]) lv_image_cache_drop(lv_image_get_src(digit_img_[i]));
            }
            lv_obj_invalidate(container_);
            LoadFaceConfig();
            // 根据新主题的定位方式，重新配置数字图片的父容器与布局
            if (use_absolute_positions_) {
                if (time_container_) lv_obj_add_flag(time_container_, LV_OBJ_FLAG_HIDDEN);
                for (int i = 0; i < 5; ++i) {
                    if (digit_img_[i]) {
                        if (lv_obj_get_parent(digit_img_[i]) != container_) lv_obj_set_parent(digit_img_[i], container_);
                        lv_obj_remove_style_all(digit_img_[i]);
                    }
                }
                ApplyPositions();
            } else {
                if (time_container_) lv_obj_clear_flag(time_container_, LV_OBJ_FLAG_HIDDEN);
                for (int i = 0; i < 5; ++i) {
                    if (digit_img_[i] && lv_obj_get_parent(digit_img_[i]) != time_container_) lv_obj_set_parent(digit_img_[i], time_container_);
                }
            }
            LoadBackgroundIfNeeded();
            EnsureDigitsLoaded();
            UpdateTime();
            // 统一再应用一次坐标，确保冒号与数字同步
            if (use_absolute_positions_) ApplyPositions();
            // 确认后强制更新布局并再应用一次绝对定位，减少偶发错位
            lv_obj_update_layout(container_);
            if (use_absolute_positions_) ApplyPositions();
            // 可选：立即刷新一帧，减少残影
            lv_obj_invalidate(container_);
            lv_refr_now(nullptr);
        }
        ExitSwitchMode();
    }

    std::string active_face_name_ = "PixelThinking";
    bool loaded_from_settings_ = false;
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* background_img_ = nullptr;
    lv_obj_t* time_container_ = nullptr;
    lv_obj_t* digit_img_[5] = {nullptr};
    int width_ = 0;
    int height_ = 0;

    std::unique_ptr<LvglGif> gif_;
    std::unique_ptr<LvglImage> background_image_;
    lv_timer_t* tick_timer_ = nullptr;
    bool timer_aligned_ = false;
    LoopMode loop_mode_ = LOOP_INFINITE;

    bool digits_loaded_ = false;
    std::array<std::unique_ptr<LvglImage>, 10> digit_images_;
    std::unique_ptr<LvglImage> colon_image_;
    lv_obj_t* time_text_ = nullptr;
    lv_obj_t* date_text_ = nullptr;
    lv_obj_t* date_line_ = nullptr;

    bool face_loaded_ = false;
    bool background_loaded_ = false;
    bool use_gif_background_ = false;
    bool use_absolute_positions_ = false;
    std::array<std::array<int,2>, 5> pos_{{{0,0},{0,0},{0,0},{0,0},{0,0}}};
    bool has_colon_pos_ = false;
    // 文本时间模式配置
    bool use_text_time_ = false;
    int time_zoom_ = 384;      // 1.5x
    int date_zoom_ = 256;      // 1.0x
    int time_y_offset_ = 0;    // 垂直偏移
    int date_gap_ = 6;         // 时间与日期的间距
};

extern "C" {
    void* PixelThinkingClock_Create(void* parent, int width, int height) {
        auto* obj = new ClockFacePixelThinkingCore((lv_obj_t*)parent, width, height);
        return static_cast<void*>(obj);
    }

    void PixelThinkingClock_Show(void* ctx) {
        if (!ctx) return;
        static_cast<ClockFacePixelThinkingCore*>(ctx)->Show();
    }

    void PixelThinkingClock_Hide(void* ctx) {
        if (!ctx) return;
        static_cast<ClockFacePixelThinkingCore*>(ctx)->Hide();
    }

    bool PixelThinkingClock_LoadBackground(void* ctx, const char* asset_name) {
        if (!ctx) return false;
        return static_cast<ClockFacePixelThinkingCore*>(ctx)->LoadBackground(asset_name ? asset_name : std::string());
    }

    void PixelThinkingClock_SetLoop(void* ctx, int loop_mode) {
        if (!ctx) return;
        auto* obj = static_cast<ClockFacePixelThinkingCore*>(ctx);
        if (loop_mode > 1) loop_mode = -1; // normalize
        obj->SetLoopMode(loop_mode == 0 ? ClockFacePixelThinkingCore::LOOP_NONE : (loop_mode == 1 ? ClockFacePixelThinkingCore::LOOP_ONCE : ClockFacePixelThinkingCore::LOOP_INFINITE));
    }
}


