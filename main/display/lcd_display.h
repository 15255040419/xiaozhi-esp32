#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "lvgl_display.h"
#include "gif/lvgl_gif.h"
#include "music_player_ui.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <font_emoji.h>

#include <atomic>
#include <memory>
#include <stdint.h>

#define PREVIEW_IMAGE_DURATION_MS 5000


#ifdef __cplusplus
// C++: 使用强类型枚举
enum class UIMode : uint8_t {
    Clock,
    Music,
    Chat
};
#else
// C: 提供等价占位，避免被 C 文件包含时报错
typedef uint8_t UIMode;
#define UIMode_Clock 0
#define UIMode_Music 1
#define UIMode_Chat  2
#endif

class LcdDisplay : public LvglDisplay {
public:
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    
    lv_draw_buf_t draw_buf_;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* emoji_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    lv_obj_t* emoji_box_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    esp_timer_handle_t preview_timer_ = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    
    // 音乐播放器UI
    std::unique_ptr<MusicPlayerUI> music_player_ui_ = nullptr;
    
    // 音乐进度更新定时器
    esp_timer_handle_t music_progress_timer_ = nullptr;
    
    // 时钟界面（PixelThinking）
    void* pixel_thinking_clock_ = nullptr;
    bool clock_visible_ = false;
    void EnsureClockFaceInitialized();
    void ShowClockFace();
    void HideClockFace();


    void InitializeLcdThemes();
    void SetupUI();
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;
    
    // 检查是否选择了音乐播放器界面风格
    bool IsMusicPlayerStyleEnabled() const;
    
    // 传统方式显示音乐信息的辅助方法
    void SetMusicInfoTraditional(const char* text, const char* mode_name);
    void SetMusicDetailsTraditional(const char* title, const char* artist, bool is_playing, const char* mode_name);

protected:
    // 添加protected构造函数
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height);
    
public:

    ~LcdDisplay();
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override; 
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void UpdateStatusBar(bool update_all = false) override;

    // 公开一个状态协调点
    void OnStateMaybeChanged();
    void ApplyUIMode(UIMode mode);

    // 统一管理表情显隐
    void ApplyEmojiVisibility();
    bool ShouldShowEmojis() const;
    bool IsPreviewVisible() const;

    // Add theme switching function
    virtual void SetTheme(Theme* theme) override;

    // Add set music info function
    virtual void SetMusicInfo(const char* song_name) override;
    virtual void SetMusicDetails(const char* song_title, const char* artist, bool is_playing) override;
    
    // 音乐播放器功能
    void ShowMusicPlayer();
    void HideMusicPlayer();
    void UpdateMusicProgress(float progress);
    void UpdateMusicLyrics(const char* lyrics);
    void UpdateMusicTime(const char* current_time, const char* duration);
    void SetMusicPlayState(bool is_playing);
    
    // 音乐控制回调设置
    void SetMusicControlCallbacks(
        void (*play_pause_cb)(void*),
        void (*previous_cb)(void*),
        void (*next_cb)(void*),
        void (*progress_cb)(float, void*),
        void (*volume_cb)(int, void*),
        void* user_data
    );
    
    // 音量控制
    void SetVolume(int volume);  // 0-100
    int GetVolume() const;
    
    // 触摸音量控制
    void EnableTouchVolumeControl(bool enable);
    bool IsTouchVolumeControlEnabled() const;
    
    // 音乐进度更新
    void StartMusicProgressUpdate();
    void StopMusicProgressUpdate();
    
    // 音乐播放器状态检查
    bool IsMusicPlayerVisible() const;
    void SetMusicPlayerPlayState(MusicPlayerUI::PlayState state);
};

// SPI LCD显示器
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// RGB LCD显示器
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// MIPI LCD显示器
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy);
};

#endif // LCD_DISPLAY_H
