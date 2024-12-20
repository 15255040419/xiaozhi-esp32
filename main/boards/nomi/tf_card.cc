#include "tf_card.h"
#include "config.h"
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <driver/sdspi_host.h>
#include <sys/stat.h>
#include <dirent.h>

static const char* TAG = "TfCard";

bool TfCard::Initialize() {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = TF_CS_PIN;
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);

    esp_err_t ret = esp_vfs_fat_sdspi_mount(TF_MOUNT_POINT, &host, &slot_config, &mount_config, &card_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount TF card (%s)", esp_err_to_name(ret));
        return false;
    }

    mounted_ = true;
    ESP_LOGI(TAG, "TF card mounted");
    return true;
}

void TfCard::Unmount() {
    if (mounted_) {
        esp_vfs_fat_sdcard_unmount(TF_MOUNT_POINT, card_);
        mounted_ = false;
        card_ = nullptr;
    }
}

TfCard::~TfCard() {
    Unmount();
}

void TfCard::CheckStatus() {
    bool card_present = true;
    if (TF_DETECT_PIN != GPIO_NUM_NC) {
        card_present = (gpio_get_level(TF_DETECT_PIN) == 0);
    }

    if (card_present && !mounted_) {
        if (Initialize()) {
            ESP_LOGI(TAG, "TF card inserted and mounted");
        }
    } else if (!card_present && mounted_) {
        Unmount();
        ESP_LOGI(TAG, "TF card removed");
    }
}

bool TfCard::ReadFile(const char* path, uint8_t** data, size_t* size) {
    if (!mounted_) {
        ESP_LOGE(TAG, "TF card not mounted");
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", TF_MOUNT_POINT, path);

    FILE* f = fopen(full_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file : %s", full_path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);

    *data = (uint8_t*)malloc(*size);
    if (*data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for file : %s", full_path);
        fclose(f);
        return false;
    }

    if (fread(*data, 1, *size, f) != *size) {
        ESP_LOGE(TAG, "Failed to read file : %s", full_path);
        free(*data);
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
}

bool TfCard::WriteFile(const char* path, const uint8_t* data, size_t size) {
    if (!mounted_) {
        ESP_LOGE(TAG, "TF card not mounted");
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", TF_MOUNT_POINT, path);

    FILE* f = fopen(full_path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing : %s", full_path);
        return false;
    }

    if (fwrite(data, 1, size, f) != size) {
        ESP_LOGE(TAG, "Failed to write file : %s", full_path);
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
}

bool TfCard::ListFiles(const char* dir_path, std::vector<std::string>& files) {
    if (!mounted_) {
        ESP_LOGE(TAG, "TF card not mounted");
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", TF_MOUNT_POINT, dir_path);

    DIR* dir = opendir(full_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory : %s", full_path);
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        files.push_back(entry->d_name);
    }
    closedir(dir);
    return true;
}
