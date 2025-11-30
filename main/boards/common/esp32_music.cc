#include "esp32_music.h"
#include "board.h"
#include "system_info.h"
#include "audio/audio_codec.h"
#include "application.h"
#include "protocols/protocol.h"
#include "display/display.h"
#include "display/lcd_display.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>  // 为isdigit函数
#include <thread>   // 为线程ID比较
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Esp32Music"

// ========== 简单的ESP32认证函数 ==========

/**
 * @brief 获取设备MAC地址
 * @return MAC地址字符串
 */
static std::string get_device_mac() {
    return SystemInfo::GetMacAddress();
}

/**
 * @brief 获取设备芯片ID
 * @return 芯片ID字符串
 */
static std::string get_device_chip_id() {
    // 使用MAC地址作为芯片ID，去除冒号分隔符
    std::string mac = SystemInfo::GetMacAddress();
    // 去除所有冒号
    mac.erase(std::remove(mac.begin(), mac.end(), ':'), mac.end());
    return mac;
}

/**
 * @brief 生成动态密钥
 * @param timestamp 时间戳
 * @return 动态密钥字符串
 */
static std::string generate_dynamic_key(int64_t timestamp) {
    // 密钥（请修改为与服务端一致）
    const std::string secret_key = "your-esp32-secret-key-2024";
    
    // 获取设备信息
    std::string mac = get_device_mac();
    std::string chip_id = get_device_chip_id();
    
    // 组合数据：MAC:芯片ID:时间戳:密钥
    std::string data = mac + ":" + chip_id + ":" + std::to_string(timestamp) + ":" + secret_key;
    
    // SHA256哈希
    unsigned char hash[32];
    mbedtls_sha256((unsigned char*)data.c_str(), data.length(), hash, 0);
    
    // 转换为十六进制字符串（前16字节）
    std::string key;
    for (int i = 0; i < 16; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", hash[i]);
        key += hex;
    }
    
    return key;
}

/**
 * @brief 为HTTP请求添加认证头
 * @param http HTTP客户端指针
 */
static void add_auth_headers(Http* http) {
    // 获取当前时间戳
    int64_t timestamp = esp_timer_get_time() / 1000000;  // 转换为秒
    
    // 生成动态密钥
    std::string dynamic_key = generate_dynamic_key(timestamp);
    
    // 获取设备信息
    std::string mac = get_device_mac();
    std::string chip_id = get_device_chip_id();
    
    // 添加认证头
    if (http) {
        http->SetHeader("X-MAC-Address", mac);
        http->SetHeader("X-Chip-ID", chip_id);
        http->SetHeader("X-Timestamp", std::to_string(timestamp));
        http->SetHeader("X-Dynamic-Key", dynamic_key);
        
        ESP_LOGI(TAG, "Added auth headers - MAC: %s, ChipID: %s, Timestamp: %lld", 
                 mac.c_str(), chip_id.c_str(), timestamp);
    }
}

// URL编码函数
static std::string url_encode(const std::string& str) {
    std::string encoded;
    char hex[4];
    
    for (size_t i = 0; i < str.length(); i++) {
        unsigned char c = str[i];
        
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';  // 空格编码为'+'或'%20'
        } else {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}


Esp32Music::Esp32Music() : last_downloaded_data_(), current_music_url_(), current_song_name_(), current_artist_(),
                         song_name_displayed_(false), current_lyric_url_(), lyrics_(), 
                         current_lyric_index_(-1), lyric_thread_(), is_lyric_running_(false),
                         is_playing_(false), is_paused_(false), is_downloading_(false), pause_start_time_(0),
                         play_thread_(), download_thread_(), current_play_time_ms_(0), last_frame_time_ms_(0), total_duration_ms_(0),
                         audio_buffer_(), buffer_mutex_(), buffer_cv_(), buffer_size_(0), mp3_decoder_(nullptr), mp3_frame_info_(), 
                         mp3_decoder_initialized_(false) {
    ESP_LOGI(TAG, "Music player initialized with lyrics display");
    InitializeMp3Decoder();
}

Esp32Music::~Esp32Music() {
    ESP_LOGI(TAG, "Destroying music player - stopping all operations");
    
    // 停止所有操作
    is_downloading_ = false;
    is_playing_ = false;
    is_lyric_running_ = false;
    
    // 🔧 修复: 释放 FFT 缓冲区内存
    if (final_pcm_data_fft) {
        heap_caps_free(final_pcm_data_fft);
        final_pcm_data_fft = nullptr;
        ESP_LOGI(TAG, "FFT buffer freed");
    }
    
    // 通知所有等待的线程
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    // 等待下载线程结束，设置5秒超时
    if (download_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for download thread to finish (timeout: 5s)");
        auto start_time = std::chrono::steady_clock::now();
        
        // 等待线程结束
        bool thread_finished = false;
        while (!thread_finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            if (elapsed >= 5) {
                ESP_LOGW(TAG, "Download thread join timeout after 5 seconds");
                break;
            }
            
            // 再次设置停止标志，确保线程能够检测到
            is_downloading_ = false;
            
            // 通知条件变量
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }
            
            // 检查线程是否已经结束
            if (!download_thread_.joinable()) {
                thread_finished = true;
            }
            
            // 定期打印等待信息
            if (elapsed > 0 && elapsed % 1 == 0) {
                ESP_LOGI(TAG, "Still waiting for download thread to finish... (%ds)", (int)elapsed);
            }
        }
        
        if (download_thread_.joinable()) {
            download_thread_.join();
        }
        ESP_LOGI(TAG, "Download thread finished");
    }
    
    // 等待播放线程结束，设置3秒超时
    if (play_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for playback thread to finish (timeout: 3s)");
        auto start_time = std::chrono::steady_clock::now();
        
        bool thread_finished = false;
        while (!thread_finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            if (elapsed >= 3) {
                ESP_LOGW(TAG, "Playback thread join timeout after 3 seconds");
                break;
            }
            
            // 再次设置停止标志
            is_playing_ = false;
            
            // 通知条件变量
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }
            
            // 检查线程是否已经结束
            if (!play_thread_.joinable()) {
                thread_finished = true;
            }
        }
        
        if (play_thread_.joinable()) {
            play_thread_.join();
        }
        ESP_LOGI(TAG, "Playback thread finished");
    }
    
    // 等待歌词线程结束
    if (lyric_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for lyric thread to finish");
        lyric_thread_.join();
        ESP_LOGI(TAG, "Lyric thread finished");
    }
    
    // 清理缓冲区和MP3解码器
    ClearAudioBuffer();
    CleanupMp3Decoder();
    
    ESP_LOGI(TAG, "Music player destroyed successfully");
}

