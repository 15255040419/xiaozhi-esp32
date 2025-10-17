#pragma once

#include <string>
#include <esp_err.h>

class SdCard {
public:
    SdCard();
    ~SdCard();

    esp_err_t Mount1Bit(int clk_gpio, int cmd_gpio, int d0_gpio, const char* mount_point = "/sdcard");
    void Unmount();

    bool mounted() const { return mounted_; }
    const std::string& mount_point() const { return mount_point_; }

private:
    bool mounted_;
    std::string mount_point_;
};
