#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/st7789_display.h"
#include "application.h"
#include "button.h"
#include "led.h"
#include "config.h"
#include "i2c_device.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>

#define TAG "LichuangRanhBoard"

class Pca9557 : public I2cDevice {
public:
    Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x01, 0x03);
        WriteReg(0x03, 0xf8);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint8_t data = ReadReg(0x01);
        data = (data & ~(1 << bit)) | (level << bit);
        WriteReg(0x01, data);
    }
};

class LichuangRanhBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t pca9557_handle_;
    Button boot_button_;
    St7789Display* display_;
    Pca9557* pca9557_;

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        pca9557_ = new Pca9557(i2c_bus_, 0x18);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {
            .mosi_io_num = GPIO_NUM_11,
            .miso_io_num = GPIO_NUM_NC,
            .sclk_io_num = GPIO_NUM_10,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .data2_io_num = GPIO_NUM_NC,
            .data3_io_num = GPIO_NUM_NC,
            .data4_io_num = GPIO_NUM_NC,
            .data5_io_num = GPIO_NUM_NC,
            .data6_io_num = GPIO_NUM_NC,
            .data7_io_num = GPIO_NUM_NC,
            .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2 + 8,
            .flags = SPICOMMON_BUSFLAG_MASTER,
            .intr_flags = 0,
        };
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeSt7789Display() {
        display_ = new St7789Display(GPIO_NUM_9, GPIO_NUM_46, DISPLAY_BACKLIGHT_PIN);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            Application::GetInstance().ToggleChatState();
        });
    }

public:
    LichuangRanhBoard() : boot_button_(BOOT_BUTTON_GPIO) {
    }

    virtual void Initialize() override {
        ESP_LOGI(TAG, "Initializing LichuangRanhBoard");
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();
        WifiBoard::Initialize();
    }

    virtual Led* GetBuiltinLed() override {
        static Led led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec* audio_codec = nullptr;
        if (audio_codec == nullptr) {
            audio_codec = new BoxAudioCodec(i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
                GPIO_NUM_NC, AUDIO_CODEC_ES8311_ADDR, AUDIO_CODEC_ES7210_ADDR, AUDIO_INPUT_REFERENCE);
            audio_codec->SetOutputVolume(AUDIO_DEFAULT_OUTPUT_VOLUME);
        }
        return audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(LichuangRanhBoard); 