bool Esp32Music::Download(const std::string& song_name, const std::string& artist_name) {
    ESP_LOGI(TAG, "Requesting music details for: %s", song_name.c_str());
    
    // 清空之前的下载数据
    last_downloaded_data_.clear();
    last_downloaded_data_.shrink_to_fit();
    
    // 保存歌名用于后续显示，重置之前的信息
    current_song_name_ = song_name;
    current_artist_ = "";  // 重置歌手信息
    total_duration_ms_ = 0;  // 重置总时长，等待API响应更新
    
    // ⚠️ 关键：尽早禁用语音处理，避免 AFE 环形缓冲区满警告刷屏
    auto& app = Application::GetInstance();
    auto& audio_service = app.GetAudioService();
    audio_service.EnableVoiceProcessing(false);
    ESP_LOGI(TAG, "Voice processing disabled before HTTP request to prevent AFE warnings");
    vTaskDelay(pdMS_TO_TICKS(50)); // 等待语音链路停止
    
    // 第一步：请求stream_pcm接口获取音频信息
    std::string base_url = "http://110.42.59.54:2233"; //http://http-embedded-music.miao-lab.top:2233
    std::string full_url = base_url + "/stream_pcm?song=" + url_encode(song_name) + "&artist=" + url_encode(artist_name);
    
    ESP_LOGI(TAG, "Request URL: %s", full_url.c_str());
    
    // 使用Board提供的HTTP客户端
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(10);  // 设置10秒超时（足够获取歌曲信息）
    
    // 设置基本请求头
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "application/json");
    
    // 添加ESP32认证头
    add_auth_headers(http.get());
    
    // 打开GET连接
    if (!http->Open("GET", full_url)) {
        ESP_LOGE(TAG, "Failed to connect to music API (timeout or network error)");
        Application::GetInstance().Alert("network", "抱歉，无法连接到音乐服务，请检查网络");
        audio_service.EnableVoiceProcessing(true); // 恢复语音处理
        return false;
    }
    
    // 检查响应状态码
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        const char* error_msg = "抱歉，音乐服务响应错误";
        if (status_code == 404) {
            error_msg = "抱歉，找不到这首歌";
        } else if (status_code >= 500) {
            error_msg = "抱歉，音乐服务器出错了";
        }
        Application::GetInstance().Alert("music", error_msg);
        http->Close();
        audio_service.EnableVoiceProcessing(true); // 恢复语音处理
        return false;
    }
    
    // 修复: 读取响应数据，限制最大读取大小防止内存溢出
    const size_t MAX_RESPONSE_SIZE = 10 * 1024;  // 最多读取10KB
    last_downloaded_data_.clear();
    last_downloaded_data_.reserve(2048);  // 预分配2KB
    
    char buffer[1024];
    size_t total_read = 0;
    while (total_read < MAX_RESPONSE_SIZE) {
        int bytes_read = http->Read(buffer, sizeof(buffer));
        if (bytes_read <= 0) break;
        
        // 检查是否会超过最大限制
        if (total_read + bytes_read > MAX_RESPONSE_SIZE) {
            ESP_LOGW(TAG, "Response size exceeds %d bytes, truncating", MAX_RESPONSE_SIZE);
            bytes_read = MAX_RESPONSE_SIZE - total_read;
        }
        
        last_downloaded_data_.append(buffer, bytes_read);
        total_read += bytes_read;
    }
    http->Close();
    
    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d", status_code, last_downloaded_data_.length());
    ESP_LOGD(TAG, "Complete music details response: %s", last_downloaded_data_.c_str());
    
    // 简单的认证响应检查（可选）
    if (last_downloaded_data_.find("ESP32动态密钥验证失败") != std::string::npos) {
        ESP_LOGE(TAG, "Authentication failed for song: %s", song_name.c_str());
        Application::GetInstance().Alert("warning", "抱歉，设备认证失败，请重试");
        audio_service.EnableVoiceProcessing(true); // 恢复语音处理
        return false;
    }
    
    if (!last_downloaded_data_.empty()) {
        // 解析响应JSON以提取音频URL
        cJSON* response_json = cJSON_Parse(last_downloaded_data_.c_str());
        if (response_json) {
            // 提取关键信息
            cJSON* artist = cJSON_GetObjectItem(response_json, "artist");
            cJSON* title = cJSON_GetObjectItem(response_json, "title");
            cJSON* audio_url = cJSON_GetObjectItem(response_json, "audio_url");
            cJSON* lyric_url = cJSON_GetObjectItem(response_json, "lyric_url");
            cJSON* duration = cJSON_GetObjectItem(response_json, "duration");
            
            if (cJSON_IsString(artist)) {
                current_artist_ = artist->valuestring;  // 存储歌手信息
                ESP_LOGI(TAG, "Artist: %s", artist->valuestring);
            }
            if (cJSON_IsString(title)) {
                current_song_name_ = title->valuestring;  // 更新歌名为服务器返回的准确歌名
                ESP_LOGI(TAG, "Title: %s", title->valuestring);
            }
            if (cJSON_IsNumber(duration)) {
                total_duration_ms_ = (int64_t)(duration->valuedouble * 1000);  // 转换秒为毫秒
                ESP_LOGI(TAG, "Duration: %.0f seconds (%lld ms)", duration->valuedouble, total_duration_ms_);
            }
            
            // 检查audio_url是否有效
            if (cJSON_IsString(audio_url) && audio_url->valuestring && strlen(audio_url->valuestring) > 0) {
                std::string audio_path = audio_url->valuestring;
                ESP_LOGI(TAG, "Audio URL path: %s", audio_path.c_str());
                
                // 验证URL完整性：必须包含具体路径，不能只是域名
                bool is_valid_url = false;
                if (audio_path.find("/stream") != std::string::npos ||
                    audio_path.find(".mp3") != std::string::npos ||
                    audio_path.find(".m4a") != std::string::npos ||
                    audio_path.find(".flac") != std::string::npos ||
                    audio_path.find(".wav") != std::string::npos) {
                    is_valid_url = true;
                } else {
                    // 检查URL是否只是域名（没有路径）
                    size_t last_slash = audio_path.find_last_of('/');
                    if (last_slash != std::string::npos && last_slash > 8) {  // "http://" 之后还有路径
                        std::string path_part = audio_path.substr(last_slash + 1);
                        if (!path_part.empty()) {
                            is_valid_url = true;  // 至少有路径部分
                        }
                    }
                }
                
                if (!is_valid_url) {
                    ESP_LOGE(TAG, "Invalid audio URL (no path or file): %s", audio_path.c_str());
                    std::string error_message = "抱歉，没有找到歌曲《" + song_name + "》";
                    Application::GetInstance().Alert("music", error_message.c_str());
                    cJSON_Delete(response_json);
                    audio_service.EnableVoiceProcessing(true); // 恢复语音处理
                    return false;
                }
                
                // 第二步：直接使用audio_url播放音乐
                current_music_url_ = audio_path;
                
                ESP_LOGI(TAG, "Starting streaming playback for: %s", song_name.c_str());
                song_name_displayed_ = false;  // 重置歌名显示标志
                
                // 关键：不要调用 AbortSpeaking，直接启动播放即可
                // AbortSpeaking 会导致状态切换到 listening，让 AI 误以为可以继续对话
                // 语音处理已经在函数开头禁用了，直接播放音乐
                
                StartStreaming(current_music_url_);
                
                // 处理歌词URL - 有歌词就直接显示
                if (cJSON_IsString(lyric_url) && lyric_url->valuestring && strlen(lyric_url->valuestring) > 0) {
                    // 直接使用歌词URL
                    std::string lyric_path = lyric_url->valuestring;
                    current_lyric_url_ = lyric_path;
                    
                    ESP_LOGI(TAG, "Loading lyrics for: %s", song_name.c_str());
                    
                    // 停止之前的歌词线程
                    if (is_lyric_running_) {
                        is_lyric_running_ = false;
                        if (lyric_thread_.joinable()) {
                            lyric_thread_.join();
                        }
                    }
                    
                    is_lyric_running_ = true;
                    current_lyric_index_ = -1;
                    lyrics_.clear();
                    lyrics_.shrink_to_fit();  // 释放内存
                    
                    lyric_thread_ = std::thread(&Esp32Music::LyricDisplayThread, this);
                } else {
                    ESP_LOGW(TAG, "No lyric URL found for this song");
                }
                
                cJSON_Delete(response_json);
                return true;
            } else {
                // audio_url为空或无效
                ESP_LOGE(TAG, "Audio URL not found or empty for song: %s", song_name.c_str());
                ESP_LOGE(TAG, "Failed to find music: 没有找到歌曲 '%s'", song_name.c_str());
                
                // 使用 Application::Alert 播报错误（语音+显示）
                std::string error_message = "抱歉，没有找到歌曲《" + std::string(song_name) + "》";
                Application::GetInstance().Alert("music", error_message.c_str());
                
                cJSON_Delete(response_json);
                audio_service.EnableVoiceProcessing(true); // 恢复语音处理
                return false;
            }
        } else {
            ESP_LOGE(TAG, "Failed to parse JSON response");
            Application::GetInstance().Alert("warning", "抱歉，音乐服务响应格式错误");
        }
    } else {
        ESP_LOGE(TAG, "Empty response from music API");
        Application::GetInstance().Alert("warning", "抱歉，音乐服务暂时不可用");
    }
    
    audio_service.EnableVoiceProcessing(true); // 恢复语音处理
    return false;
}



