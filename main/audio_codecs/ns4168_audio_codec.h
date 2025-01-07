#pragma once

#include "audio_codec.h"
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <vector>

class NS4168AudioCodec : public AudioCodec {
private:
    static const size_t AUDIO_BUFFER_SIZE = 4096;  // 4K 缓冲区
    std::vector<int16_t> audio_buffer_;
    size_t buffer_pos_;

    i2s_chan_handle_t tx_handle_ = nullptr;
    i2s_chan_handle_t rx_handle_ = nullptr;

public:
    NS4168AudioCodec(int input_sample_rate, int output_sample_rate,
                    gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout,
                    gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din);
    virtual ~NS4168AudioCodec();

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;
}; 