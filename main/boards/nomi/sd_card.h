#pragma once

#include <string>
#include <vector>
#include "driver/sdspi_host.h"
#include "driver/gpio.h"
#include "esp_vfs_fat.h"

class SdCard {
public:
    SdCard();
    ~SdCard();

    bool Initialize();
    void Unmount();
    bool IsMounted() const { return mounted_; }
    void CheckStatus();
    
    bool ReadFile(const char* filename, uint8_t** data, size_t* size);
    bool WriteFile(const char* filename, const uint8_t* data, size_t size);
    bool ListFiles(const char* dir_path, std::vector<std::string>& files);

private:
    bool mounted_;
    sdmmc_card_t* card_;
}; 