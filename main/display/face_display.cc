#include "face_display.h"
#include <esp_log.h>
#include <math.h>

#define TAG "FaceDisplay"

#define CANVAS_WIDTH 120
#define CANVAS_HEIGHT 100

static lv_color_t canvas_buf1[LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_WIDTH, CANVAS_HEIGHT)];
static lv_color_t canvas_buf2[LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_WIDTH, CANVAS_HEIGHT)];

FaceDisplay::FaceDisplay(lv_obj_t* parent, int width, int height)
    : parent_(parent)
    , width_(width)
    , height_(height)
    , current_expression_(Expression::Normal)
    , target_expression_(Expression::Normal)
    , is_blinking_(false)
    , is_transitioning_(false)
{
    createEyes();
    current_config_ = getExpressionConfig(Expression::Normal);
    target_config_ = current_config_;
}

FaceDisplay::~FaceDisplay() {
    if (left_eye_canvas_) {
        lv_obj_del(left_eye_canvas_);
    }
    if (right_eye_canvas_) {
        lv_obj_del(right_eye_canvas_);
    }
}

void FaceDisplay::createEyes() {
    // 创建左眼画布
    left_eye_canvas_ = lv_canvas_create(parent_);
    lv_canvas_set_buffer(left_eye_canvas_, canvas_buf1, CANVAS_WIDTH, CANVAS_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(left_eye_canvas_, LV_ALIGN_CENTER, -60, 0);
    
    // 创建右眼画布
    right_eye_canvas_ = lv_canvas_create(parent_);
    lv_canvas_set_buffer(right_eye_canvas_, canvas_buf2, CANVAS_WIDTH, CANVAS_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(right_eye_canvas_, LV_ALIGN_CENTER, 60, 0);
}

void FaceDisplay::drawEye(lv_obj_t* canvas, const EyeConfig& config, bool is_left) {
    // 清空画布为黑色背景
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

    // 计算眼睛中心点
    int16_t centerX = CANVAS_WIDTH / 2;
    int16_t centerY = CANVAS_HEIGHT / 2;

    // 计算斜率引起的高度变化
    int32_t delta_y_top = config.Height * config.Slope_Top / 2.0;
    int32_t delta_y_bottom = config.Height * config.Slope_Bottom / 2.0;
    
    // 计算总高度
    auto totalHeight = config.Height + delta_y_top - delta_y_bottom;
    
    // 调整圆角半径
    auto config_copy = config;
    if (config.Radius_Bottom > 0 && config.Radius_Top > 0 && 
        totalHeight - 1 < config.Radius_Bottom + config.Radius_Top) {
        int32_t corrected_radius_top = (float)config.Radius_Top * (totalHeight - 1) / 
                                     (config.Radius_Bottom + config.Radius_Top);
        int32_t corrected_radius_bottom = (float)config.Radius_Bottom * (totalHeight - 1) / 
                                        (config.Radius_Bottom + config.Radius_Top);
        config_copy.Radius_Top = corrected_radius_top;
        config_copy.Radius_Bottom = corrected_radius_bottom;
    }

    // 计算眼睛内部角点坐标
    int32_t TLc_y = centerY + config.OffsetY - config.Height/2 + config_copy.Radius_Top - delta_y_top;
    int32_t TLc_x = centerX + config.OffsetX - config.Width/2 + config_copy.Radius_Top;
    int32_t TRc_y = centerY + config.OffsetY - config.Height/2 + config_copy.Radius_Top + delta_y_top;
    int32_t TRc_x = centerX + config.OffsetX + config.Width/2 - config_copy.Radius_Top;
    int32_t BLc_y = centerY + config.OffsetY + config.Height/2 - config_copy.Radius_Bottom - delta_y_bottom;
    int32_t BLc_x = centerX + config.OffsetX - config.Width/2 + config_copy.Radius_Bottom;
    int32_t BRc_y = centerY + config.OffsetY + config.Height/2 - config_copy.Radius_Bottom + delta_y_bottom;
    int32_t BRc_x = centerX + config.OffsetX + config.Width/2 - config_copy.Radius_Bottom;

    // 计算内部范围
    int32_t min_c_x = std::min(TLc_x, BLc_x);
    int32_t max_c_x = std::max(TRc_x, BRc_x);
    int32_t min_c_y = std::min(TLc_y, TRc_y);
    int32_t max_c_y = std::max(BLc_y, BRc_y);

    // 创建绘图描述符
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_white();  // 白色眼睛
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.radius = 0;

    // 绘制主体矩形
    lv_area_t area = {
        .x1 = static_cast<lv_coord_t>(min_c_x),
        .y1 = static_cast<lv_coord_t>(min_c_y),
        .x2 = static_cast<lv_coord_t>(max_c_x),
        .y2 = static_cast<lv_coord_t>(max_c_y)
    };
    lv_canvas_draw_rect(canvas, area.x1, area.y1, area.x2 - area.x1 + 1, area.y2 - area.y1 + 1, &rect_dsc);

    // 绘制延伸区域
    // 右边
    lv_canvas_draw_rect(canvas, TRc_x, TRc_y, 
                       BRc_x + config_copy.Radius_Bottom - TRc_x + 1,
                       BRc_y - TRc_y + 1, &rect_dsc);

    // 左边
    lv_canvas_draw_rect(canvas, TLc_x - config_copy.Radius_Top, TLc_y,
                       BLc_x - (TLc_x - config_copy.Radius_Top) + 1,
                       BLc_y - TLc_y + 1, &rect_dsc);

    // 上边
    lv_canvas_draw_rect(canvas, TLc_x, TLc_y - config_copy.Radius_Top,
                       TRc_x - TLc_x + 1,
                       TRc_y - (TLc_y - config_copy.Radius_Top) + 1, &rect_dsc);

    // 下边
    lv_canvas_draw_rect(canvas, BLc_x, BLc_y,
                       BRc_x - BLc_x + 1,
                       BRc_y + config_copy.Radius_Bottom - BLc_y + 1, &rect_dsc);

    // 绘制上部斜边
    if (config.Slope_Top > 0) {
        lv_point_t points[] = {
            {.x = static_cast<lv_coord_t>(TLc_x), .y = static_cast<lv_coord_t>(TLc_y-config_copy.Radius_Top)},
            {.x = static_cast<lv_coord_t>(TRc_x), .y = static_cast<lv_coord_t>(TRc_y-config_copy.Radius_Top)},
            {.x = static_cast<lv_coord_t>(TRc_x), .y = static_cast<lv_coord_t>(TRc_y)}
        };
        lv_canvas_draw_polygon(canvas, points, 3, &rect_dsc);
    } 
    else if (config.Slope_Top < 0) {
        lv_point_t points[] = {
            {.x = static_cast<lv_coord_t>(TRc_x), .y = static_cast<lv_coord_t>(TRc_y-config_copy.Radius_Top)},
            {.x = static_cast<lv_coord_t>(TLc_x), .y = static_cast<lv_coord_t>(TLc_y-config_copy.Radius_Top)},
            {.x = static_cast<lv_coord_t>(TLc_x), .y = static_cast<lv_coord_t>(TLc_y)}
        };
        lv_canvas_draw_polygon(canvas, points, 3, &rect_dsc);
    }

    // 绘制下部斜边
    if (config.Slope_Bottom > 0) {
        lv_point_t points[] = {
            {.x = static_cast<lv_coord_t>(BRc_x+config_copy.Radius_Bottom), .y = static_cast<lv_coord_t>(BRc_y+config_copy.Radius_Bottom)},
            {.x = static_cast<lv_coord_t>(BLc_x-config_copy.Radius_Bottom), .y = static_cast<lv_coord_t>(BLc_y+config_copy.Radius_Bottom)},
            {.x = static_cast<lv_coord_t>(BLc_x), .y = static_cast<lv_coord_t>(BLc_y)}
        };
        lv_canvas_draw_polygon(canvas, points, 3, &rect_dsc);
    }
    else if (config.Slope_Bottom < 0) {
        lv_point_t points[] = {
            {.x = static_cast<lv_coord_t>(BLc_x-config_copy.Radius_Bottom), .y = static_cast<lv_coord_t>(BLc_y+config_copy.Radius_Bottom)},
            {.x = static_cast<lv_coord_t>(BRc_x+config_copy.Radius_Bottom), .y = static_cast<lv_coord_t>(BRc_y+config_copy.Radius_Bottom)},
            {.x = static_cast<lv_coord_t>(BRc_x), .y = static_cast<lv_coord_t>(BRc_y)}
        };
        lv_canvas_draw_polygon(canvas, points, 3, &rect_dsc);
    }

    // 绘制圆角
    if (config_copy.Radius_Top > 0) {
        // 左上角
        lv_draw_arc_dsc_t arc_dsc;
        lv_draw_arc_dsc_init(&arc_dsc);
        arc_dsc.color = rect_dsc.bg_color;
        arc_dsc.width = config_copy.Radius_Top;
        arc_dsc.start_angle = 180;
        arc_dsc.end_angle = 270;
        lv_canvas_draw_arc(canvas, TLc_x, TLc_y, config_copy.Radius_Top, 180, 270, &arc_dsc);

        // 右上角
        arc_dsc.start_angle = 270;
        arc_dsc.end_angle = 360;
        lv_canvas_draw_arc(canvas, TRc_x, TRc_y, config_copy.Radius_Top, 270, 360, &arc_dsc);
    }

    if (config_copy.Radius_Bottom > 0) {
        // 左下角
        lv_draw_arc_dsc_t arc_dsc;
        lv_draw_arc_dsc_init(&arc_dsc);
        arc_dsc.color = rect_dsc.bg_color;
        arc_dsc.width = config_copy.Radius_Bottom;
        arc_dsc.start_angle = 90;
        arc_dsc.end_angle = 180;
        lv_canvas_draw_arc(canvas, BLc_x, BLc_y, config_copy.Radius_Bottom, 90, 180, &arc_dsc);

        // 右下角
        arc_dsc.start_angle = 0;
        arc_dsc.end_angle = 90;
        lv_canvas_draw_arc(canvas, BRc_x, BRc_y, config_copy.Radius_Bottom, 0, 90, &arc_dsc);
    }

    // 更新画布显示
    lv_obj_invalidate(canvas);
}

void FaceDisplay::fillRectangle(lv_draw_ctx_t& draw_ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    // 确保从左上到右下绘制
    int32_t l = std::min(x0, x1);
    int32_t r = std::max(x0, x1);
    int32_t t = std::min(y0, y1);
    int32_t b = std::max(y0, y1);
    
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    rect_dsc.bg_opa = LV_OPA_COVER;
    
    lv_area_t area;
    area.x1 = static_cast<lv_coord_t>(l);
    area.y1 = static_cast<lv_coord_t>(t);
    area.x2 = static_cast<lv_coord_t>(r);
    area.y2 = static_cast<lv_coord_t>(b);
    
    lv_draw_rect(&draw_ctx, &rect_dsc, &area);
}

void FaceDisplay::fillRectangularTriangle(lv_draw_ctx_t& draw_ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    rect_dsc.bg_opa = LV_OPA_COVER;
    
    lv_point_t points[3] = {
        {.x = static_cast<lv_coord_t>(x0), .y = static_cast<lv_coord_t>(y0)},
        {.x = static_cast<lv_coord_t>(x1), .y = static_cast<lv_coord_t>(y1)},
        {.x = static_cast<lv_coord_t>(x1), .y = static_cast<lv_coord_t>(y0)}
    };
    
    lv_draw_polygon(&draw_ctx, &rect_dsc, points, 3);
}

void FaceDisplay::fillEllipseCorner(lv_draw_ctx_t& draw_ctx, CornerType corner, 
                                  int16_t x0, int16_t y0, int32_t rx, int32_t ry) {
    if (rx < 2 || ry < 2) return;

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    rect_dsc.bg_opa = LV_OPA_COVER;

    // 使用Bresenham椭圆算法
    int32_t x, y;
    int32_t rx2 = rx * rx;
    int32_t fx2 = 4 * rx2;
    int32_t s;

    // 根据不同角落位置调整绘制方向
    for (x = 0, y = ry, s = 2 * ry * ry + rx2 * (1 - 2 * ry); ry * x <= rx * y; x++) {
        int32_t draw_x = x0;
        int32_t draw_y = y0;
        
        switch (corner) {
            case CornerType::T_R:
                draw_x += x;
                draw_y -= y;
                break;
            case CornerType::T_L:
                draw_x -= x;
                draw_y -= y;
                break;
            case CornerType::B_R:
                draw_x += x;
                draw_y += y;
                break;
            case CornerType::B_L:
                draw_x -= x;
                draw_y += y;
                break;
        }
        
        // 绘制水平线
        lv_area_t area;
        if (corner == CornerType::T_L || corner == CornerType::B_L) {
            area.x1 = draw_x;
            area.x2 = draw_x + x;
        } else {
            area.x1 = draw_x - x;
            area.x2 = draw_x;
        }
        area.y1 = draw_y;
        area.y2 = draw_y;
        
        lv_draw_rect(&draw_ctx, &rect_dsc, &area);

        if (s >= 0) {
            s += fx2 * (1 - y);
            y--;
        }
        s += 2 * ry * ry * ((4 * x) + 6);
    }
}

EyeConfig FaceDisplay::getExpressionConfig(Expression exp) {
    EyeConfig config = {
        .OffsetX = 0,
        .OffsetY = 0,
        .Height = 40,
        .Width = 40,
        .Slope_Top = 0,
        .Slope_Bottom = 0,
        .Radius_Top = 8,
        .Radius_Bottom = 8,
        .Inverse_Radius_Top = 0,
        .Inverse_Radius_Bottom = 0,
        .Inverse_Offset_Top = 0,
        .Inverse_Offset_Bottom = 0
    };

    switch (exp) {
        case Expression::Happy:
            config.Height = 10;
            config.Width = 40;
            config.Radius_Top = 10;
            config.Radius_Bottom = 0;
            break;
        case Expression::Sad:
            config.Height = 15;
            config.Width = 40;
            config.Slope_Top = -0.5f;
            config.Radius_Top = 1;
            config.Radius_Bottom = 10;
            break;
        case Expression::Angry:
            config.Height = 25;
            config.Width = 40;
            config.Slope_Top = -0.3f;
            config.Radius_Top = 5;
            config.Radius_Bottom = 5;
            break;
        case Expression::Surprised:
            config.Height = 50;
            config.Width = 50;
            config.Radius_Top = 25;
            config.Radius_Bottom = 25;
            break;
        case Expression::Sleepy:
            config.Height = 8;
            config.Width = 40;
            config.Slope_Top = 0.1f;
            break;
        case Expression::Blink:
            config.Height = 5;
            config.Width = 40;
            break;
        case Expression::Glee:
            config.Height = 8;
            config.Width = 40;
            config.Radius_Top = 8;
            config.Radius_Bottom = 0;
            config.Inverse_Radius_Bottom = 5;
            break;
        case Expression::Worried:
            config.Height = 25;
            config.Width = 40;
            config.Slope_Top = -0.1f;
            config.Radius_Top = 6;
            config.Radius_Bottom = 10;
            break;
        case Expression::Focused:
            config.Height = 35;
            config.Width = 35;
            config.Radius_Top = 17;
            config.Radius_Bottom = 17;
            break;
        case Expression::Annoyed:
            config.Height = 20;
            config.Width = 40;
            config.Slope_Top = -0.2f;
            config.Radius_Top = 3;
            config.Radius_Bottom = 8;
            break;
        case Expression::Skeptic:
            config.Height = 30;
            config.Width = 40;
            config.Slope_Top = 0.2f;
            config.Radius_Top = 8;
            config.Radius_Bottom = 8;
            break;
        case Expression::Frustrated:
            config.Height = 25;
            config.Width = 45;
            config.Slope_Top = -0.4f;
            config.Slope_Bottom = 0.1f;
            config.Radius_Top = 4;
            config.Radius_Bottom = 6;
            break;
        case Expression::Unimpressed:
            config.Height = 15;
            config.Width = 40;
            config.Slope_Bottom = 0.2f;
            config.Radius_Top = 2;
            config.Radius_Bottom = 8;
            break;
        case Expression::Suspicious:
            config.Height = 30;
            config.Width = 35;
            config.Slope_Top = -0.3f;
            config.Radius_Top = 5;
            config.Radius_Bottom = 15;
            break;
        case Expression::Squint:
            config.Height = 8;
            config.Width = 35;
            config.Slope_Top = 0.1f;
            config.Slope_Bottom = -0.1f;
            break;
        case Expression::Furious:
            config.Height = 30;
            config.Width = 45;
            config.Slope_Top = -0.5f;
            config.Radius_Top = 3;
            config.Radius_Bottom = 3;
            break;
        case Expression::Scared:
            config.Height = 45;
            config.Width = 45;
            config.Radius_Top = 22;
            config.Radius_Bottom = 22;
            break;
        case Expression::Awe:
            config.Height = 40;
            config.Width = 40;
            config.Radius_Top = 20;
            config.Radius_Bottom = 20;
            config.Inverse_Radius_Top = 5;
            break;
        case Expression::LookLeft:
            config.Height = 35;
            config.Width = 40;
            config.OffsetX = -15;  // 向左偏移
            config.Radius_Top = 8;
            config.Radius_Bottom = 8;
            break;
        case Expression::LookRight:
            config.Height = 35;
            config.Width = 40;
            config.OffsetX = 15;   // 向右偏移
            config.Radius_Top = 8;
            config.Radius_Bottom = 8;
            break;
        default:
            break;
    }
    
    return config;
}

void FaceDisplay::setExpression(Expression exp) {
    if (exp != current_expression_ && !is_blinking_) {
        startTransition(exp);
    }
}

void FaceDisplay::startTransition(Expression exp) {
    target_expression_ = exp;
    target_config_ = getExpressionConfig(exp);
    transition_start_ = lv_tick_get();
    transition_duration_ = TRANSITION_TIME_MS;
    is_transitioning_ = true;
}

void FaceDisplay::updateTransition() {
    if (!is_transitioning_) return;

    uint32_t time_elapsed = lv_tick_get() - transition_start_;
    float progress = (float)time_elapsed / transition_duration_;

    if (progress >= 1.0f) {
        current_config_ = target_config_;
        current_expression_ = target_expression_;
        is_transitioning_ = false;
        progress = 1.0f;
    }

    // 线性插值计算当前配置
    EyeConfig current;
    current.Width = current_config_.Width + (target_config_.Width - current_config_.Width) * progress;
    current.Height = current_config_.Height + (target_config_.Height - current_config_.Height) * progress;
    current.OffsetX = current_config_.OffsetX + (target_config_.OffsetX - current_config_.OffsetX) * progress;
    current.OffsetY = current_config_.OffsetY + (target_config_.OffsetY - current_config_.OffsetY) * progress;
    current.Slope_Top = current_config_.Slope_Top + (target_config_.Slope_Top - current_config_.Slope_Top) * progress;
    current.Slope_Bottom = current_config_.Slope_Bottom + (target_config_.Slope_Bottom - current_config_.Slope_Bottom) * progress;
    current.Radius_Top = current_config_.Radius_Top + (target_config_.Radius_Top - current_config_.Radius_Top) * progress;
    current.Radius_Bottom = current_config_.Radius_Bottom + (target_config_.Radius_Bottom - current_config_.Radius_Bottom) * progress;

    // 绘制当前状态
    drawEye(left_eye_canvas_, current, true);
    drawEye(right_eye_canvas_, current, false);
}

void FaceDisplay::update() {
    if (is_transitioning_) {
        updateTransition();
    } else {
        // 如果没有在过渡中，也需要绘制当前状态
        drawEye(left_eye_canvas_, current_config_, true);
        drawEye(right_eye_canvas_, current_config_, false);
    }
    
    if (is_blinking_) {
        uint32_t time_elapsed = lv_tick_get() - transition_start_;
        if (time_elapsed >= BLINK_TIME_MS * 2) {
            is_blinking_ = false;
            target_config_ = getExpressionConfig(current_expression_);
            transition_start_ = lv_tick_get();
            transition_duration_ = BLINK_TIME_MS;
        }
    }
}

void FaceDisplay::doBlink() {
    if (!is_blinking_ && !is_transitioning_) {
        is_blinking_ = true;
        EyeConfig blink_config = getExpressionConfig(Expression::Blink);
        target_config_ = blink_config;
        transition_start_ = lv_tick_get();
        transition_duration_ = BLINK_TIME_MS;
    }
}
