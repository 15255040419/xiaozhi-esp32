#pragma once

#include "lvgl.h"
#include <string>

// 前向声明
struct _lv_obj_t;
struct _lv_event_t;
struct _lv_timer_t;
class LvglTheme;

class MusicPlayerUI {
public:
    // 音乐播放器状态
    enum PlayState {
        STOPPED = 0,
        PLAYING = 1,
        PAUSED = 2
    };

    MusicPlayerUI(lv_obj_t* parent, int width, int height, LvglTheme* theme, int status_bar_height = 0);
    ~MusicPlayerUI();

    // 显示/隐藏音乐播放器
    void Show();
    void Hide();
    bool IsVisible() const { return visible_; }

    // 更新音乐信息
    void SetSongTitle(const char* title);
    void SetArtist(const char* artist);  // 设置歌手信息
    void SetLyrics(const char* lyrics);
    void SetProgress(float progress);  // 0.0 - 1.0
    void SetPlayState(PlayState state);
    void SetDuration(const char* duration);  // 如 "03:45"
    void SetCurrentTime(const char* current_time);  // 如 "01:23"

    // 设置回调函数
    void SetPlayPauseCallback(void (*callback)(void* user_data), void* user_data);
    void SetPreviousCallback(void (*callback)(void* user_data), void* user_data);
    void SetNextCallback(void (*callback)(void* user_data), void* user_data);
    void SetProgressCallback(void (*callback)(float progress, void* user_data), void* user_data);
    void SetVolumeCallback(void (*callback)(int volume, void* user_data), void* user_data);
    
    // 音量控制
    void SetVolume(int volume);  // 0-100
    int GetVolume() const { return current_volume_; }
    
    // 主题更新
    void UpdateTheme(LvglTheme* theme);

private:
    // UI组件
    lv_obj_t* parent_;
    lv_obj_t* container_;
    lv_obj_t* song_title_label_;  // 歌名标签（固定不动）
    lv_obj_t* lyrics_label_;      // 歌词标签（可更新）
    lv_obj_t* control_container_;
    lv_obj_t* prev_btn_;
    lv_obj_t* play_pause_btn_;
    lv_obj_t* next_btn_;
    lv_obj_t* progress_container_;
    lv_obj_t* current_time_label_;
    lv_obj_t* progress_bar_;
    lv_obj_t* duration_label_;
    lv_obj_t* volume_container_;
    lv_obj_t* volume_slider_;
    lv_obj_t* volume_label_;      // 音量数值标签
    lv_obj_t* music_icon_label_;  // 音乐图标

    // 状态
    bool visible_;
    PlayState play_state_;
    int width_;
    int height_;
    int status_bar_height_;
    int current_volume_;
    
    // 主题
    LvglTheme* theme_;
    
    // 当前歌名、歌手和歌词
    std::string current_song_title_;
    std::string current_artist_;
    std::string current_lyrics_;

    // 回调函数
    void (*play_pause_callback_)(void* user_data);
    void* play_pause_user_data_;
    void (*previous_callback_)(void* user_data);
    void* previous_user_data_;
    void (*next_callback_)(void* user_data);
    void* next_user_data_;
    void (*progress_callback_)(float progress, void* user_data);
    void* progress_user_data_;
    void (*volume_callback_)(int volume, void* user_data);
    void* volume_user_data_;

    // 内部方法
    void CreateUI();
    void DestroyUI();
    void CreateVolumeControl(int height, int padding);
    void CreateSongInfoArea(int height, int padding);
    void CreateControlButtons(int height, int padding);
    void CreateProgressBar(int height, int padding);
    void UpdatePlayPauseButton();
    void UpdateSongInfoDisplay();  // 更新歌名和歌词显示

    // 静态事件回调
    static void PlayPauseEventCb(lv_event_t* e);
    static void PreviousEventCb(lv_event_t* e);
    static void NextEventCb(lv_event_t* e);
    static void ProgressEventCb(lv_event_t* e);
    static void VolumeEventCb(lv_event_t* e);
};
