#include "wallpapers.h"

#include "lvgl.h"
#include <string.h>

// 获取对应分辨率的壁纸数组
const lv_img_dsc_t** GetWallpapersForResolution(int width, int height) {
    if (width == 320 && height == 240) {
        // 如果�?20x240分辨率，使用320x240的壁�?
        static const lv_img_dsc_t* wallpapers_320x240[] = {
            &wallpaper0_320x240,
            &wallpaper1_320x240,
            &wallpaper2_320x240,
            &wallpaper3_320x240,
        };
        return wallpapers_320x240;
    } else {
        // 其他分辨率默认使�?40x240的壁�?
        static const lv_img_dsc_t* wallpapers_240x240[] = {
            &wallpaper0,
            &wallpaper1,
            &wallpaper2,
            &wallpaper3,
        };
        return wallpapers_240x240;
    }
}
