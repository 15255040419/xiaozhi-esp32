#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 由 C++ 核心实现提供
// 前向声明需要在C环境下可见的 lv_obj_t
void* PixelThinkingClock_Create(void* parent, int width, int height);
void PixelThinkingClock_Show(void* ctx);
void PixelThinkingClock_Hide(void* ctx);
bool PixelThinkingClock_LoadBackground(void* ctx, const char* asset_name);
void PixelThinkingClock_SetLoop(void* ctx, int loop_mode);

// C 包装器（保持名称简单）
void* clock_face_create(void* parent, int width, int height) {
    return PixelThinkingClock_Create(parent, width, height);
}

void clock_face_show(void* ctx) {
    PixelThinkingClock_Show(ctx);
}

void clock_face_hide(void* ctx) {
    PixelThinkingClock_Hide(ctx);
}

bool clock_face_load_background(void* ctx, const char* asset_name) {
    return PixelThinkingClock_LoadBackground(ctx, asset_name);
}

void clock_face_set_loop(void* ctx, int loop_mode) {
    PixelThinkingClock_SetLoop(ctx, loop_mode);
}

#ifdef __cplusplus
}
#endif