std::string Esp32Music::GetDownloadResult() {
    return last_downloaded_data_;
}

// 开始流式播放
bool Esp32Music::StartStreaming(const std::string& music_url) {
    if (music_url.empty()) {
        ESP_LOGE(TAG, "Music URL is empty");
        return false;
    }
    
    // 内存追踪（DEBUG级别）
    ESP_LOGD(TAG, "Memory before playback - SRAM: %d bytes, SPIRAM: %d bytes", 
            heap_caps_get_free_size(MALLOC_CAP_8BIT),
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    ESP_LOGD(TAG, "Starting streaming for URL: %s", music_url.c_str());
    // 在启动播放前，停用语音处理与唤醒词，避免 AFE 环路缓冲堆积
    {
        auto& app = Application::GetInstance();
        auto& audio_service = app.GetAudioService();
        // 仅停止语音处理链路（VAD/ASR），保留唤醒词运行以支持语音打断
        audio_service.EnableVoiceProcessing(false);
        // 等待一小段时间让相关任务稳定
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // 停止标志并通知可能等待的线程（下载/播放/歌词）
    is_downloading_ = false;
    is_playing_ = false;
    is_lyric_running_ = false;
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    // 🔧 修复：快速清理旧线程，避免长时间等待导致 AI 误判
    // 对于旧线程，如果 joinable 就直接 detach，不等待
    // 这样可以立即开始新的播放，避免工具调用超时
    if (lyric_thread_.joinable()) {
        ESP_LOGI(TAG, "Detaching old-lyric thread");
        lyric_thread_.detach();
    }
    if (download_thread_.joinable()) {
        ESP_LOGI(TAG, "Detaching old-download thread");
        download_thread_.detach();
    }
    if (play_thread_.joinable()) {
        ESP_LOGI(TAG, "Detaching old-play thread");
        play_thread_.detach();
    }
    
    // 清空缓冲区
    ClearAudioBuffer();
    
    // 配置线程栈大小以避免栈溢出
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = 8192;  // 8KB栈大小
    cfg.prio = 5;           // 中等优先级
    cfg.thread_name = "audio_stream";
    esp_pthread_set_cfg(&cfg);
    
    // 开始下载线程
    is_downloading_ = true;
    download_thread_ = std::thread(&Esp32Music::DownloadAudioStream, this, music_url);
    
    // 开始播放线程（会等待缓冲区有足够数据）
    is_playing_ = true;
    play_thread_ = std::thread(&Esp32Music::PlayAudioStream, this);
    
    ESP_LOGI(TAG, "Streaming threads started successfully");
    
    return true;
}

// 停止流式播放
bool Esp32Music::StopStreaming() {
    ESP_LOGI(TAG, "Stopping music streaming - current state: downloading=%d, playing=%d", 
            is_downloading_.load(), is_playing_.load());

    // 重置采样率到原始值
    ResetSampleRate();
    
    // 检查是否有流式播放正在进行
    if (!is_playing_ && !is_downloading_) {
        ESP_LOGW(TAG, "No streaming in progress");
        return true;
    }
    
    // 停止下载和播放标志
    is_downloading_ = false;
    is_playing_ = false;
    is_paused_ = false;  // 🔧 重置暂停状态，确保清理逻辑正常进行
    is_lyric_running_ = false;
    
    // 清空歌名显示
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display) {
        display->SetMusicInfo("");  // 清空歌名显示
        ESP_LOGI(TAG, "Cleared song name display");
    }
    
    // 通知所有等待的线程
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    // 等待歌词线程结束（使用超时机制避免死锁）
    if (lyric_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for lyric thread to finish...");
        
        // 等待线程自然结束，最多等待2秒
        int wait_count = 0;
        const int max_wait = 200; // 最多等待2秒
        
        while (is_lyric_running_ && wait_count < max_wait) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait_count++;
        }
        
        SafeJoinThread(lyric_thread_, "lyric");
    }
    
    // 等待下载、播放、歌词线程结束
    SafeJoinThread(download_thread_, "download");
    SafeJoinThread(play_thread_, "play", 1000); // 1秒超时
    SafeJoinThread(lyric_thread_, "lyric");
    
    // 统一在这里清理解码器（线程都结束后更安全）
    CleanupMp3Decoder();
    
    // 清理音频缓冲区
    ClearAudioBuffer();
    
    // 🔧 修复: 清空音频解码队列
    {
        auto& app = Application::GetInstance();
        app.ClearAudioQueue();
    }
    
    // 重置播放相关变量
    current_play_time_ms_ = 0;
    total_frames_decoded_ = 0;
    last_frame_time_ms_ = 0;
    
    ESP_LOGI(TAG, "Music streaming completely stopped and cleaned up");
    
    // 内存追踪（DEBUG级别）
    ESP_LOGD(TAG, "Memory after stop - SRAM: %d bytes, SPIRAM: %d bytes",
            heap_caps_get_free_size(MALLOC_CAP_8BIT),
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    // 若设备处于空闲态，恢复唤醒词检测
    {
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateIdle) {
            auto& audio_service = app.GetAudioService();
            audio_service.EnableWakeWordDetection(true);
        }
    }
    return true;
}

