#ifndef WALLPAPERS_H
#define WALLPAPERS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 定义不同分辨率的壁纸数组
// 240x240分辨率的壁纸
extern const lv_img_dsc_t wallpaper0;
extern const lv_img_dsc_t wallpaper1;
extern const lv_img_dsc_t wallpaper2;
extern const lv_img_dsc_t wallpaper3;

// 320x240分辨率的壁纸
extern const lv_img_dsc_t wallpaper0_320x240;
extern const lv_img_dsc_t wallpaper1_320x240;
extern const lv_img_dsc_t wallpaper2_320x240;
extern const lv_img_dsc_t wallpaper3_320x240;

// 根据分辨率获取对应的壁纸数组
const lv_img_dsc_t** GetWallpapersForResolution(int width, int height);

#ifdef __cplusplus
}
#endif

#endif // WALLPAPERS_H