#include "sdcard.h"

#include <string.h>
#include <esp_log.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#include <esp_vfs_fat.h>

static const char* TAG = "SdCard";

SdCard::SdCard() : mounted_(false) {}
SdCard::~SdCard() { Unmount(); }

esp_err_t SdCard::Mount1Bit(int clk_gpio, int cmd_gpio, int d0_gpio, const char* mount_point) {
    if (mounted_) return ESP_OK;

    mount_point_ = mount_point ? mount_point : "/sdcard";

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = (gpio_num_t)clk_gpio;
    slot_config.cmd = (gpio_num_t)cmd_gpio;
    slot_config.d0  = (gpio_num_t)d0_gpio;
    slot_config.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_card_t* card = nullptr;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point_.c_str(), &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Mount SD failed: %s", esp_err_to_name(ret));
        return ret;
    }
    sdmmc_card_print_info(stdout, card);
    mounted_ = true;
    ESP_LOGI(TAG, "SD mounted at %s", mount_point_.c_str());
    return ESP_OK;
}

void SdCard::Unmount() {
    if (!mounted_) return;
    esp_vfs_fat_sdcard_unmount(mount_point_.c_str(), nullptr);
    mounted_ = false;
    ESP_LOGI(TAG, "SD unmounted");
}