// 流式下载音频数据
void Esp32Music::DownloadAudioStream(const std::string& music_url) {
    ESP_LOGD(TAG, "Starting audio stream download from: %s", music_url.c_str());
    
    // 验证URL有效性
    if (music_url.empty() || music_url.find("http") != 0) {
        ESP_LOGE(TAG, "Invalid URL format: %s", music_url.c_str());
        is_downloading_ = false;
        return;
    }
    
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);  // 音频流不设超时（需要持续下载）
    
    // 设置基本请求头
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "*/*");
    http->SetHeader("Range", "bytes=0-");  // 支持断点续传
    
    // 添加ESP32认证头
    add_auth_headers(http.get());
    
    if (!http->Open("GET", music_url)) {
        ESP_LOGE(TAG, "Failed to connect to music stream URL");
        is_downloading_ = false;
        return;
    }
    
    int status_code = http->GetStatusCode();
    if (status_code != 200 && status_code != 206) {  // 206 for partial content
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        is_downloading_ = false;
        return;
    }
    
    ESP_LOGI(TAG, "Started downloading audio stream, status: %d", status_code);
    
    // 分块读取音频数据
    const size_t chunk_size = 4096;  // 4KB每块
    char buffer[chunk_size];
    size_t total_downloaded = 0;
    
    while (is_downloading_ && is_playing_) {
        // 暂停时暂停下载，但不断开连接
        // 如果用户长时间暂停（超过5分钟），连接会自然超时，恢复时会重连
        if (is_paused_.load()) {
            // 等待恢复或停止
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this] { return !is_paused_.load() || !is_downloading_ || !is_playing_.load(); });
            
            // 检查是否被停止
            if (!is_downloading_ || !is_playing_) {
                ESP_LOGI(TAG, "Download stopped during pause");
                break;
            }
            
            ESP_LOGI(TAG, "Download resumed after pause");
            continue;
        }
        
        int bytes_read = http->Read(buffer, chunk_size);
        if (bytes_read < 0) {
            ESP_LOGE(TAG, "Failed to read audio data: error code %d", bytes_read);
            break;
        }
        if (bytes_read == 0) {
            ESP_LOGI(TAG, "Audio stream download completed, total: %d bytes", total_downloaded);
            break;
        }
        
        // 打印数据块信息
        // ESP_LOGI(TAG, "Downloaded chunk: %d bytes at offset %d", bytes_read, total_downloaded);
        
        // 安全地打印数据块的十六进制内容（前16字节）
        if (bytes_read >= 16) {
            // ESP_LOGI(TAG, "Data: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X ...", 
            //         (unsigned char)buffer[0], (unsigned char)buffer[1], (unsigned char)buffer[2], (unsigned char)buffer[3],
            //         (unsigned char)buffer[4], (unsigned char)buffer[5], (unsigned char)buffer[6], (unsigned char)buffer[7],
            //         (unsigned char)buffer[8], (unsigned char)buffer[9], (unsigned char)buffer[10], (unsigned char)buffer[11],
            //         (unsigned char)buffer[12], (unsigned char)buffer[13], (unsigned char)buffer[14], (unsigned char)buffer[15]);
        } else {
            ESP_LOGI(TAG, "Data chunk too small: %d bytes", bytes_read);
        }
        
        // 尝试检测文件格式（检查文件头）
        if (total_downloaded == 0 && bytes_read >= 4) {
            if (memcmp(buffer, "ID3", 3) == 0) {
                ESP_LOGI(TAG, "Detected MP3 file with ID3 tag");
            } else if (buffer[0] == 0xFF && (buffer[1] & 0xE0) == 0xE0) {
                ESP_LOGI(TAG, "Detected MP3 file header");
            } else if (memcmp(buffer, "RIFF", 4) == 0) {
                ESP_LOGI(TAG, "Detected WAV file");
            } else if (memcmp(buffer, "fLaC", 4) == 0) {
                ESP_LOGI(TAG, "Detected FLAC file");
            } else if (memcmp(buffer, "OggS", 4) == 0) {
                ESP_LOGI(TAG, "Detected OGG file");
            } else {
                // 检测到非音频格式（可能是 HTML 错误页面）
                ESP_LOGE(TAG, "Invalid audio format, first 4 bytes: %02X %02X %02X %02X", 
                        (unsigned char)buffer[0], (unsigned char)buffer[1], 
                        (unsigned char)buffer[2], (unsigned char)buffer[3]);
                
                // 检查是否是 HTML
                if (buffer[0] == '<') {
                    ESP_LOGE(TAG, "Received HTML instead of audio data - server returned error page");
                    Application::GetInstance().Alert("music", "抱歉，这首歌暂时无法播放");
                } else {
                    Application::GetInstance().Alert("music", "抱歉，音频格式不支持");
                }
                
                http->Close();
                is_downloading_ = false;
                return;
            }
        }
        
        // 创建音频数据块
        uint8_t* chunk_data = (uint8_t*)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
        if (!chunk_data) {
            ESP_LOGE(TAG, "Failed to allocate memory for audio chunk");
            break;
        }
        memcpy(chunk_data, buffer, bytes_read);
        
        // 等待缓冲区有空间
        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this] { return buffer_size_ < MAX_BUFFER_SIZE || !is_downloading_ || is_paused_.load(); });
            
            if (is_downloading_ && !is_paused_.load()) {
                audio_buffer_.push(AudioChunk(chunk_data, bytes_read));
                buffer_size_ += bytes_read;
                total_downloaded += bytes_read;
                
                // 通知播放线程有新数据
                buffer_cv_.notify_one();
                
                if (total_downloaded % (256 * 1024) == 0) {  // 每256KB打印一次进度
                    ESP_LOGI(TAG, "Downloaded %d bytes, buffer size: %d", total_downloaded, buffer_size_);
                }
            } else {
                heap_caps_free(chunk_data);
                if (!is_downloading_) {
                    break;
                }
                // 如果是暂停状态，释放当前chunk并继续循环
            }
        }
    }
    
    http->Close();
    is_downloading_ = false;
    
    // 通知播放线程下载完成
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    ESP_LOGI(TAG, "Audio stream download thread finished");
}

