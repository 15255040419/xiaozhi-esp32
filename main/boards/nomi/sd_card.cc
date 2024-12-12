#include "sd_card.h"
#include "config.h"
#include <esp_log.h>
#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>
#include <dirent.h>

#define TAG "SdCard"

SdCard::SdCard() : mounted_(false), card_(nullptr) {
}

SdCard::~SdCard() {
    Unmount();
}

bool SdCard::Initialize() {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 可以尝试提高到 SDMMC_FREQ_HIGHSPEED

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_PIN,
        .miso_io_num = SD_MISO_PIN,
        .sclk_io_num = SD_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    
    if (spi_bus_initialize(static_cast<spi_host_device_t>(host.slot), &bus_cfg, SDSPI_DEFAULT_DMA) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus");
        return false;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = GPIO_NUM_NC;  // 使用内部CS控制
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);

    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        return false;
    }

    mounted_ = true;
    ESP_LOGI(TAG, "SD card mounted");
    return true;
}

void SdCard::Unmount() {
    if (mounted_) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card_);
        mounted_ = false;
        card_ = nullptr;
    }
}

void SdCard::CheckStatus() {
    bool card_present = true;
    if (SD_DETECT_PIN != GPIO_NUM_NC) {
        card_present = (gpio_get_level(SD_DETECT_PIN) == 0);
    }

    if (card_present && !mounted_) {
        if (Initialize()) {
            ESP_LOGI(TAG, "SD card inserted and mounted");
        }
    } else if (!card_present && mounted_) {
        Unmount();
        ESP_LOGI(TAG, "SD card removed");
    }
}

bool SdCard::ReadFile(const char* path, uint8_t** data, size_t* size) {
    if (!mounted_) {
        ESP_LOGE(TAG, "SD card not mounted");
        return false;
    }

    char full_path[64];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, path);

    FILE* f = fopen(full_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s", full_path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);

    *data = (uint8_t*)malloc(*size);
    if (*data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        fclose(f);
        return false;
    }

    if (fread(*data, 1, *size, f) != *size) {
        ESP_LOGE(TAG, "Failed to read file");
        free(*data);
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
}

bool SdCard::WriteFile(const char* path, const uint8_t* data, size_t size) {
    if (!mounted_) {
        ESP_LOGE(TAG, "SD card not mounted");
        return false;
    }

    char full_path[64];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, path);

    FILE* f = fopen(full_path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing %s", full_path);
        return false;
    }

    bool success = (fwrite(data, 1, size, f) == size);
    fclose(f);

    if (!success) {
        ESP_LOGE(TAG, "Failed to write file");
        return false;
    }

    ESP_LOGI(TAG, "File written successfully: %s", path);
    return true;
}

bool SdCard::ListFiles(const char* dir_path, std::vector<std::string>& files) {
    if (!mounted_) {
        ESP_LOGE(TAG, "SD card not mounted");
        return false;
    }

    char full_path[64];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, dir_path);

    DIR* dir = opendir(full_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory %s", full_path);
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            files.push_back(entry->d_name);
        }
    }
    closedir(dir);
    return true;
} 