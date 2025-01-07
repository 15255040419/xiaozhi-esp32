#include "ns4168_audio_codec.h"
#include <esp_log.h>
#include <cmath>
#include <cstring>

#define TAG "NS4168AudioCodec"

NS4168AudioCodec::~NS4168AudioCodec() {
    if (rx_handle_ != nullptr) {
        i2s_channel_disable(rx_handle_);
        i2s_del_channel(rx_handle_);
    }
    if (tx_handle_ != nullptr) {
        i2s_channel_disable(tx_handle_);
        i2s_del_channel(tx_handle_);
    }
}

NS4168AudioCodec::NS4168AudioCodec(int input_sample_rate, int output_sample_rate,
                                 gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout,
                                 gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din) {
    ESP_LOGI(TAG, "Initializing NS4168 codec...");
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    duplex_ = true;

    // 配置 NS4168 I2S 输出通道
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)0, I2S_ROLE_MASTER);
    tx_chan_cfg.dma_desc_num = 2;
    tx_chan_cfg.dma_frame_num = 240 * 3;  // 720 帧
    tx_chan_cfg.auto_clear = true;
    tx_chan_cfg.auto_clear_before_cb = false;
    tx_chan_cfg.intr_priority = 0;

    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_handle_, nullptr));
    if (tx_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create TX channel");
        return;
    }

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(output_sample_rate_)),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = spk_bclk,
            .ws = spk_ws,
            .dout = spk_dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &tx_std_cfg));
    ESP_LOGI(TAG, "NS4168 output initialized");

    // 配置麦克风 I2S 输入通道
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)1, I2S_ROLE_MASTER);
    rx_chan_cfg.dma_desc_num = 4;
    rx_chan_cfg.dma_frame_num = 480;
    rx_chan_cfg.auto_clear = false;
    rx_chan_cfg.auto_clear_before_cb = false;
    rx_chan_cfg.intr_priority = 5;

    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, nullptr, &rx_handle_));
    if (rx_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create RX channel");
        return;
    }

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(input_sample_rate_)),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = mic_sck,
            .ws = mic_ws,
            .dout = I2S_GPIO_UNUSED,
            .din = mic_din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };

    ESP_LOGI(TAG, "Microphone config - sample rate: %d, bclk: %d, ws: %d, din: %d",
             input_sample_rate_, mic_sck, mic_ws, mic_din);

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &rx_std_cfg));
    ESP_LOGI(TAG, "Microphone input initialized");

    // 测试麦克风是否工作
    std::vector<int16_t> test_buffer(480);
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(rx_handle_, test_buffer.data(),
                                    test_buffer.size() * sizeof(int16_t),
                                    &bytes_read, 100);
    if (err == ESP_OK && bytes_read > 0) {
        ESP_LOGI(TAG, "Microphone test successful, read %d bytes", bytes_read);
        // 打印一些采样值
        for (int i = 0; i < 5 && i < bytes_read/2; i++) {
            ESP_LOGI(TAG, "Sample[%d] = %d", i, test_buffer[i]);
        }
    } else {
        ESP_LOGE(TAG, "Microphone test failed: %s", esp_err_to_name(err));
    }

    // 初始化音频缓冲区
    audio_buffer_.resize(AUDIO_BUFFER_SIZE);
    buffer_pos_ = 0;
}

int NS4168AudioCodec::Write(const int16_t* data, int samples) {
    if (!output_enabled_) {
        return 0;
    }

    std::vector<int16_t> buffer(samples * 2);  // 双声道
    
    // 应用音量并转换为立体声
    float volume = output_volume_ / 100.0f;
    for (int i = 0; i < samples; i++) {
        int16_t sample = data[i] * volume;
        buffer[i * 2] = sample;     // 左声道
        buffer[i * 2 + 1] = sample; // 右声道
    }

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(tx_handle_, buffer.data(), 
                                     buffer.size() * sizeof(int16_t),
                                     &bytes_written, 10);  // 10ms 超时
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "Failed to write audio data: %s", esp_err_to_name(err));
        return 0;
    }
    return bytes_written / (2 * sizeof(int16_t));  // 返回实际写入的单声道样本数
}

int NS4168AudioCodec::Read(int16_t* dest, int samples) {
    if (!input_enabled_) {
        return 0;
    }

    // 如果缓冲区中的数据不足，则读取更多数据
    if (buffer_pos_ < samples) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(rx_handle_, 
                                        audio_buffer_.data() + buffer_pos_,
                                        (AUDIO_BUFFER_SIZE - buffer_pos_) * sizeof(int16_t),
                                        &bytes_read, 0);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "Failed to read audio data: %s", esp_err_to_name(err));
            return 0;
        }
        buffer_pos_ += bytes_read / sizeof(int16_t);
    }

    // 复制数据到目标缓冲区
    int output_samples = std::min(samples, (int)buffer_pos_);
    if (output_samples > 0) {
        memcpy(dest, audio_buffer_.data(), output_samples * sizeof(int16_t));
        
        // 移动剩余数据到缓冲区开头
        if (buffer_pos_ > output_samples) {
            memmove(audio_buffer_.data(), 
                   audio_buffer_.data() + output_samples,
                   (buffer_pos_ - output_samples) * sizeof(int16_t));
        }
        buffer_pos_ -= output_samples;
    }

    return output_samples;
} 