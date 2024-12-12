#include "ap5056.h"
#include "esp_log.h"

static const char* TAG = "AP5056";

Ap5056::Ap5056(gpio_num_t vbat_pin) : vbat_pin_(vbat_pin) {
    // 根据GPIO号获取对应的ADC通道
    adc_channel_ = ADC_CHANNEL_7;  // GPIO8对应ADC1的通道7
}

Ap5056::~Ap5056() {
    if (adc_handle_) {
        adc_oneshot_del_unit(adc_handle_);
    }
}

bool Ap5056::Initialize() {
    // 配置ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle_));

    // 配置ADC通道
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, adc_channel_, &config));
    
    return true;
}

int Ap5056::ReadVoltage() {
    int adc_raw;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle_, adc_channel_, &adc_raw));
    return adc_raw;
}

float Ap5056::GetBatteryVoltage() {
    int raw = ReadVoltage();
    // 将ADC值转换为电压值(根据实际电路调整)
    return raw * (3.3f / 4095.0f) * 2;  // 假设使用分压电路，实际值需要校准
}

int Ap5056::GetBatteryLevel() {
    float voltage = GetBatteryVoltage();
    // 根据电池电压计算电量百分比
    if (voltage >= 4.2f) return 100;
    if (voltage <= 3.0f) return 0;
    return (int)((voltage - 3.0f) / 1.2f * 100);
}

bool Ap5056::IsCharging() {
    // 通���电压变化趋势判断是否在充电
    static float last_voltage = 0;
    float current_voltage = GetBatteryVoltage();
    bool charging = current_voltage > last_voltage;
    last_voltage = current_voltage;
    return charging;
}

bool Ap5056::IsDischarging() {
    // 通过电压变化趋势判断是否在放电
    static float last_voltage = 0;
    float current_voltage = GetBatteryVoltage();
    bool discharging = current_voltage < last_voltage;
    last_voltage = current_voltage;
    return discharging;
}

bool Ap5056::IsPowerGood() {
    return GetBatteryVoltage() > 3.3f;
}

void Ap5056::PowerOff() {
    // 如果有控制关机的GPIO，在这里实现
    ESP_LOGI(TAG, "Power off requested");
} 