// 流式播放音频数据
void Esp32Music::PlayAudioStream() {
    ESP_LOGI(TAG, "Starting audio stream playback");
    
    // 初始化时间跟踪变量
    current_play_time_ms_ = 0;
    last_frame_time_ms_ = 0;
    total_frames_decoded_ = 0;
    
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec) {
        ESP_LOGE(TAG, "Audio codec not available");
        is_playing_ = false;
        
        // 通知用户播放失败
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display) {
            display->SetChatMessage("system", "音频设备暂时不可用，请检查设备连接。");
            ESP_LOGI(TAG, "Notified user about playback failure due to codec unavailable");
        }
        return;
    }
    
    // 确保音频输出启用，如果未启用则启用它
    if (!codec->output_enabled()) {
        ESP_LOGI(TAG, "Audio output disabled, enabling for music playback");
        codec->EnableOutput(true);
    }
    
    if (!mp3_decoder_initialized_) {
        ESP_LOGW(TAG, "MP3 decoder not initialized, attempting to re-initialize");
        if (!InitializeMp3Decoder()) {
            ESP_LOGE(TAG, "Failed to re-initialize MP3 decoder");
            is_playing_ = false;
            
            // 通知用户播放失败
            auto& board = Board::GetInstance();
            auto display = board.GetDisplay();
            if (display) {
                display->SetChatMessage("system", "抱歉，暂时无法播放这首歌曲，请稍后再试。");
                ESP_LOGI(TAG, "Notified user about playback failure due to decoder initialization failure");
            }
            return;
        }
        ESP_LOGI(TAG, "MP3 decoder re-initialized successfully after previous cleanup");
    }
    
    
    // 等待缓冲区有足够数据开始播放
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        buffer_cv_.wait(lock, [this] { 
            return buffer_size_ >= MIN_BUFFER_SIZE || (!is_downloading_ && !audio_buffer_.empty()); 
        });
    }
    
    ESP_LOGI(TAG, "Starting playback with buffer size: %d", buffer_size_);
    
    size_t total_played = 0;
    uint8_t* mp3_input_buffer = nullptr;
    int bytes_left = 0;
    uint8_t* read_ptr = nullptr;
    
    // 分配MP3输入缓冲区 - 使用RAII管理内存
    mp3_input_buffer = (uint8_t*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!mp3_input_buffer) {
        ESP_LOGE(TAG, "Failed to allocate MP3 input buffer");
        is_playing_ = false;
        return;
    }
    
    // 确保在函数退出时释放缓冲区
    auto buffer_guard = [](uint8_t* buffer) {
        if (buffer) {
            heap_caps_free(buffer);
        }
    };
    std::unique_ptr<uint8_t, decltype(buffer_guard)> buffer_ptr(mp3_input_buffer, buffer_guard);
    
    // 标记是否已经处理过ID3标签
    bool id3_processed = false;
    
    while (is_playing_) {
        // 检查是否暂停
        if (is_paused_.load()) {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this] { return !is_paused_.load() || !is_playing_.load(); });
            if (!is_playing_.load()) {
                break;  // 如果在暂停期间被停止，则退出
            }
            continue;
        }
        
        // 🔧 修复: 检查设备状态，只有在空闲状态才播放音乐，增强状态切换逻辑
        auto& app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();
        
        // 状态转换：说话中-》待机状态-》播放音乐
        if (current_state == kDeviceStateSpeaking) {
            ESP_LOGI(TAG, "Device is in speaking state, aborting speech for music playback");
            app.AbortSpeaking(kAbortReasonNone);
            vTaskDelay(pdMS_TO_TICKS(150)); // 等待语音完全停止
            continue;
        } else if (current_state == kDeviceStateListening) {
            ESP_LOGI(TAG, "Device is in listening state, switching to idle for music playback");
            app.ToggleChatState(); // 切换到待机状态
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        } else if (current_state != kDeviceStateIdle) { 
            // 不是待机状态，暂停音乐播放等待
            ESP_LOGD(TAG, "Device state is %d, pausing music playback", current_state);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // 设备状态检查通过，显示当前播放的歌名
        if (!song_name_displayed_ && !current_song_name_.empty()) {
            auto& board = Board::GetInstance();
            auto display = board.GetDisplay();
            if (display) {
                // 使用新的统一API直接设置歌曲详细信息
                display->SetMusicDetails(current_song_name_.c_str(), 
                                       !current_artist_.empty() ? current_artist_.c_str() : nullptr, 
                                       true);  // is_playing = true
                ESP_LOGI(TAG, "Displaying song details: %s by %s", 
                         current_song_name_.c_str(), 
                         !current_artist_.empty() ? current_artist_.c_str() : "Unknown Artist");
                song_name_displayed_ = true;
                
                // 歌词显示已启动
                ESP_LOGI(TAG, "Lyrics display active");
            }
        }
        
        // 如果需要更多MP3数据，从缓冲区读取
        if (bytes_left < 4096) {  // 保持至少4KB数据用于解码
            AudioChunk chunk;
            
            // 从缓冲区获取音频数据
            {
                std::unique_lock<std::mutex> lock(buffer_mutex_);
                if (audio_buffer_.empty()) {
                    if (!is_downloading_ && !is_paused_.load()) {
                        // 下载完成且缓冲区为空，且未暂停，播放结束
                        ESP_LOGI(TAG, "Playback finished, total played: %d bytes", total_played);
                        break;
                    }
                    // 等待新数据或恢复播放
                    buffer_cv_.wait(lock, [this] { 
                        return !audio_buffer_.empty() || !is_downloading_ || !is_playing_.load(); 
                    });
                    if (!is_playing_.load()) {
                        ESP_LOGI(TAG, "Playback stopped by user, total played: %d bytes", total_played);
                        break;
                    }
                    if (audio_buffer_.empty() && !is_downloading_ && !is_paused_.load()) {
                        ESP_LOGI(TAG, "Playback finished after wait, total played: %d bytes", total_played);
                        break;
                    }
                    if (audio_buffer_.empty()) {
                        continue;
                    }
                }
                
                chunk = audio_buffer_.front();
                audio_buffer_.pop();
                buffer_size_ -= chunk.size;
                
                // 通知下载线程缓冲区有空间
                buffer_cv_.notify_one();
            }
            
            // 将新数据添加到MP3输入缓冲区
            if (chunk.data && chunk.size > 0) {
                // 移动剩余数据到缓冲区开头
                if (bytes_left > 0 && read_ptr != mp3_input_buffer) {
                    memmove(mp3_input_buffer, read_ptr, bytes_left);
                }
                
                // 检查缓冲区空间
                size_t space_available = 8192 - bytes_left;
                size_t copy_size = std::min(chunk.size, space_available);
                
                // 复制新数据
                memcpy(mp3_input_buffer + bytes_left, chunk.data, copy_size);
                bytes_left += copy_size;
                read_ptr = mp3_input_buffer;
                
                // 检查并跳过ID3标签（仅在开始时处理一次）
                if (!id3_processed && bytes_left >= 10) {
                    size_t id3_skip = SkipId3Tag(read_ptr, bytes_left);
                    if (id3_skip > 0) {
                        read_ptr += id3_skip;
                        bytes_left -= id3_skip;
                        ESP_LOGI(TAG, "Skipped ID3 tag: %u bytes", (unsigned int)id3_skip);
                    }
                    id3_processed = true;
                }
                
                // 释放chunk内存
                heap_caps_free(chunk.data);
            }
        }
        
        // 尝试找到MP3帧同步
        int sync_offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync_offset < 0) {
            ESP_LOGW(TAG, "No MP3 sync word found, skipping %d bytes", bytes_left);
            bytes_left = 0;
            continue;
        }
        
        // 跳过到同步位置
        if (sync_offset > 0) {
            read_ptr += sync_offset;
            bytes_left -= sync_offset;
        }
        
        // 解码MP3帧
        // 将解码输出缓冲改为堆上分配，降低任务栈占用
        static const int kPcmSamples = 2304;
        int16_t* pcm_buffer = (int16_t*)heap_caps_malloc(kPcmSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!pcm_buffer) {
            ESP_LOGE(TAG, "Failed to allocate pcm_buffer");
            is_playing_ = false;
            break;
        }
        int decode_result = MP3Decode(mp3_decoder_, &read_ptr, &bytes_left, pcm_buffer, 0);
        
        if (decode_result == 0) {
            // 解码成功，获取帧信息
            MP3GetLastFrameInfo(mp3_decoder_, &mp3_frame_info_);
            total_frames_decoded_++;
            
            // 基本的帧信息有效性检查，防止除零错误
            if (mp3_frame_info_.samprate == 0 || mp3_frame_info_.nChans == 0) {
                ESP_LOGW(TAG, "Invalid frame info: rate=%d, channels=%d, skipping", 
                        mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                continue;
            }
            
            // 计算当前帧的持续时间(毫秒)
            int frame_duration_ms = (mp3_frame_info_.outputSamps * 1000) / 
                                  (mp3_frame_info_.samprate * mp3_frame_info_.nChans);
            
            // 更新当前播放时间
            current_play_time_ms_ += frame_duration_ms;
            
            // 减少频繁的日志输出，只在每100帧输出一次
            if (total_frames_decoded_ % 100 == 0) {
                ESP_LOGD(TAG, "Frame %d: time=%lldms, duration=%dms, rate=%d, ch=%d", 
                        total_frames_decoded_, current_play_time_ms_, frame_duration_ms,
                        mp3_frame_info_.samprate, mp3_frame_info_.nChans);
            }
            
            // 更新歌词显示
                UpdateLyricDisplay(current_play_time_ms_);
            
            // 将PCM数据发送到Application的音频解码队列
            if (mp3_frame_info_.outputSamps > 0) {
                int16_t* final_pcm_data = pcm_buffer;
                int final_sample_count = mp3_frame_info_.outputSamps;
                std::vector<int16_t> mono_buffer;
                
                // 如果是双通道，转换为单通道混合
                if (mp3_frame_info_.nChans == 2) {
                    // 双通道转单通道：将左右声道混合
                    int stereo_samples = mp3_frame_info_.outputSamps;  // 包含左右声道的总样本数
                    int mono_samples = stereo_samples / 2;  // 实际的单声道样本数
                    
                    mono_buffer.resize(mono_samples);
                    
                    for (int i = 0; i < mono_samples; ++i) {
                        // 混合左右声道 (L + R) / 2
                        int left = pcm_buffer[i * 2];      // 左声道
                        int right = pcm_buffer[i * 2 + 1]; // 右声道
                        mono_buffer[i] = (int16_t)((left + right) / 2);
                    }
                    
                    final_pcm_data = mono_buffer.data();
                    final_sample_count = mono_samples;

                    // 减少频繁日志输出
                    if (total_frames_decoded_ % 200 == 0) {
                        ESP_LOGD(TAG, "Converted stereo to mono: %d -> %d samples", 
                                stereo_samples, mono_samples);
                    }
                } else if (mp3_frame_info_.nChans == 1) {
                    // 已经是单声道，无需转换
                    ESP_LOGD(TAG, "Already mono audio: %d samples", final_sample_count);
                } else {
                    ESP_LOGW(TAG, "Unsupported channel count: %d, treating as mono", 
                            mp3_frame_info_.nChans);
                }
                
                // 创建AudioStreamPacket
                AudioStreamPacket packet;
                packet.sample_rate = mp3_frame_info_.samprate;
                packet.frame_duration = 60;  // 使用Application默认的帧时长
                packet.timestamp = 0;
                
                // 将int16_t PCM数据转换为uint8_t字节数组
                size_t pcm_size_bytes = final_sample_count * sizeof(int16_t);
                packet.payload.resize(pcm_size_bytes);
                memcpy(packet.payload.data(), final_pcm_data, pcm_size_bytes);

                // 🔧 修复: 动态管理 FFT 缓冲区，避免大小不匹配和内存泄漏
                // 注意：GetAudioData() 可能被外部调用，所以需要保留最新的数据
                // 但我们需要确保缓冲区大小匹配，避免溢出
                static size_t last_fft_buffer_size = 0;
                size_t required_size = final_sample_count * sizeof(int16_t);
                
                if (final_pcm_data_fft == nullptr || required_size > last_fft_buffer_size) {
                    // 需要重新分配：首次分配或大小不够
                    if (final_pcm_data_fft != nullptr) {
                        heap_caps_free(final_pcm_data_fft);
                        final_pcm_data_fft = nullptr;
                    }
                    
                    final_pcm_data_fft = (int16_t*)heap_caps_malloc(required_size, MALLOC_CAP_SPIRAM);
                    if (final_pcm_data_fft == nullptr) {
                        ESP_LOGE(TAG, "Failed to allocate FFT buffer: %d bytes", required_size);
                        last_fft_buffer_size = 0;
                    } else {
                        last_fft_buffer_size = required_size;
                        ESP_LOGD(TAG, "FFT buffer allocated: %d bytes", required_size);
                    }
                }
                
                if (final_pcm_data_fft != nullptr) {
                    memcpy(final_pcm_data_fft, final_pcm_data, required_size);
                }
                
                // 减少频繁日志输出，只在每1000帧输出一次
                if (total_frames_decoded_ % 1000 == 0) {
                    ESP_LOGD(TAG, "Sending %d PCM samples (%d bytes, rate=%d, channels=%d->1) to Application", 
                            final_sample_count, pcm_size_bytes, mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                }
                
                // 发送到Application的音频解码队列
                auto& app = Application::GetInstance();
                app.AddAudioData(std::move(packet));
                total_played += pcm_size_bytes;
                
                // 打印播放进度
                if (total_played % (128 * 1024) == 0) {
                    ESP_LOGI(TAG, "Played %d bytes, buffer size: %d", total_played, buffer_size_);
                }
            }
            
        } else {
            // 解码失败
            ESP_LOGW(TAG, "MP3 decode failed with error: %d", decode_result);
                
            // 跳过一些字节继续尝试
                if (bytes_left > 1) {
                    read_ptr++;
                    bytes_left--;
                } else {
                    bytes_left = 0;
            }
        }
        heap_caps_free(pcm_buffer);
    }
    
    // MP3缓冲区由RAII自动清理，无需手动释放
    
    // 播放结束时进行基本清理，但不调用StopStreaming避免线程自我等待
    ESP_LOGI(TAG, "Audio stream playback finished, total played: %d bytes", total_played);
    
    // 如果没有播放任何数据，说明音频文件无效
    if (total_played == 0 && !is_paused_.load()) {
        ESP_LOGE(TAG, "Failed to play any audio data - invalid audio file");
        Application::GetInstance().Alert("music", "抱歉，这首歌暂时无法播放");
    }
    
    ESP_LOGI(TAG, "Performing basic cleanup from play thread");
    
    // 停止播放标志
    is_playing_ = false;
    is_paused_ = false;  // 重置暂停状态
    
    // 清理音频缓冲区释放内存
    ClearAudioBuffer();
    
    // 🔧 修复: 清理 MP3 解码器（自然播放完后必须释放！）
    CleanupMp3Decoder();
    
    // 🔧 修复: 释放 FFT 缓冲区
    if (final_pcm_data_fft) {
        heap_caps_free(final_pcm_data_fft);
        final_pcm_data_fft = nullptr;
        ESP_LOGI(TAG, "FFT buffer freed in playback thread");
    }
    
    // 🔧 修复: 清空音频解码队列
    {
        auto& app = Application::GetInstance();
        app.ClearAudioQueue();
    }
    
    // 停止歌词线程（不在播放线程中join，避免死锁）
    is_lyric_running_ = false;
    
    // 清理歌词数据释放内存
    lyrics_.clear();
    lyrics_.shrink_to_fit();  // 释放 vector 容量
    current_lyric_index_ = -1;
    
    // 重置其他状态变量和清理字符串数据
    current_play_time_ms_ = 0;
    total_duration_ms_ = 0;
    song_name_displayed_ = false;
    
    // 清理字符串数据释放内存
    current_song_name_.clear();
    current_song_name_.shrink_to_fit();
    current_artist_.clear();
    current_artist_.shrink_to_fit();
    current_music_url_.clear();
    current_music_url_.shrink_to_fit();
    current_lyric_url_.clear();
    current_lyric_url_.shrink_to_fit();
    last_downloaded_data_.clear();
    last_downloaded_data_.shrink_to_fit();
    
    // 内存追踪（DEBUG级别）
    ESP_LOGD(TAG, "Memory after playback - SRAM: %d bytes, SPIRAM: %d bytes",
            heap_caps_get_free_size(MALLOC_CAP_8BIT),
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    // 通知显示器播放已完成
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display) {
        // 使用新的统一API通知播放结束
        display->SetMusicDetails("", "", false);  // is_playing = false
        ESP_LOGI(TAG, "Notified display that music playback ended");
        
        // 删除频谱相关代码
    } else {
        ESP_LOGI(TAG, "No display available for cleanup");
    }
    
    // 🔧 不需要异步 join 线程，因为：
    // 1. 下载/歌词线程已经自然结束（is_downloading/is_lyric_running 为 false）
    // 2. 在 StartStreaming() 中，旧线程会被 detach，新线程会创建
    // 3. detach 的线程会在退出时自动回收资源
    // 4. 不需要 join，避免竞态条件导致的崩溃
}

