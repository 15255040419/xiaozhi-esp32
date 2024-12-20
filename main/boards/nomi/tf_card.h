#pragma once

#include <string>
#include <vector>
#include "sdmmc_cmd.h"

class TfCard {
public:
    TfCard() : mounted_(false), card_(nullptr) {}
    ~TfCard();

    bool Initialize();
    void Unmount();
    void CheckStatus();
    bool ReadFile(const char* path, uint8_t** data, size_t* size);
    bool WriteFile(const char* filename, const uint8_t* data, size_t size);
    bool ListFiles(const char* dir_path, std::vector<std::string>& files);
    bool IsMounted() const { return mounted_; }

private:
    bool mounted_;
    sdmmc_card_t* card_;
};
