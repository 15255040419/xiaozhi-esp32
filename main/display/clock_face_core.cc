#include "lvgl.h"
#include <esp_timer.h>
#include <esp_log.h>
#include <string>
#include <memory>
#include <array>
#include <set>
#include <algorithm>
#include <cJSON.h>
#include <dirent.h>
#include <ctype.h>
#include <limits>
#include <stdint.h>

#include "assets.h"
#include "display/lvgl_display/lvgl_image.h"
#include "display/lvgl_display/gif/lvgl_gif.h"
#include "settings.h"
#include "board.h"

// PixelThinking 风格的时钟界面（核心实现，C 接口见底部）

static bool g_clock_face_active = false;

extern "C" bool clock_face_is_active() {
    return g_clock_face_active;
}

class ClockFaceCore {
public:
    enum LoopMode {
        LOOP_NONE = 0,
        LOOP_ONCE = 1,
        LOOP_INFINITE = -1
    };

    ClockFaceCore(lv_obj_t* parent, int width, int height)
        : parent_(parent), width_(width), height_(height) {
        // 先加载配置（仅解析JSON，不加载资源）
        LoadFaceConfig();
        CreateUI();
    }

    ~ClockFaceCore() {
        Stop();
        if (gif_) {
            gif_.reset();
        }
        StopTick();
        if (container_) lv_obj_del(container_);
    }