// 清空音频缓冲区
void Esp32Music::ClearAudioBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    while (!audio_buffer_.empty()) {
        AudioChunk chunk = audio_buffer_.front();
        audio_buffer_.pop();
        if (chunk.data) {
            heap_caps_free(chunk.data);
        }
    }
    
    buffer_size_ = 0;
    ESP_LOGI(TAG, "Audio buffer cleared");
}

// 初始化MP3解码器
bool Esp32Music::InitializeMp3Decoder() {
    mp3_decoder_ = MP3InitDecoder();
    if (mp3_decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        mp3_decoder_initialized_ = false;
        return false;
    }
    
    mp3_decoder_initialized_ = true;
    ESP_LOGI(TAG, "MP3 decoder initialized successfully");
    return true;
}

// 清理MP3解码器
void Esp32Music::CleanupMp3Decoder() {
    if (mp3_decoder_ != nullptr) {
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
    }
    mp3_decoder_initialized_ = false;
    ESP_LOGI(TAG, "MP3 decoder cleaned up");
}

// 重置采样率到原始值
void Esp32Music::ResetSampleRate() {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec && codec->original_output_sample_rate() > 0 && 
        codec->output_sample_rate() != codec->original_output_sample_rate()) {
        ESP_LOGI(TAG, "重置采样率：从 %d Hz 重置到原始值 %d Hz", 
                codec->output_sample_rate(), codec->original_output_sample_rate());
        if (codec->SetOutputSampleRate(-1)) {  // -1 表示重置到原始值
            ESP_LOGI(TAG, "成功重置采样率到原始值: %d Hz", codec->output_sample_rate());
        } else {
            ESP_LOGW(TAG, "无法重置采样率到原始值");
        }
    }
}

// 跳过MP3文件开头的ID3标签
size_t Esp32Music::SkipId3Tag(uint8_t* data, size_t size) {
    if (!data || size < 10) {
        return 0;
    }
    
    // 检查ID3v2标签头 "ID3"
    if (memcmp(data, "ID3", 3) != 0) {
        return 0;
    }
    
    // 计算标签大小（synchsafe integer格式）
    uint32_t tag_size = ((uint32_t)(data[6] & 0x7F) << 21) |
                        ((uint32_t)(data[7] & 0x7F) << 14) |
                        ((uint32_t)(data[8] & 0x7F) << 7)  |
                        ((uint32_t)(data[9] & 0x7F));
    
    // ID3v2头部(10字节) + 标签内容
    size_t total_skip = 10 + tag_size;
    
    // 确保不超过可用数据大小
    if (total_skip > size) {
        total_skip = size;
    }
    
    ESP_LOGI(TAG, "Found ID3v2 tag, skipping %u bytes", (unsigned int)total_skip);
    return total_skip;
}

