#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 由 C++ 核心实现提供（改为通用名称）
void* ClockFace_Create(void* parent, int width, int height);
void ClockFace_Show(void* ctx);
void ClockFace_Hide(void* ctx);
bool ClockFace_LoadBackground(void* ctx, const char* asset_name);
void ClockFace_SetLoop(void* ctx, int loop_mode);

// C 包装器（保持名称简单）
void* clock_face_create(void* parent, int width, int height) {
    return ClockFace_Create(parent, width, height);
}

void clock_face_show(void* ctx) {
    ClockFace_Show(ctx);
}

void clock_face_hide(void* ctx) {
    ClockFace_Hide(ctx);
}

bool clock_face_load_background(void* ctx, const char* asset_name) {
    return ClockFace_LoadBackground(ctx, asset_name);
}

void clock_face_set_loop(void* ctx, int loop_mode) {
    ClockFace_SetLoop(ctx, loop_mode);
}

#ifdef __cplusplus
}
#endif