    // 统一清理当前背景/数字/冒号/GIF并丢弃缓存
    void ClearImagesAndCache() {
        if (gif_) gif_->Stop();
        if (gif_sd_buf_) { heap_caps_free(gif_sd_buf_); gif_sd_buf_ = nullptr; gif_sd_size_ = 0; }
        // 背景
        if (background_img_) {
            const void* bg_src = lv_image_get_src(background_img_);
            if (bg_src) lv_image_cache_drop(bg_src);
            lv_image_set_src(background_img_, NULL);
            lv_obj_add_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
        }
        // 数字/冒号
        for (int i = 0; i < 5; ++i) {
            if (digit_img_[i]) {
                const void* s = lv_image_get_src(digit_img_[i]);
                if (s) lv_image_cache_drop(s);
                lv_image_set_src(digit_img_[i], NULL);
                lv_obj_add_flag(digit_img_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        for (auto &img : digit_images_) img.reset();
        colon_image_.reset();
        gif_.reset();
        background_image_.reset();
        digits_loaded_ = false;
        background_loaded_ = false;
        face_loaded_ = false;
    }

    void Show() {
        if (container_) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(container_);
        }
        g_clock_face_active = true;
        // 提升 LVGL image cache，减小首次加载抖动
        lv_image_cache_resize(2 * 1024 * 1024, true);
        // 同时支持两种模式：
        // 1) face.json 提供 4 个坐标 -> 使用绝对定位（隐藏 flex 容器）
        // 2) 未提供 -> 使用原来的水平 FLEX 布局
        // 从设置读取默认主题
        if (!loaded_from_settings_) {
            Settings s("display", false);
            std::string face = s.GetString("clock_face", active_face_name_);
            if (!face.empty()) {
                for (auto &c : face) c = (char)tolower((unsigned char)c);
                active_face_name_ = face;
            }
            loaded_from_settings_ = true;
        }
        // 若未插 SD，而上次保存的是 SD 主题，则回退到内置 flower
        if (!IsSdMounted() && active_face_name_ != "flower") {
            ESP_LOGW(TAG, "SD not mounted; fallback to built-in theme 'flower' (was '%s')", active_face_name_.c_str());
            active_face_name_ = "flower";
            Settings s("display", true);
            s.SetString("clock_face", active_face_name_);
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
        ClearImagesAndCache();
        // 降低 image cache，释放缓存
        lv_image_cache_resize(512 * 1024, true);
        if (container_) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
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

    // 辅助：解析 SD 卡 clock_faces 目录（兼容 8.3 短名）
    static std::string ResolveSdClockFacesDir() {
        const char* kDefault = "/sdcard/clock_faces";
        DIR* d = opendir(kDefault);
        if (d) { closedir(d); return kDefault; }
        DIR* root = opendir("/sdcard");
        if (!root) return kDefault;
        std::string base = kDefault;
        struct dirent* e;
        while ((e = readdir(root)) != nullptr) {
            const char* n = e->d_name; if (!n) continue;
            std::string lower = n; for (auto &c : lower) c = (char)tolower((unsigned char)c);
            if (lower == "clock_faces" || lower.rfind("clock", 0) == 0) { base = std::string("/sdcard/") + n; break; }
        }
        closedir(root);
        return base;
    }

    static bool IsSdMounted() {
        DIR* root = opendir("/sdcard");
        if (root) { closedir(root); return true; }
        return false;
    }

    // 在 SD 的 clock_faces 目录下解析实际主题目录（大小写不敏感）
    static std::string ResolveSdThemeDir(const std::string& base_dir, const std::string& theme_name) {
        std::string wanted = theme_name;
        for (auto &c : wanted) c = (char)tolower((unsigned char)c);
        std::string fallback = base_dir + "/" + theme_name;
        DIR* d = opendir(base_dir.c_str());
        if (!d) return fallback;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            const char* n = e->d_name; if (!n || n[0]=='.') continue;
            std::string lower = n; for (auto &c : lower) c = (char)tolower((unsigned char)c);
            if (lower == wanted) { std::string real = base_dir + "/" + n; closedir(d); return real; }
        }
        closedir(d);
        return fallback;
    }

    // 从 assets 或 SD 加载壁纸（精简且括号严格配平）
    bool LoadBackground(const std::string& bg_name) {
        auto& assets = Assets::GetInstance();
        void* ptr = nullptr;
        size_t size = 0;
        if (!background_img_) return false;
        if (bg_name.empty()) {
            lv_obj_add_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
            return true;
        }

        // 在加载新背景前，安全清理当前背景资源与缓存，保证与时间渲染解耦
        {
            if (gif_) gif_->Stop();
            if (gif_sd_buf_) { heap_caps_free(gif_sd_buf_); gif_sd_buf_ = nullptr; gif_sd_size_ = 0; }
            const void* prev = lv_image_get_src(background_img_);
            if (prev) lv_image_cache_drop(prev);
            lv_image_set_src(background_img_, NULL);
            background_image_.reset();
            gif_.reset();
        }

        // 直接传入了 SD 绝对路径：/sdcard/...
        if (bg_name.rfind("/sdcard/", 0) == 0) {
            std::string data;
            if (Assets::ReadFileFromSd(bg_name.c_str(), data)) {
                std::unique_ptr<LvglRawImage> probe = std::make_unique<LvglRawImage>((void*)data.data(), data.size());
                if (probe->IsGif()) {
                    // 为 GIF 分配持久缓冲，并直接基于原始数据创建 GIF，不走静态图片解码
                    if (gif_sd_buf_) { heap_caps_free(gif_sd_buf_); gif_sd_buf_ = nullptr; gif_sd_size_ = 0; }
                    void* copy_buf = heap_caps_malloc(data.size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (copy_buf) memcpy(copy_buf, data.data(), data.size());
                    gif_sd_buf_ = copy_buf;
                    gif_sd_size_ = data.size();
                    lv_img_dsc_t raw;
                    memset(&raw, 0, sizeof(raw));
                    raw.data = static_cast<const uint8_t*>(gif_sd_buf_);
                    raw.data_size = gif_sd_size_;
                    raw.header.magic = LV_IMAGE_HEADER_MAGIC;
                    raw.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
                    gif_ = std::make_unique<LvglGif>(&raw);
                    if (gif_ && gif_->IsLoaded()) {
                        gif_->SetFrameCallback([this]() {
                            if (background_img_) {
                                lv_image_set_src(background_img_, gif_->image_dsc());
                                lv_obj_invalidate(background_img_);
                            }
                        });
                        lv_image_set_src(background_img_, gif_->image_dsc());
                        lv_obj_clear_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
                        SetLoopMode(loop_mode_);
                        gif_->Start();
                        return true;
                    }
                } else {
                    void* copy_buf = heap_caps_malloc(data.size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (copy_buf) memcpy(copy_buf, data.data(), data.size());
                    try {
                        background_image_ = std::make_unique<LvglAllocatedImage>(copy_buf, data.size());
                        if (background_image_ && background_image_->image_dsc()) {
                            lv_image_set_src(background_img_, background_image_->image_dsc());
                            lv_obj_clear_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
                            return true;
                        }
                    } catch (...) {
                        ESP_LOGE(TAG, "Background static image decode failed (SD abs)");
                        if (copy_buf) heap_caps_free(copy_buf);
                        background_image_.reset();
                    }
                }
            }
            // 若绝对路径失败，继续走下面的兼容逻辑
        }

        // 优先尝试 SD：将 "clock/<face>/..." 映射到实际 SD 主题目录（大小写不敏感）
        {
            std::string rel = bg_name; for (auto &ch : rel) if (ch == '\\') ch = '/';
            const std::string prefix = "clock/";
            if (rel.rfind(prefix, 0) == 0) rel = rel.substr(prefix.size());
            std::string face_dir = rel;
            size_t slash = face_dir.find('/');
            std::string rest;
            if (slash != std::string::npos) { rest = face_dir.substr(slash + 1); face_dir = face_dir.substr(0, slash); }
            std::string base_dir = ResolveSdClockFacesDir();
            std::string theme_dir = ResolveSdThemeDir(base_dir, face_dir);
            std::string sd_path = theme_dir + (rest.empty() ? std::string("") : std::string("/") + rest);
            std::string data;
            if (!sd_path.empty() && Assets::ReadFileFromSd(sd_path.c_str(), data)) {
                std::unique_ptr<LvglRawImage> probe = std::make_unique<LvglRawImage>((void*)data.data(), data.size());
                if (probe->IsGif()) {
                    if (gif_sd_buf_) { heap_caps_free(gif_sd_buf_); gif_sd_buf_ = nullptr; gif_sd_size_ = 0; }
                    void* copy_buf = heap_caps_malloc(data.size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (copy_buf) memcpy(copy_buf, data.data(), data.size());
                    gif_sd_buf_ = copy_buf;
                    gif_sd_size_ = data.size();
                    lv_img_dsc_t raw;
                    memset(&raw, 0, sizeof(raw));
                    raw.data = static_cast<const uint8_t*>(gif_sd_buf_);
                    raw.data_size = gif_sd_size_;
                    raw.header.magic = LV_IMAGE_HEADER_MAGIC;
                    raw.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
                    gif_ = std::make_unique<LvglGif>(&raw);
                    if (gif_ && gif_->IsLoaded()) {
                        gif_->SetFrameCallback([this]() {
                            if (background_img_) {
                                lv_image_set_src(background_img_, gif_->image_dsc());
                                lv_obj_invalidate(background_img_);
                            }
                        });
                        lv_image_set_src(background_img_, gif_->image_dsc());
                        lv_obj_clear_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
                        SetLoopMode(loop_mode_);
                        gif_->Start();
                        return true;
                    }
                } else {
                    void* copy_buf = heap_caps_malloc(data.size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (copy_buf) memcpy(copy_buf, data.data(), data.size());
                    try {
                        background_image_ = std::make_unique<LvglAllocatedImage>(copy_buf, data.size());
                        if (background_image_ && background_image_->image_dsc()) {
                            lv_image_set_src(background_img_, background_image_->image_dsc());
                            lv_obj_clear_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
                            return true;
                        }
                    } catch (...) {
                        ESP_LOGE(TAG, "Background static image decode failed (SD mapped)");
                        if (copy_buf) heap_caps_free(copy_buf);
                        background_image_.reset();
                    }
                }
            }
        }

        // 尝试内置资源：先用给定路径，再尝试同目录候选名，再尝试扁平化(clock_*)文件名
        std::string matched_key;
        auto try_load_exact = [&](const std::string& name)->bool{
            std::string key = name; for (auto &ch : key) if (ch == '\\') ch = '/';
            // 1) 直接键
            if (assets.GetAssetData(key, ptr, size)) { matched_key = key; return true; }
            // 2) @前缀
            {
                std::string at = std::string("@") + key; if (assets.GetAssetData(at, ptr, size)) { matched_key = at; return true; }
            }
            // 3) assets/ 前缀
            {
                std::string ak = std::string("assets/") + key; if (assets.GetAssetData(ak, ptr, size)) { matched_key = ak; return true; }
            }
            // 4) 扁平化（clock/flower/bg/1.gif -> clock_flower_bg_1.gif）
            std::string flat = key; for (auto &ch : flat) if (ch == '/') ch = '_';
            if (assets.GetAssetData(flat, ptr, size)) { matched_key = flat; return true; }
            std::string af = std::string("assets/") + flat; if (assets.GetAssetData(af, ptr, size)) { matched_key = af; return true; }
            return false;
        };

        // 先尝试当前主题（仅精确路径）
        if (!try_load_exact(bg_name)) {
            // 再尝试当前主题目录下的常见命名（gif/png）
            std::string base; auto slash = bg_name.find_last_of('/'); if (slash != std::string::npos) base = bg_name.substr(0, slash + 1);
            bool ok = false;
            for (int ci = 0; ci < 4; ++ci) {
                if (try_load_exact(base + kBgCandidates_[ci])) { ok = true; break; }
            }
            if (!ok) {
                ESP_LOGW(TAG, "Background not found in theme: %s", bg_name.c_str());
                return false;
            }
        }

        // 解码并设置
        std::unique_ptr<LvglImage> raw = std::make_unique<LvglRawImage>(ptr, size);
        if (raw->IsGif()) {
            gif_ = std::make_unique<LvglGif>(raw->image_dsc());
            if (gif_->IsLoaded()) {
                gif_->SetFrameCallback([this]() {
                    if (background_img_) {
                        lv_image_set_src(background_img_, gif_->image_dsc());
                        lv_obj_invalidate(background_img_);
                    }
                });
                lv_image_set_src(background_img_, gif_->image_dsc());
                lv_obj_clear_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
                SetLoopMode(loop_mode_);
                gif_->Start();
                return true;
            }
            gif_.reset();
        }
        void* copy_buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (copy_buf) memcpy(copy_buf, ptr, size);
        try {
            background_image_ = std::make_unique<LvglAllocatedImage>(copy_buf, size);
            if (!background_image_ || !background_image_->image_dsc()) {
                ESP_LOGE(TAG, "Failed to decode background image (assets)");
                return false;
            }
            lv_image_set_src(background_img_, background_image_->image_dsc());
            lv_obj_clear_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
            return true;
        } catch (...) {
            ESP_LOGE(TAG, "Background static image decode failed (assets)");
            if (copy_buf) heap_caps_free(copy_buf);
            background_image_.reset();
            return false;
        }
    }

private:
    // 共享背景候选名（优先 gif，再 png，再 jpg）
    static constexpr const char* kBgCandidates_[4] = {
        "bg.gif", "background.gif", "bg.png", "background.png"
    };

    // （已不再使用的辅助函数移除）

        // 统一解析 face.json 内容
    bool ParseFaceJsonBuffer(const char* buf, size_t len) {
        if (!buf || len == 0) return false;
        cJSON* root = cJSON_ParseWithLength(buf, len);
        if (!root) return false;
        auto cleanup = [&](){ cJSON_Delete(root); };

        // use_gif_background
        cJSON* gif_bg = cJSON_GetObjectItem(root, "use_gif_background");
        if (cJSON_IsString(gif_bg)) {
            std::string v = gif_bg->valuestring; for (auto &ch : v) ch = (char)tolower((unsigned char)ch);
            use_gif_background_ = (v == "yes" || v == "true" || v == "1");
        }
        // background_loop
        cJSON* loop = cJSON_GetObjectItem(root, "background_loop");
        if (cJSON_IsString(loop)) {
            std::string v = loop->valuestring; for (auto &ch : v) ch = (char)tolower((unsigned char)ch);
            if (v == "once") loop_mode_ = LOOP_ONCE; else if (v == "none") loop_mode_ = LOOP_NONE; else loop_mode_ = LOOP_INFINITE;
        } else if (cJSON_IsNumber(loop)) {
            int n = loop->valueint; loop_mode_ = (n == 0 ? LOOP_NONE : (n == 1 ? LOOP_ONCE : LOOP_INFINITE));
        }
        // use_text_time
        cJSON* txt = cJSON_GetObjectItem(root, "use_text_time");
        if (cJSON_IsString(txt)) {
            std::string v = txt->valuestring; for (auto &ch : v) ch = (char)tolower((unsigned char)ch);
            use_text_time_ = (v == "yes" || v == "true" || v == "1");
        } else if (cJSON_IsBool(txt)) {
            use_text_time_ = cJSON_IsTrue(txt);
        } else if (cJSON_IsNumber(txt)) {
            use_text_time_ = (txt->valueint != 0);
        }
        // optional text params（缩放已停用，仅保留 date_gap/time_y 可选偏移）
        auto read_int = [&](const char* key, int &out){ cJSON* n = cJSON_GetObjectItem(root, key); if (cJSON_IsNumber(n)) out = n->valueint; };
        read_int("text_time_y", time_y_offset_);
        read_int("text_date_gap", date_gap_);
        // positions
        auto read_xy = [&](const char* key, std::array<int,2>& out) -> bool {
            cJSON* a = cJSON_GetObjectItem(root, key);
            if (!a || !cJSON_IsArray(a) || cJSON_GetArraySize(a) != 2) return false;
            cJSON* x = cJSON_GetArrayItem(a, 0);
            cJSON* y = cJSON_GetArrayItem(a, 1);
            if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) return false;
            out[0] = x->valueint; out[1] = y->valueint; return true;
        };
        std::array<int, 2> pos_h_a{0,0}, pos_h_b{0,0}, pos_m_a{0,0}, pos_m_b{0,0}, pos_colon{0,0};
        bool ok = true;
        ok &= read_xy("pos_clock_hour_a", pos_h_a);
        ok &= read_xy("pos_clock_hour_b", pos_h_b);
        ok &= read_xy("pos_clock_min_a", pos_m_a);
        ok &= read_xy("pos_clock_min_b", pos_m_b);
        has_colon_pos_ = read_xy("pos_clock_colon", pos_colon);
        // 兼容简写键名：ha/hb/ma/mb/colon
        if (!ok) {
            ok = true;
            ok &= read_xy("ha", pos_h_a);
            ok &= read_xy("hb", pos_h_b);
            ok &= read_xy("ma", pos_m_a);
            ok &= read_xy("mb", pos_m_b);
            has_colon_pos_ = read_xy("colon", pos_colon) || has_colon_pos_;
        }
        // 兼容数组形式：pos 或 positions: [[hx,hy],[hx2,hy2],[mx,my],[mx2,my2],[cx,cy]]
        if (!ok) {
            auto try_array = [&](const char* key)->bool{
                cJSON* arr = cJSON_GetObjectItem(root, key);
                if (!arr || !cJSON_IsArray(arr)) return false;
                int n = cJSON_GetArraySize(arr);
                if (n < 4) return false;
                auto read_pair = [&](int idx, std::array<int,2>& out)->bool{
                    cJSON* a = cJSON_GetArrayItem(arr, idx);
                    if (!a || !cJSON_IsArray(a) || cJSON_GetArraySize(a) != 2) return false;
                    cJSON* x = cJSON_GetArrayItem(a, 0);
                    cJSON* y = cJSON_GetArrayItem(a, 1);
                    if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) return false;
                    out[0] = x->valueint; out[1] = y->valueint; return true;
                };
                bool ok2 = true;
                ok2 &= read_pair(0, pos_h_a);
                ok2 &= read_pair(1, pos_h_b);
                ok2 &= read_pair(2, pos_m_a);
                ok2 &= read_pair(3, pos_m_b);
                if (n >= 5) { has_colon_pos_ = read_pair(4, pos_colon); }
                return ok2;
            };
            ok = try_array("pos") || try_array("positions");
        }
        if (ok) {
            use_absolute_positions_ = true;
            pos_[0] = pos_h_a; pos_[1] = pos_h_b; pos_[2] = pos_m_a; pos_[3] = pos_m_b; if (has_colon_pos_) pos_[4] = pos_colon;
            ESP_LOGI(TAG, "Loaded face.json positions: HA(%d,%d) HB(%d,%d) MA(%d,%d) MB(%d,%d)",
                     pos_[0][0], pos_[0][1], pos_[1][0], pos_[1][1], pos_[2][0], pos_[2][1], pos_[3][0], pos_[3][1]);
        } else {
            use_absolute_positions_ = false;
            ESP_LOGI(TAG, "face.json positions missing or invalid, using auto layout");
        }
        cleanup();
        return true;
    }
    // 本地主题枚举：优先 SD（兼容短名/缺 face.json），否则回退内置 assets 键名
    std::vector<std::string> ListClockFacesLocal() const {
        std::vector<std::string> faces;
        // 1) SD 根目录解析真实 clock_faces 路径
        if (IsSdMounted()) {
            std::string base_dir = ResolveSdClockFacesDir();
            DIR* dir = opendir(base_dir.c_str());
            if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != nullptr) {
                const char* name = ent->d_name; if (!name || name[0]=='.') continue;
                std::string theme_dir = base_dir + "/" + name;
                DIR* sub = opendir(theme_dir.c_str());
                if (!sub) continue;
                closedir(sub);
                // 归一化成小写，后续路径按小写构造，避免大小写差异
                std::string lower = name; for (auto &c : lower) c = (char)tolower((unsigned char)c);
                faces.emplace_back(lower);
            }
                closedir(dir);
                if (!faces.empty()) return faces;
            }
        }
        // 2) 回退到内置：直接使用内置清单文件（若存在）
        std::vector<std::string> faces_builtin;
        void* ptr = nullptr; size_t size = 0;
        auto& assets = Assets::GetInstance();
        if (assets.GetAssetData("assets/clock/clock.json", ptr, size) ||
            assets.GetAssetData("clock/clock.json", ptr, size) ||
            assets.GetAssetData("@clock/clock.json", ptr, size)) {
            cJSON* root = cJSON_ParseWithLength((const char*)ptr, size);
            if (root) {
                if (cJSON_IsArray(root)) {
                    int n = cJSON_GetArraySize(root);
                    for (int i = 0; i < n; ++i) {
                        cJSON* it = cJSON_GetArrayItem(root, i);
                        if (cJSON_IsString(it)) {
                            std::string v = it->valuestring; for (auto &c : v) c = (char)tolower((unsigned char)c);
                            faces_builtin.emplace_back(v);
                        }
                    }
                }
                cJSON_Delete(root);
            }
        }
        return faces_builtin;
    }
    // 加载配置（仅解析JSON，不加载资源）
    void LoadFaceConfig() {
        // 允许多次调用，以便在 assets 就绪后能生效
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
        const std::string face_path = std::string("clock/") + active_face_name_ + "/face.json";
        // 1) SD 正常名
        if (IsSdMounted()) {
            std::string base_dir = ResolveSdClockFacesDir();
            std::string theme_dir = ResolveSdThemeDir(base_dir, active_face_name_);
            std::string sd_path = theme_dir + "/face.json";
            std::string data;
            if (Assets::ReadFileFromSd(sd_path.c_str(), data)) {
                ESP_LOGI(TAG, "Try SD face.json: %s", sd_path.c_str());
                if (ParseFaceJsonBuffer(data.data(), data.size())) return;
            }
            // 2) SD 短名 FACE.JSO
            std::string sd_short = theme_dir + "/FACE.JSO";
            if (Assets::ReadFileFromSd(sd_short.c_str(), data)) {
                ESP_LOGI(TAG, "Try SD FACE.JSO: %s", sd_short.c_str());
                if (ParseFaceJsonBuffer(data.data(), data.size())) return;
            }
            // 3) SD 大写 JSON: FACE.JSON（部分卡会保留大写扩展名）
            std::string sd_upper = theme_dir + "/FACE.JSON";
            if (Assets::ReadFileFromSd(sd_upper.c_str(), data)) {
                ESP_LOGI(TAG, "Try SD FACE.JSON: %s", sd_upper.c_str());
                if (ParseFaceJsonBuffer(data.data(), data.size())) return;
            }
            // 4) 目录内兜底：枚举 face*.json（大小写不敏感，兼容 8.3）
            DIR* td = opendir(theme_dir.c_str());
            if (td) {
                struct dirent* e;
                while ((e = readdir(td)) != nullptr) {
                    const char* n = e->d_name; if (!n || n[0]=='.') continue;
                    std::string lower = n; for (auto &c : lower) c = (char)tolower((unsigned char)c);
                    bool looks = (lower == "face.json") || (lower == "face.jso") || (lower.find("face") != std::string::npos && (lower.rfind(".json") == lower.size()-5 || lower.rfind(".jso") == lower.size()-4));
                    if (!looks) continue;
                    std::string p = theme_dir + "/" + n;
                    if (Assets::ReadFileFromSd(p.c_str(), data)) {
                        ESP_LOGI(TAG, "Try SD face(any): %s", p.c_str());
                        if (ParseFaceJsonBuffer(data.data(), data.size())) { closedir(td); return; }
                    }
                }
                closedir(td);
            }
        }
        // 3) 内置
        if (try_get(face_path)) {
            (void)ParseFaceJsonBuffer(static_cast<const char*>(ptr), size);
        }
    }

    // 不再支持自适应布局

    // 加载背景资源
    void LoadBackgroundIfNeeded() {
        if (background_loaded_) return;
        background_loaded_ = true;
        
        // 先尝试从 SD 背景目录构建壁纸列表，并按 JSON 动静态选择默认 1.gif 或 1.png
        if (BuildBackgroundList()) {
            int def = -1;
            // 读取上次选中的壁纸（按主题名区分）
            {
                Settings s("display", false);
                std::string key = MakeBgKeyForTheme(active_face_name_);
                std::string saved = s.GetString(key.c_str(), "");
                if (!saved.empty()) {
                    std::string saved_lower = saved; for (auto &c : saved_lower) c = (char)tolower((unsigned char)c);
                    for (int i = 0; i < (int)bg_files_.size(); ++i) {
                        std::string lower = bg_files_[i]; for (auto &c : lower) c = (char)tolower((unsigned char)c);
                        if (lower == saved_lower) { def = i; break; }
                    }
                }
            }
            // 若无记录或记录缺失，优先 1.gif 或 1.png
            if (def < 0) {
                for (int i = 0; i < (int)bg_files_.size(); ++i) {
                    std::string lower = bg_files_[i]; for (auto &c : lower) c = (char)tolower((unsigned char)c);
                    if (use_gif_background_) { if (lower == "1.gif") { def = i; break; } }
                    else { if (lower == "1.png") { def = i; break; } }
                }
            }
            if (def < 0) def = 0;
            current_bg_index_ = def;
            LoadBackgroundByIndex(current_bg_index_);
            SetLoopMode(loop_mode_);
            return;
        }

        // 回退：直接尝试 1.gif/1.png；若缺省，再尝试主题根的 bg.*
        {
            std::string base = std::string("clock/") + active_face_name_ + "/bg/";
            // 增加对扁平化打包名的兜底（clock_face_core会递归LoadBackground，这里只尝试规范路径）
            if (!(use_gif_background_ ? LoadBackground(base + "1.gif") : LoadBackground(base + "1.png"))) {
                // 再兜底一个候选
                if (!LoadBackground(base + (use_gif_background_ ? "1.png" : "1.gif"))) {
                    // 最后尝试主题根目录常见命名
            std::string theme_base = std::string("clock/") + active_face_name_ + "/";
            static const char* kRootBg[] = {"bg.gif","bg.png"};
                    for (const char* fn : kRootBg) {
                        if (LoadBackground(theme_base + fn)) break;
                    }
                }
            }
            SetLoopMode(loop_mode_);
        }
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
            auto self = static_cast<ClockFaceCore*>(lv_event_get_user_data(e));
            if (!self) return;
            lv_event_code_t code = lv_event_get_code(e);
            // 全局触摸唤醒：在时钟界面内也直接唤醒
            if (code == LV_EVENT_PRESSED) {
                Board::GetInstance().SetPowerSaveMode(false);
            }
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
                // 记录按下起点与时间，并把长按阈值设为 1000ms
                lv_indev_t* indev = lv_indev_get_act();
                if (indev) {
                    lv_indev_set_long_press_time(indev, 1000);
                }
                lv_point_t p{}; if (indev) lv_indev_get_point(indev, &p);
                self->press_x_ = p.x; self->press_y_ = p.y; self->press_tick_ = lv_tick_get();
                self->moved_significantly_ = false;
                // 移除 gesture 锁，依靠 LVGL 手势事件
            } else if (code == LV_EVENT_PRESSING) {
                // 在时钟界面内拦截触摸事件，避免外部处理（例如触摸音量）
                // 同时检测位移，超过阈值则标记为手势优先
                lv_indev_t* indev = lv_indev_get_act();
                if (indev) {
                    lv_point_t p{}; lv_indev_get_point(indev, &p);
                    int dx = p.x - self->press_x_;
                    int dy = p.y - self->press_y_;
                    if ((dx*dx + dy*dy) > (self->move_threshold_px_ * self->move_threshold_px_)) {
                        self->moved_significantly_ = true;
                    }
                }
                lv_event_stop_bubbling(e);
                lv_event_stop_processing(e);
            } else if (code == LV_EVENT_SHORT_CLICKED) {
                // 短按：重播 GIF（不进入切换）
                if (self->gif_ && self->gif_->IsLoaded()) {
                    self->gif_->Stop();
                    self->SetLoopMode(self->loop_mode_);
                    self->gif_->Start();
                }
                lv_event_stop_bubbling(e);
                lv_event_stop_processing(e);
            } else if (code == LV_EVENT_LONG_PRESSED) {
                // 长按：仅当累计位移未超过阈值时才成立；否则由手势处理
                if (!self->moved_significantly_) {
                    self->EnterSwitchMode();
                }
                lv_event_stop_bubbling(e);
                lv_event_stop_processing(e);
            } else if (code == LV_EVENT_GESTURE) {
                // 左右滑动：切换当前主题下的壁纸
                // 取消额外抖动限制，依靠 LVGL 自身手势判定
                lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
                if (dir == LV_DIR_LEFT) {
                    self->NextBackground();
                } else if (dir == LV_DIR_RIGHT) {
                    self->PrevBackground();
                }
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
                auto self = static_cast<ClockFaceCore*>(lv_timer_get_user_data(timer));
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
        if (use_text_time_) { // 文本时间模式：不加载数字图片
            digits_loaded_ = true;
            return;
        }
        // 懒加载：尝试加载冒号（仅当前主题提供则显示；不回退到其它主题）
        if (!colon_image_ || !colon_image_->image_dsc()) {
            (void)LoadImageTo(colon_image_, MakeColonPath());
        }
        digits_loaded_ = true;
    }

    std::string MakeNumberPath(int d) const {
        char buf[128];
        snprintf(buf, sizeof(buf), "clock/%s/number/%d.png", active_face_name_.c_str(), d);
        return std::string(buf);
    }
    std::string MakeColonPath() const {
        return std::string("clock/") + active_face_name_ + "/number/colon.png";
    }

    static bool LoadImageTo(std::unique_ptr<LvglImage>& out_img, const std::string& path_png) {
        void* ptr = nullptr; size_t size = 0; auto& assets = Assets::GetInstance();
        // 尝试完整路径 -> @前缀 -> 斜杠转下划线 -> basename
        std::string n1 = path_png; for (auto &ch : n1) if (ch == '\\') ch = '/';
        std::string matched_key;
        // 先尝试 SD 文件
        {
            // 从 path_png 截出相对路径 clock/<face>/...
            const std::string prefix = "clock/";
            if (n1.rfind(prefix, 0) == 0) {
                std::string base_dir = ResolveSdClockFacesDir();
                std::string rel = n1.substr(prefix.size());
                std::string sd_path = base_dir + "/" + rel;
                std::string data;
                if (Assets::ReadFileFromSd(sd_path.c_str(), data)) {
                    void* copy_buf = heap_caps_malloc(data.size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (copy_buf) memcpy(copy_buf, data.data(), data.size());
                    out_img = std::make_unique<LvglAllocatedImage>(copy_buf, data.size());
                    if (out_img->image_dsc()) {
                        ESP_LOGI(TAG, "Digit image loaded from SD: %s", sd_path.c_str());
                        return true;
                    }
                    out_img.reset();
                }
            }
        }
        auto try_key = [&](const std::string& key)->bool{
            // 直接键
            if (assets.GetAssetData(key, ptr, size)) { matched_key = key; return true; }
            // @前缀
            if (assets.GetAssetData(std::string("@")+key, ptr, size)) { matched_key = std::string("@")+key; return true; }
            // assets/ 前缀
            if (assets.GetAssetData(std::string("assets/")+key, ptr, size)) { matched_key = std::string("assets/")+key; return true; }
            // 扁平化：clock/... -> clock_...
            std::string flat = key; for (auto &ch : flat) if (ch == '/') ch = '_';
            if (assets.GetAssetData(flat, ptr, size)) { matched_key = flat; return true; }
            if (assets.GetAssetData(std::string("assets/")+flat, ptr, size)) { matched_key = std::string("assets/")+flat; return true; }
            return false;
        };
        bool ok = try_key(n1);
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

        // 懒加载：先确保当前4个数字贴图可用
        {
            int need[4] = { d0, d1, d2, d3 };
            for (int i = 0; i < 4; ++i) {
                int d = need[i];
                if (!digit_images_[d]) {
                    (void)LoadImageTo(digit_images_[d], MakeNumberPath(d));
                }
            }
        }
        // 检查4位数字是否都可用（不需要冒号）
        bool digits_ok =
            digit_images_[d0] && digit_images_[d0]->image_dsc() &&
            digit_images_[d1] && digit_images_[d1]->image_dsc() &&
            digit_images_[d2] && digit_images_[d2]->image_dsc() &&
            digit_images_[d3] && digit_images_[d3]->image_dsc();
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
            // 如主题提供了冒号资源，则显示；否则隐藏
            if (colon_image_ && colon_image_->image_dsc()) {
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
            // flower 主题：显示日期（默认字体），位于时间数字下方（大小写不敏感）
            {
                std::string n = active_face_name_;
                for (auto &c : n) c = (char)tolower((unsigned char)c);
                if (n == "flower") UpdateAndShowDate(tm_info, /*is_digits_mode=*/true);
                else { if (date_text_) lv_obj_add_flag(date_text_, LV_OBJ_FLAG_HIDDEN); if (date_line_) lv_obj_add_flag(date_line_, LV_OBJ_FLAG_HIDDEN); }
            }
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
            // 日期标签：仅 flower 主题显示；位于时间下方（大小写不敏感）
            {
                std::string n = active_face_name_;
                for (auto &c : n) c = (char)tolower((unsigned char)c);
                if (n == "flower") UpdateAndShowDate(tm_info, /*is_digits_mode=*/false);
                else { if (date_text_) lv_obj_add_flag(date_text_, LV_OBJ_FLAG_HIDDEN); if (date_line_) lv_obj_add_flag(date_line_, LV_OBJ_FLAG_HIDDEN); }
            }
            // 隐藏图片层以避免覆盖
            for (int i = 0; i < 5; ++i) {
                if (digit_img_[i]) lv_obj_add_flag(digit_img_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // 统一更新/显示日期（仅 flower 调用）
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
                lv_obj_align(date_text_, LV_ALIGN_CENTER, 0, bottom_y + date_gap_ + 42);
            } else if (time_container_) {
                lv_obj_align_to(date_text_, time_container_, LV_ALIGN_OUT_BOTTOM_MID, 0, date_gap_ + 42);
            } else {
                lv_obj_align(date_text_, LV_ALIGN_CENTER, 0, date_gap_ + 42);
            }
        } else {
            // 文本时间：跟随 time_text_
            if (time_text_) lv_obj_align_to(date_text_, time_text_, LV_ALIGN_OUT_BOTTOM_MID, 0, date_gap_ + 42);
            else lv_obj_align(date_text_, LV_ALIGN_CENTER, 0, date_gap_ + 42);
        }
        // 在读取宽度之前强制布局，确保首次显示获得正确宽度
        lv_obj_update_layout(date_text_);
        // 设置细线长度略长于日期文本（考虑缩放）
        lv_coord_t wtxt = lv_obj_get_width(date_text_);
        lv_coord_t wtxt_scaled = (wtxt * date_zoom_) / 256; // date_zoom_: 256=1.0x
        lv_coord_t line_w = wtxt_scaled + 30; // 日期文本宽度 + 30 像素
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
        // 懒加载：需要时加载对应数字
        if (!digit_images_[d]) {
            (void)LoadImageTo(digit_images_[d], MakeNumberPath(d));
        }
        if (!digit_images_[d] || !digit_images_[d]->image_dsc()) {
            // 不隐藏，保留上一次的贴图，避免出现缺位
            ESP_LOGW(TAG, "Digit %d image not ready; keep previous at pos %d", d, pos);
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
    static constexpr const char* TAG = "ClockFace";
    // 生成短键用于 NVS（避免超过 15 字节限制）：cb_<8hex>
    static std::string MakeBgKeyForTheme(const std::string& theme) {
        uint32_t h = 2166136261u; // FNV-1a 32-bit
        for (char ch : theme) {
            char c = (char)tolower((unsigned char)ch);
            h ^= (uint8_t)c;
            h *= 16777619u;
        }
        char buf[12]; // "cb_" + 8hex + null
        snprintf(buf, sizeof(buf), "cb_%08x", (unsigned)h);
        return std::string(buf);
    }
    static int ExtractLeadingNumber(const std::string& s) {
        int i = 0;
        while (i < (int)s.size() && isdigit((unsigned char)s[i])) ++i;
        if (i == 0) return -1;
        return atoi(s.substr(0, i).c_str());
    }
    // 切换模式状态
    bool switch_mode_ = false;
    std::vector<std::string> faces_;
    int current_face_index_ = -1;     // 正在使用
    int preview_face_index_ = -1;     // 预览中的
    bool touch_active_ = false;       // 切换界面内是否有手指按住/滑动
    uint32_t last_interaction_tick_ = 0; // 最近一次交互的 tick
    // 长按与滑动判定辅助
    int press_x_ = 0;
    int press_y_ = 0;
    uint32_t press_tick_ = 0;
    bool moved_significantly_ = false;
    const int move_threshold_px_ = 14; // 超过该位移则优先判定为滑动
    // 移除 gesture 锁，依靠 LVGL 手势事件
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
        // 枚举主题（尽量不改动 assets：在此本地完成 SD 与内置枚举）
        faces_ = ListClockFacesLocal();
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
            auto self = static_cast<ClockFaceCore*>(lv_event_get_user_data(e));
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
            auto self = static_cast<ClockFaceCore*>(lv_timer_get_user_data(t));
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
            // 立即重新加载并显示（先清理跨主题状态）
            // 彻底释放旧的 LVGL 对象源，避免缓存持有旧指针
            if (gif_) gif_->Stop();
            // 先缓存当前 src 指针再 drop，避免传 NULL
            const void* bg_src = background_img_ ? lv_image_get_src(background_img_) : nullptr;
            if (bg_src) lv_image_cache_drop(bg_src);
            if (background_img_) { lv_image_set_src(background_img_, NULL); lv_obj_add_flag(background_img_, LV_OBJ_FLAG_HIDDEN); }
            for (int i = 0; i < 5; ++i) {
                if (digit_img_[i]) {
                    const void* s = lv_image_get_src(digit_img_[i]);
                    if (s) lv_image_cache_drop(s);
                    lv_image_set_src(digit_img_[i], NULL);
                    lv_obj_add_flag(digit_img_[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
            face_loaded_ = false; background_loaded_ = false; gif_.reset(); background_image_.reset();
            digits_loaded_ = false;
            for (auto &img : digit_images_) img.reset();
            // 重置冒号资源，避免跨主题复用
            colon_image_.reset();
            // 清理背景列表与目录缓存，避免串台
            bg_files_.clear();
            current_bg_index_ = -1;
            theme_dir_sd_.clear();
            bg_dir_sd_.clear();
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
            // 强制预热当前时间需要的数字，避免首次不完整
            {
                time_t now = time(NULL);
                struct tm* tm_info = localtime(&now);
                if (tm_info) {
                    int h = tm_info->tm_hour, m = tm_info->tm_min;
                    int need[4] = { h/10, h%10, m/10, m%10 };
                    for (int i = 0; i < 4; ++i) {
                        int d = need[i];
                        if (!digit_images_[d] || !digit_images_[d]->image_dsc()) {
                            (void)LoadImageTo(digit_images_[d], MakeNumberPath(d));
                        }
                    }
                    if (!colon_image_ || !colon_image_->image_dsc()) {
                        (void)LoadImageTo(colon_image_, MakeColonPath());
                    }
                }
            }
            // 首帧强制渲染完整时间
            UpdateTime();
            // 统一再应用一次坐标，确保冒号与数字同步
            if (use_absolute_positions_) ApplyPositions();
            // 确认后强制更新布局并再应用一次绝对定位，减少偶发错位
            lv_obj_update_layout(container_);
            if (use_absolute_positions_) ApplyPositions();
            // 立即刷新一帧，保证首帧位置与贴图完整
            lv_obj_invalidate(container_);
            lv_refr_now(nullptr);
        }
        ExitSwitchMode();
    }

    std::string active_face_name_ = "flower";
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
    void* gif_sd_buf_ = nullptr;
    size_t gif_sd_size_ = 0;
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
    int time_zoom_ = 256;      // 固定 1.0x（缩放取消，保留字段避免侵入式改动）
    int date_zoom_ = 256;      // 固定 1.0x
    int time_y_offset_ = 0;    // 垂直偏移
    int date_gap_ = 6;         // 时间与日期的间距

    // 背景多图轮换（SD）
    std::vector<std::string> bg_files_;
    int current_bg_index_ = -1;
    std::string theme_dir_sd_;
    std::string bg_dir_sd_;

    bool BuildBackgroundList() {
        bg_files_.clear(); current_bg_index_ = -1; theme_dir_sd_.clear();
        std::string base_dir = ResolveSdClockFacesDir();
        // 解析实际主题目录，避免大小写差异
        std::string theme_dir = ResolveSdThemeDir(base_dir, active_face_name_);
        std::string dir = theme_dir + "/bg";
        DIR* d = opendir(dir.c_str());
        if (!d) {
            // 兼容大小写与 8.3：在主题目录下查找以 background/backgr 开头的目录
            DIR* td = opendir(theme_dir.c_str());
            if (!td) return false;
            struct dirent* e;
            while ((e = readdir(td)) != nullptr) {
                const char* n = e->d_name; if (!n || n[0]=='.') continue;
                std::string sub = theme_dir + "/" + n;
                DIR* test = opendir(sub.c_str());
                if (!test) continue; // 不是目录
                closedir(test);
                std::string lower = n; for (auto &c : lower) c = (char)tolower((unsigned char)c);
                if (lower == "bg") { dir = sub; break; }
            }
            closedir(td);
            d = opendir(dir.c_str());
            if (!d) return false;
        }
        theme_dir_sd_ = theme_dir;
        bg_dir_sd_ = dir;
        auto is_img = [](const char* fn) {
            if (!fn) {
                return false;
            }
            std::string s = fn;
            for (auto &c : s) c = (char)tolower((unsigned char)c);
            bool is_png = s.size() > 4 && s.rfind(".png") == s.size() - 4;
            bool is_gif = s.size() > 4 && s.rfind(".gif") == s.size() - 4;
            return is_png || is_gif;
        };
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            if (ent->d_name[0]=='.') continue;
            if (!is_img(ent->d_name)) continue;
            bg_files_.emplace_back(ent->d_name);
        }
        closedir(d);
        if (bg_files_.empty()) return false;
        // 数字优先排序：提取前导数字比较，避免 10.* 排在 2.* 前面
        std::sort(bg_files_.begin(), bg_files_.end(), [&](const std::string& a, const std::string& b){
            int na = ExtractLeadingNumber(a), nb = ExtractLeadingNumber(b);
            if (na != -1 || nb != -1) {
                if (na == -1) return false; // 非数字排后
                if (nb == -1) return true;
                if (na != nb) return na < nb;
            }
            return a < b; // 同号或无号按字典序
        });
        ESP_LOGI(TAG, "Background list from %s: %d files", dir.c_str(), (int)bg_files_.size());
        return true;
    }

    bool LoadBackgroundByIndex(int idx) {
        if (idx < 0 || idx >= (int)bg_files_.size()) return false;
        ESP_LOGI(TAG, "Switch background: idx=%d name=%s", idx, bg_files_[idx].c_str());
        // 若已解析到实际 SD 背景目录，优先直接按绝对路径加载，避免 8.3 目录名映射失败
        if (!bg_dir_sd_.empty()) {
            std::string full = bg_dir_sd_ + "/" + bg_files_[idx];
            bool ok = LoadBackground(full);
            if (ok) ESP_LOGI(TAG, "Background switched (SD abs): %s", full.c_str());
            else ESP_LOGI(TAG, "Background failed (SD abs): %s", full.c_str());
            if (ok) {
                // 持久化当前主题的壁纸选择（使用短键）
                Settings s("display", true);
                std::string key = MakeBgKeyForTheme(active_face_name_);
                s.SetString(key.c_str(), bg_files_[idx]);
                return true;
            }
        }
        std::string path = std::string("clock/") + active_face_name_ + "/bg/" + bg_files_[idx];
        bool ok2 = LoadBackground(path);
        if (ok2) ESP_LOGI(TAG, "Background switched: %s", path.c_str());
        else ESP_LOGI(TAG, "Background failed: %s", path.c_str());
        if (ok2) {
            Settings s("display", true);
            std::string key = MakeBgKeyForTheme(active_face_name_);
            s.SetString(key.c_str(), bg_files_[idx]);
        }
        return ok2;
    }

    void NextBackground() {
        if (bg_files_.empty()) return;
        // 基于编号推进：若当前名是 N.xxx，则找到编号为 N+1 的项；否则退化为顺序
        int n = (int)bg_files_.size();
        int cur_num = current_bg_index_ >= 0 ? ExtractLeadingNumber(bg_files_[current_bg_index_]) : -1;
        int target_idx = -1;
        if (cur_num != -1) {
            int want = cur_num + 1;
            for (int i = 0; i < n; ++i) {
                if (ExtractLeadingNumber(bg_files_[i]) == want) { target_idx = i; break; }
            }
        }
        if (target_idx == -1) target_idx = (current_bg_index_ + 1) % n;
        ESP_LOGI(TAG, "Gesture NEXT: %d(%d) -> %d(%d) (%s)", current_bg_index_, cur_num, target_idx, ExtractLeadingNumber(bg_files_[target_idx]), bg_files_[target_idx].c_str());
        current_bg_index_ = target_idx;
        (void)LoadBackgroundByIndex(current_bg_index_);
        SetLoopMode(loop_mode_);
        // 背景切换后立即刷新时间，确保数字同步
        UpdateTime();
    }
    void PrevBackground() {
        if (bg_files_.empty()) return;
        int n = (int)bg_files_.size();
        int cur_num = current_bg_index_ >= 0 ? ExtractLeadingNumber(bg_files_[current_bg_index_]) : -1;
        int target_idx = -1;
        if (cur_num != -1) {
            int want = cur_num - 1;
            for (int i = 0; i < n; ++i) {
                if (ExtractLeadingNumber(bg_files_[i]) == want) { target_idx = i; break; }
            }
        }
        if (target_idx == -1) target_idx = (current_bg_index_ - 1 + n) % n;
        ESP_LOGI(TAG, "Gesture PREV: %d(%d) -> %d(%d) (%s)", current_bg_index_, cur_num, target_idx, ExtractLeadingNumber(bg_files_[target_idx]), bg_files_[target_idx].c_str());
        current_bg_index_ = target_idx;
        (void)LoadBackgroundByIndex(current_bg_index_);
        SetLoopMode(loop_mode_);
        UpdateTime();
    }
};

extern "C" {
    void* ClockFace_Create(void* parent, int width, int height) {
        auto* obj = new ClockFaceCore((lv_obj_t*)parent, width, height);
        return static_cast<void*>(obj);
    }

    void ClockFace_Show(void* ctx) {
        if (!ctx) return;
        static_cast<ClockFaceCore*>(ctx)->Show();
    }

    void ClockFace_Hide(void* ctx) {
        if (!ctx) return;
        static_cast<ClockFaceCore*>(ctx)->Hide();
    }

    bool ClockFace_LoadBackground(void* ctx, const char* asset_name) {
        if (!ctx) return false;
        return static_cast<ClockFaceCore*>(ctx)->LoadBackground(asset_name ? asset_name : std::string());
    }

    void ClockFace_SetLoop(void* ctx, int loop_mode) {
        if (!ctx) return;
        auto* obj = static_cast<ClockFaceCore*>(ctx);
        if (loop_mode > 1) loop_mode = -1; // normalize
        obj->SetLoopMode(loop_mode == 0 ? ClockFaceCore::LOOP_NONE : (loop_mode == 1 ? ClockFaceCore::LOOP_ONCE : ClockFaceCore::LOOP_INFINITE));
    }
}