// 下载歌词
bool Esp32Music::DownloadLyrics(const std::string& lyric_url) {
    ESP_LOGI(TAG, "Downloading lyrics from: %s", lyric_url.c_str());
    
    // 检查URL是否为空
    if (lyric_url.empty()) {
        ESP_LOGD(TAG, "Lyric URL is empty (song may not have lyrics)");
        return false;
    }
    
    // 只尝试1次，快速失败（避免卡顿体验差）
    const int max_retries = 1;
    int retry_count = 0;
    bool success = false;
    std::string lyric_content;
    std::string current_url = lyric_url;
    int redirect_count = 0;
    const int max_redirects = 5;  // 最多允许5次重定向
    
    while (retry_count < max_retries && !success && redirect_count < max_redirects) {
        if (retry_count > 0) {
            ESP_LOGI(TAG, "Retrying lyric download (attempt %d of %d)", retry_count + 1, max_retries);
            // 重试前暂停一下
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        // 使用Board提供的HTTP客户端
        auto network = Board::GetInstance().GetNetwork();
        auto http = network->CreateHttp(5);  // 歌词下载设置5秒超时
        if (!http) {
            ESP_LOGW(TAG, "Failed to create HTTP client for lyric download");
            retry_count++;
            continue;
        }
        
        // 设置基本请求头
        http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
        http->SetHeader("Accept", "text/plain");
        
        // 添加ESP32认证头
        add_auth_headers(http.get());
        
        // 打开GET连接
        if (!http->Open("GET", current_url)) {
            ESP_LOGD(TAG, "Failed to open HTTP connection for lyrics (may not be available)");
            // 移除delete http; 因为unique_ptr会自动管理内存
            retry_count++;
            continue;
        }
        
        // 检查HTTP状态码
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "Lyric download HTTP status code: %d", status_code);
        
        // 处理重定向 - 由于Http类没有GetHeader方法，我们只能根据状态码判断
        if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) {
            // 由于无法获取Location头，只能报告重定向但无法继续
            ESP_LOGW(TAG, "Received redirect status %d but cannot follow redirect (no GetHeader method)", status_code);
            http->Close();
            retry_count++;
            continue;
        }
        
        // 非200系列状态码视为错误
        if (status_code < 200 || status_code >= 300) {
            http->Close();
            
            // 如果是404（未找到）或410（已删除），立即放弃，不重试
            if (status_code == 404 || status_code == 410) {
                ESP_LOGD(TAG, "Lyrics not found (HTTP %d), song will play without lyrics", status_code);
                return false;  // 直接返回，不再重试
            }
            
            ESP_LOGW(TAG, "Lyric download failed with HTTP %d", status_code);
            retry_count++;
            continue;
        }
        
        // 读取响应
        lyric_content.clear();
        char buffer[1024];
        int bytes_read;
        bool read_error = false;
        int total_read = 0;
        
        // 由于无法获取Content-Length和Content-Type头，我们不知道预期大小和内容类型
        ESP_LOGD(TAG, "Starting to read lyric content");
        
        while (true) {
            bytes_read = http->Read(buffer, sizeof(buffer) - 1);
            // ESP_LOGD(TAG, "Lyric HTTP read returned %d bytes", bytes_read); // 注释掉以减少日志输出
            
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                lyric_content += buffer;
                total_read += bytes_read;
                
                // 定期打印下载进度 - 改为DEBUG级别减少输出
                if (total_read % 4096 == 0) {
                    ESP_LOGD(TAG, "Downloaded %d bytes so far", total_read);
                }
            } else if (bytes_read == 0) {
                // 正常结束，没有更多数据
                ESP_LOGD(TAG, "Lyric download completed, total bytes: %d", total_read);
                success = true;
                break;
            } else {
                // bytes_read < 0，可能是ESP-IDF的已知问题
                // 如果已经读取到了一些数据，则认为下载成功
                if (!lyric_content.empty()) {
                    ESP_LOGW(TAG, "HTTP read returned %d, but we have data (%d bytes), continuing", bytes_read, lyric_content.length());
                    success = true;
                    break;
                } else {
                    ESP_LOGD(TAG, "Failed to read lyric data: error code %d", bytes_read);
                    read_error = true;
                    break;
                }
            }
        }
        
        http->Close();
        
        if (read_error) {
            retry_count++;
            continue;
        }
        
        // 如果成功读取数据，跳出重试循环
        if (success) {
            break;
        }
    }
    
    // 检查是否超过了最大重试次数
    if (retry_count >= max_retries) {
        ESP_LOGD(TAG, "Lyrics not available after %d attempts (song will play without lyrics)", max_retries);
        return false;
    }
    
    // 记录前几个字节的数据，帮助调试
    if (!lyric_content.empty()) {
        size_t preview_size = std::min(lyric_content.size(), size_t(50));
        std::string preview = lyric_content.substr(0, preview_size);
        ESP_LOGD(TAG, "Lyric content preview (%d bytes): %s", lyric_content.length(), preview.c_str());
    } else {
        ESP_LOGD(TAG, "Lyrics are empty (song will play without lyrics)");
        return false;
    }
    
    ESP_LOGI(TAG, "Lyrics downloaded successfully, size: %d bytes", lyric_content.length());
    return ParseLyrics(lyric_content);
}

// 解析歌词
bool Esp32Music::ParseLyrics(const std::string& lyric_content) {
    ESP_LOGI(TAG, "Parsing lyrics content");
    
    // 使用锁保护lyrics_数组访问
    std::lock_guard<std::mutex> lock(lyrics_mutex_);
    
    lyrics_.clear();
    lyrics_.shrink_to_fit();  // 释放旧的歌词内存
    
    // 按行分割歌词内容
    std::istringstream stream(lyric_content);
    std::string line;
    
    while (std::getline(stream, line)) {
        // 去除行尾的回车符
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // 跳过空行
        if (line.empty()) {
            continue;
        }
        
        // 解析LRC格式: [mm:ss.xx]歌词文本
        if (line.length() > 10 && line[0] == '[') {
            size_t close_bracket = line.find(']');
            if (close_bracket != std::string::npos) {
                std::string tag_or_time = line.substr(1, close_bracket - 1);
                std::string content = line.substr(close_bracket + 1);
                
                // 检查是否是元数据标签而不是时间戳
                // 元数据标签通常是 [ti:标题], [ar:艺术家], [al:专辑] 等
                size_t colon_pos = tag_or_time.find(':');
                if (colon_pos != std::string::npos) {
                    std::string left_part = tag_or_time.substr(0, colon_pos);
                    
                    // 检查冒号左边是否是时间（数字）
                    bool is_time_format = true;
                    for (char c : left_part) {
                        if (!isdigit(c)) {
                            is_time_format = false;
                            break;
                        }
                    }
                    
                    // 如果不是时间格式，跳过这一行（元数据标签）
                    if (!is_time_format) {
                        // 可以在这里处理元数据，例如提取标题、艺术家等信息
                        ESP_LOGD(TAG, "Skipping metadata tag: [%s]", tag_or_time.c_str());
                        continue;
                    }
                    
                    // 是时间格式，解析时间戳
                    try {
                        int minutes = std::stoi(tag_or_time.substr(0, colon_pos));
                        float seconds = std::stof(tag_or_time.substr(colon_pos + 1));
                        int timestamp_ms = minutes * 60 * 1000 + (int)(seconds * 1000);
                        
                        // 安全处理歌词文本，确保UTF-8编码正确
                        std::string safe_lyric_text;
                        if (!content.empty()) {
                            // 创建安全副本并验证字符串
                            safe_lyric_text = content;
                            // 确保字符串以null结尾
                            safe_lyric_text.shrink_to_fit();
                        }
                        
                        lyrics_.push_back(std::make_pair(timestamp_ms, safe_lyric_text));
                        
                        if (!safe_lyric_text.empty()) {
                            // 限制日志输出长度，避免中文字符截断问题
                            size_t log_len = std::min(safe_lyric_text.length(), size_t(50));
                            std::string log_text = safe_lyric_text.substr(0, log_len);
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] %s", timestamp_ms, log_text.c_str());
                        } else {
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] (empty)", timestamp_ms);
                        }
                    } catch (const std::exception& e) {
                        ESP_LOGW(TAG, "Failed to parse time: %s", tag_or_time.c_str());
                    }
                }
            }
        }
    }
    
    // 按时间戳排序
    std::sort(lyrics_.begin(), lyrics_.end());
    
    ESP_LOGI(TAG, "Parsed %d lyric lines", lyrics_.size());
    return !lyrics_.empty();
}

