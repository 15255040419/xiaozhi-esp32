#pragma once

#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc/adc_oneshot.h"

class Ap5056 {
public:
    Ap5056(gpio_num_t vbat_pin);
    ~Ap5056();
    
    bool Initialize();
    bool IsCharging();
    bool IsDischarging();
    bool IsPowerGood();
    float GetBatteryVoltage();
    int GetBatteryLevel();
    void PowerOff();

private:
    gpio_num_t vbat_pin_;
    adc_oneshot_unit_handle_t adc_handle_;
    adc_channel_t adc_channel_;
    
    int ReadVoltage();
};