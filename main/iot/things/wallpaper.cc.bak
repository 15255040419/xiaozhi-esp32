#include "iot/thing.h"
#include "board.h"
#include "display/lcd_display.h"
#include <esp_log.h>

#define TAG "Wallpaper"

namespace iot {

class Wallpaper : public Thing {
public:
    Wallpaper() : Thing("Wallpaper", "控制壁纸切换的设备") {
        // 定义方法：NextWallpaper（切换到下一个壁纸）
        methods_.AddMethod("NextWallpaper", "切换到下一个壁纸", ParameterList(), [this](const ParameterList& parameters) {
            auto display = Board::GetInstance().GetDisplay();
            if (display) {
                display->ChangeWallpaper("next");
                display->ShowNotification("下一张壁纸");
            }
        });

        // 定义方法：PreviousWallpaper（切换到上一个壁纸）
        methods_.AddMethod("PreviousWallpaper", "切换到上一个壁纸", ParameterList(), [this](const ParameterList& parameters) {
            auto display = Board::GetInstance().GetDisplay();
            if (display) {
                display->ChangeWallpaper("previous");
                display->ShowNotification("上一张壁纸");
            }
        });
    }
};

} // namespace iot

DECLARE_THING(Wallpaper);