// 歌词显示线程
void Esp32Music::LyricDisplayThread() {
    ESP_LOGI(TAG, "Lyric display thread started");
    
    if (!DownloadLyrics(current_lyric_url_)) {
        ESP_LOGI(TAG, "Lyrics not available for this song (will play without lyrics)");
        is_lyric_running_ = false;
        return;
    }
    
    // 定期检查是否需要更新显示(降低频率以减少CPU使用)
    while (is_lyric_running_ && is_playing_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));  // 增加到500ms，减少CPU负载
    }
    
    ESP_LOGI(TAG, "Lyric display thread finished");
}

void Esp32Music::UpdateLyricDisplay(int64_t current_time_ms) {
    std::lock_guard<std::mutex> lock(lyrics_mutex_);
    
    if (lyrics_.empty()) {
        return;
    }
    
    // 查找当前应该显示的歌词
    int new_lyric_index = -1;
    
    // 从当前歌词索引开始查找，提高效率
    int start_index = (current_lyric_index_.load() >= 0) ? current_lyric_index_.load() : 0;
    
    // 正向查找：找到最后一个时间戳小于等于当前时间的歌词
    for (int i = start_index; i < (int)lyrics_.size(); i++) {
        if (lyrics_[i].first <= current_time_ms) {
            new_lyric_index = i;
        } else {
            break;  // 时间戳已超过当前时间
        }
    }
    
    // 如果没有找到(可能当前时间比第一句歌词还早)，显示空
    if (new_lyric_index == -1) {
        new_lyric_index = -1;
    }
    
    // 如果歌词索引发生变化，更新显示
    if (new_lyric_index != current_lyric_index_) {
        current_lyric_index_ = new_lyric_index;
        
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display) {
            std::string lyric_text;
            
            if (current_lyric_index_ >= 0 && current_lyric_index_ < (int)lyrics_.size()) {
                lyric_text = lyrics_[current_lyric_index_].second;
            }
            
            // 显示歌词到音乐播放器UI
            auto lcd_display = dynamic_cast<LcdDisplay*>(display);
            if (lcd_display) {
                lcd_display->UpdateMusicLyrics(lyric_text.c_str());
            } else {
                // 如果不是LCD显示器，使用原来的方式
                display->SetChatMessage("lyric", lyric_text.c_str());
            }
            
            ESP_LOGD(TAG, "Lyric update at %lldms: %s", 
                    current_time_ms, 
                    lyric_text.empty() ? "(no lyric)" : lyric_text.c_str());
        }
    }
}

// 删除复杂的认证初始化方法，使用简单的静态函数

// 删除复杂的类方法，使用简单的静态函数

/**
 * @brief 添加认证头到HTTP请求
 * @param http_client HTTP客户端指针
 * 
 * 添加的认证头包括：
 * - X-MAC-Address: 设备MAC地址
 * - X-Chip-ID: 设备芯片ID
 * - X-Timestamp: 当前时间戳
 * - X-Dynamic-Key: 动态生成的密钥
 */
// 删除复杂的AddAuthHeaders方法，使用简单的静态函数

// 删除复杂的认证验证和配置方法，使用简单的静态函数


// 暂停播放
bool Esp32Music::PauseStreaming() {
    if (!is_playing_.load() || is_paused_.load()) {
        ESP_LOGW(TAG, "Cannot pause: not playing or already paused");
        return false;
    }
    
    is_paused_ = true;
    // 记录暂停时间，用于判断是否需要重连
    pause_start_time_ = esp_timer_get_time();
    ESP_LOGI(TAG, "Music playback paused");
    return true;
}

// 继续播放
bool Esp32Music::ResumeStreaming() {
    if (!is_playing_.load()) {
        ESP_LOGW(TAG, "Cannot resume: music is not playing (playback may have finished)");
        return false;
    }
    
    if (!is_paused_.load()) {
        ESP_LOGW(TAG, "Cannot resume: music is not paused");
        return false;
    }
    
    // 确保设备处于空闲状态以便播放音乐
    auto& app = Application::GetInstance();
    DeviceState current_state = app.GetDeviceState();
    
    if (current_state == kDeviceStateListening || current_state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Device state is %d, switching to idle for music resume", current_state);
        app.ToggleChatState(); // 切换到待机状态
        vTaskDelay(pdMS_TO_TICKS(100)); // 短暂延迟确保状态切换
    }
    
    // 检查暂停时长（仅用于日志）
    int64_t pause_duration_us = esp_timer_get_time() - pause_start_time_;
    int64_t pause_duration_ms = pause_duration_us / 1000;
    ESP_LOGI(TAG, "Resuming after pause of %lld ms", pause_duration_ms);
    
    // 检查下载线程是否还在运行
    if (!is_downloading_.load()) {
        // 下载线程已停止（可能因为网络超时或下载完成）
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        if (audio_buffer_.empty() && !current_music_url_.empty()) {
            ESP_LOGI(TAG, "Download thread stopped and buffer empty, restarting download");
            
            // 确保旧的下载线程已完全清理
            if (download_thread_.joinable()) {
                ESP_LOGW(TAG, "Warning: download thread still joinable, cleaning up");
                SafeJoinThread(download_thread_, "download-cleanup");
            }
            
            // 重新启动下载线程
            is_downloading_ = true;
            download_thread_ = std::thread(&Esp32Music::DownloadAudioStream, this, current_music_url_);
        } else if (!audio_buffer_.empty()) {
            ESP_LOGI(TAG, "Download completed during pause, buffer has %zu bytes", buffer_size_);
        }
    } else {
        ESP_LOGI(TAG, "Download thread still running, buffer has %zu bytes", buffer_size_);
    }
    
    is_paused_ = false;
    buffer_cv_.notify_all();  // 唤醒可能等待的播放线程
    ESP_LOGI(TAG, "Music playback resumed");
    return true;
}

// 统一的线程安全清理函数
void Esp32Music::SafeJoinThread(std::thread& thread, const std::string& thread_name, int timeout_ms) {
    if (!thread.joinable()) {
        return;
    }
    
    ESP_LOGI(TAG, "Joining %s thread", thread_name.c_str());
    
    try {
        // 对于播放线程，使用超时机制
        if (thread_name.find("play") != std::string::npos) {
            auto start_time = esp_timer_get_time();
            bool joined = false;
            
            while (!joined && (esp_timer_get_time() - start_time) < (timeout_ms * 1000)) {
                if (!thread.joinable()) {
                    joined = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            
            if (!joined && thread.joinable()) {
                ESP_LOGW(TAG, "%s thread join timeout, detaching", thread_name.c_str());
                thread.detach();
                return;
            }
        }
        
        if (thread.joinable()) {
            thread.join();
            ESP_LOGI(TAG, "%s thread joined successfully", thread_name.c_str());
        }
    } catch (const std::exception& e) {
        ESP_LOGW(TAG, "Exception during %s thread join: %s", thread_name.c_str(), e.what());
        try {
            if (thread.joinable()) {
                thread.detach();
                ESP_LOGW(TAG, "%s thread detached after join failure", thread_name.c_str());
            }
        } catch (const std::exception& e2) {
            ESP_LOGE(TAG, "Failed to detach %s thread: %s", thread_name.c_str(), e2.what());
        }
    }
}