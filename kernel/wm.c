#include "wm.h"

#include <stdint.h>

#include "console.h"
#include "fb.h"
#include "mouse.h"
#include "pit.h"
#include "serial.h"
#include "vbe.h"

#define WM_MAX_WINDOWS          8U
#define WM_TITLE_MAX            31U

#define WM_BORDER_SIZE          2U
#define WM_TITLEBAR_HEIGHT      18U
#define WM_MIN_WINDOW_W         120U
#define WM_MIN_WINDOW_H         80U
#define WM_TASKBAR_HEIGHT       28U

#define WM_DESKTOP_COLOR        0x1B263BU
#define WM_BORDER_COLOR         0x1E1E1EU
#define WM_TITLE_TEXT_COLOR     0xFFFFFFU
#define WM_CURSOR_OUTLINE       0x000000U
#define WM_CURSOR_FILL          0xFFFFFFU
#define WM_CURSOR_PRESSED       0xFFAA00U

#define WM_APP_MARGIN           4
#define WM_I32_MAX              2147483647
#define WM_I32_MIN              (-2147483647 - 1)
#define WM_CALC_BUTTONS         16U
#define WM_CALC_ROWS            4U
#define WM_CALC_COLS            4U
#define WM_CHECKLIST_ITEMS      5U
#define WM_TERM_HISTORY_LINES   128U
#define WM_TERM_LINE_MAX        96U
#define WM_TERM_INPUT_MAX       96U
#define WM_DOCK_BUTTONS         4U
#define WM_TERMINAL_PROMPT      "claudeos> "

enum wm_event_type {
    WM_EVENT_MOUSE_MOVE = 0,
    WM_EVENT_MOUSE_DOWN = 1,
    WM_EVENT_MOUSE_UP = 2,
    WM_EVENT_FOCUS = 3,
    WM_EVENT_DRAG_START = 4,
    WM_EVENT_DRAG_MOVE = 5,
    WM_EVENT_DRAG_END = 6,
};

struct wm_event {
    enum wm_event_type type;
    int32_t mouse_x;
    int32_t mouse_y;
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
};

struct wm_rect {
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
};

enum wm_app_kind {
    WM_APP_NONE = 0,
    WM_APP_CALCULATOR = 1,
    WM_APP_UPTIME = 2,
    WM_APP_CHECKLIST = 3,
    WM_APP_TERMINAL = 4,
};

enum wm_dock_button {
    WM_DOCK_TERMINAL = 0,
    WM_DOCK_CALCULATOR = 1,
    WM_DOCK_UPTIME = 2,
    WM_DOCK_CHECKLIST = 3,
};

struct wm_calculator_state {
    int32_t accumulator;
    int32_t input_value;
    uint8_t has_accumulator;
    uint8_t has_input;
    char pending_op;
    uint8_t error;
    int8_t pressed_button;
    char display[16];
};

struct wm_uptime_state {
    uint8_t stopwatch_running;
    uint32_t stopwatch_started_tick;
    uint32_t stopwatch_elapsed_ticks;
    int8_t pressed_button;
};

struct wm_checklist_state {
    uint8_t checked[WM_CHECKLIST_ITEMS];
    int8_t pressed_item;
};

struct wm_terminal_state {
    char lines[WM_TERM_HISTORY_LINES][WM_TERM_LINE_MAX];
    uint16_t line_head;
    uint16_t line_count;
    char input[WM_TERM_INPUT_MAX];
    uint16_t input_len;
};

union wm_app_state {
    struct wm_calculator_state calculator;
    struct wm_uptime_state uptime;
    struct wm_checklist_state checklist;
    struct wm_terminal_state terminal;
};

struct wm_window;
typedef void (*wm_window_event_fn)(struct wm_window *window, const struct wm_event *event);
typedef void (*wm_window_draw_fn)(const struct wm_window *window);

struct wm_window {
    uint8_t in_use;
    uint8_t focused;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t body_color_a;
    uint32_t body_color_b;
    uint32_t body_color;
    uint32_t title_color_active;
    uint32_t title_color_inactive;
    uint32_t title_color;
    uint32_t click_count;
    char title[WM_TITLE_MAX + 1U];
    wm_window_event_fn on_event;
    wm_window_draw_fn on_draw;
    enum wm_app_kind app_kind;
    union wm_app_state app;
};

struct wm_state {
    uint8_t ready;
    uint8_t active;
    uint8_t needs_redraw;
    uint8_t window_count;
    uint8_t z_order[WM_MAX_WINDOWS];
    struct wm_window windows[WM_MAX_WINDOWS];
    int32_t screen_w;
    int32_t screen_h;
    int32_t mouse_x;
    int32_t mouse_y;
    uint8_t mouse_buttons;
    int8_t drag_window;
    int32_t drag_off_x;
    int32_t drag_off_y;
    uint32_t last_animated_tick;
};

static struct wm_state g_wm;
static const char g_calc_labels[WM_CALC_BUTTONS] = {
    '7', '8', '9', '/',
    '4', '5', '6', '*',
    '1', '2', '3', '-',
    'C', '0', '=', '+',
};
static const char *const g_checklist_labels[WM_CHECKLIST_ITEMS] = {
    "BOOT CHECKS",
    "MOUNT FILESYSTEMS",
    "RUN CALCULATOR",
    "VERIFY UPTIME",
    "OPEN TERMINAL",
};
static const char *const g_dock_labels[WM_DOCK_BUTTONS] = {
    "TERMINAL",
    "CALC",
    "UPTIME",
    "CHECK",
};
static const enum wm_app_kind g_dock_apps[WM_DOCK_BUTTONS] = {
    WM_APP_TERMINAL,
    WM_APP_CALCULATOR,
    WM_APP_UPTIME,
    WM_APP_CHECKLIST,
};
static int32_t g_terminal_window_index = -1;

static int32_t wm_clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint32_t wm_dim_color(uint32_t rgb, uint8_t percent)
{
    uint32_t r = (rgb >> 16) & 0xFFU;
    uint32_t g = (rgb >> 8) & 0xFFU;
    uint32_t b = rgb & 0xFFU;

    r = (r * percent) / 100U;
    g = (g * percent) / 100U;
    b = (b * percent) / 100U;

    return (r << 16) | (g << 8) | b;
}

static void serial_put_dec(uint32_t value)
{
    char buf[11];
    uint32_t idx = 0U;

    if (value == 0U) {
        serial_putchar('0');
        return;
    }

    while (value > 0U && idx < sizeof(buf)) {
        buf[idx++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (idx > 0U) {
        serial_putchar(buf[--idx]);
    }
}

static uint32_t wm_strnlen(const char *text, uint32_t max_len)
{
    uint32_t len = 0U;

    if (text == 0) {
        return 0U;
    }

    while (len < max_len && text[len] != '\0') {
        len++;
    }

    return len;
}

static void wm_copy_string(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t i = 0U;

    if (dst == 0 || src == 0 || dst_size == 0U) {
        return;
    }

    while (i + 1U < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static uint32_t wm_u32_to_string(char *dst, uint32_t dst_size, uint32_t value)
{
    char buf[11];
    uint32_t idx = 0U;
    uint32_t out = 0U;

    if (dst == 0 || dst_size == 0U) {
        return 0U;
    }

    if (value == 0U) {
        dst[0] = '0';
        if (dst_size > 1U) {
            dst[1] = '\0';
            return 1U;
        }

        return 0U;
    }

    while (value > 0U && idx < sizeof(buf)) {
        buf[idx++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (idx > 0U && out + 1U < dst_size) {
        dst[out++] = buf[--idx];
    }

    dst[out] = '\0';
    return out;
}

static uint32_t wm_i32_to_string(char *dst, uint32_t dst_size, int32_t value)
{
    uint32_t offset = 0U;
    uint32_t magnitude;

    if (dst == 0 || dst_size == 0U) {
        return 0U;
    }

    if (value < 0) {
        if (dst_size < 2U) {
            dst[0] = '\0';
            return 0U;
        }

        dst[0] = '-';
        offset = 1U;
        magnitude = (uint32_t)(-(int64_t)value);
    } else {
        magnitude = (uint32_t)value;
    }

    return offset + wm_u32_to_string(dst + offset, dst_size - offset, magnitude);
}

static int wm_rect_contains(const struct wm_rect *rect, int32_t x, int32_t y)
{
    if (rect == 0) {
        return 0;
    }

    if (x < rect->x || y < rect->y) {
        return 0;
    }

    if (x >= (rect->x + (int32_t)rect->w) || y >= (rect->y + (int32_t)rect->h)) {
        return 0;
    }

    return 1;
}

static int32_t wm_workspace_bottom(void)
{
    int32_t bottom = g_wm.screen_h - (int32_t)WM_TASKBAR_HEIGHT;

    if (bottom < 0) {
        bottom = 0;
    }

    return bottom;
}

static int wm_window_content_rect(const struct wm_window *window, struct wm_rect *out_rect)
{
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;

    if (window == 0 || out_rect == 0 || window->in_use == 0U) {
        return 0;
    }

    if (window->width <= (2U * WM_BORDER_SIZE) || window->height <= (2U * WM_BORDER_SIZE)) {
        return 0;
    }

    x = window->x + (int32_t)WM_BORDER_SIZE + WM_APP_MARGIN;
    y = window->y + (int32_t)WM_BORDER_SIZE + (int32_t)WM_TITLEBAR_HEIGHT + WM_APP_MARGIN;
    w = (int32_t)window->width - (int32_t)(2U * WM_BORDER_SIZE) - (2 * WM_APP_MARGIN);
    h = (int32_t)window->height - (int32_t)(2U * WM_BORDER_SIZE) - (int32_t)WM_TITLEBAR_HEIGHT - (2 * WM_APP_MARGIN);

    if (w <= 0 || h <= 0) {
        return 0;
    }

    out_rect->x = x;
    out_rect->y = y;
    out_rect->w = (uint32_t)w;
    out_rect->h = (uint32_t)h;
    return 1;
}

static void wm_format_hms(uint32_t total_seconds, char *dst, uint32_t dst_size)
{
    uint32_t hours = total_seconds / 3600U;
    uint32_t minutes = (total_seconds / 60U) % 60U;
    uint32_t seconds = total_seconds % 60U;
    uint32_t pos = 0U;

    if (dst == 0 || dst_size == 0U) {
        return;
    }

    pos = wm_u32_to_string(dst, dst_size, hours);
    if (pos + 6U >= dst_size) {
        return;
    }

    dst[pos++] = ':';
    dst[pos++] = (char)('0' + ((minutes / 10U) % 10U));
    dst[pos++] = (char)('0' + (minutes % 10U));
    dst[pos++] = ':';
    dst[pos++] = (char)('0' + ((seconds / 10U) % 10U));
    dst[pos++] = (char)('0' + (seconds % 10U));
    dst[pos] = '\0';
}

static void wm_fill_rect_clamped(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t rgb)
{
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (w == 0U || h == 0U) {
        return;
    }

    x0 = x;
    y0 = y;
    x1 = x + (int32_t)w;
    y1 = y + (int32_t)h;

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > g_wm.screen_w) {
        x1 = g_wm.screen_w;
    }
    if (y1 > g_wm.screen_h) {
        y1 = g_wm.screen_h;
    }

    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    fb_fill_rect((uint32_t)x0, (uint32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0), rgb);
}

static void wm_draw_text_right(const struct wm_rect *rect, const char *text, uint32_t fg, uint32_t bg)
{
    uint32_t len;
    uint32_t text_w;
    int32_t draw_x;
    int32_t draw_y;

    if (rect == 0 || text == 0) {
        return;
    }

    len = wm_strnlen(text, 31U);
    text_w = len * 8U;
    draw_x = rect->x + 2;
    draw_y = rect->y + 2;

    if (text_w + 4U < rect->w) {
        draw_x = rect->x + (int32_t)rect->w - (int32_t)text_w - 2;
    }
    if (rect->h > 16U) {
        draw_y = rect->y + ((int32_t)rect->h - 16) / 2;
    }

    fb_draw_text((uint32_t)draw_x, (uint32_t)draw_y, text, fg, bg);
}

static void wm_clear_state(void)
{
    g_wm.active = 0U;
    g_wm.needs_redraw = 0U;
    g_wm.window_count = 0U;
    g_wm.mouse_x = 0;
    g_wm.mouse_y = 0;
    g_wm.mouse_buttons = 0U;
    g_wm.drag_window = -1;
    g_wm.drag_off_x = 0;
    g_wm.drag_off_y = 0;
    g_wm.last_animated_tick = 0U;
    g_terminal_window_index = -1;

    for (uint32_t i = 0U; i < WM_MAX_WINDOWS; i++) {
        g_wm.z_order[i] = 0U;
        g_wm.windows[i].in_use = 0U;
    }
}

static void wm_copy_title(char *dst, const char *src)
{
    wm_copy_string(dst, WM_TITLE_MAX + 1U, src);
}

static void wm_default_event_handler(struct wm_window *window, const struct wm_event *event)
{
    if (window == 0 || event == 0) {
        return;
    }

    if (event->type == WM_EVENT_FOCUS) {
        window->title_color = window->title_color_active;
        return;
    }

    if (event->type == WM_EVENT_MOUSE_DOWN) {
        window->click_count++;
        if ((window->click_count & 1U) != 0U) {
            window->body_color = window->body_color_b;
        } else {
            window->body_color = window->body_color_a;
        }
    }
}

static int wm_alloc_window_slot(void)
{
    for (uint32_t i = 0U; i < WM_MAX_WINDOWS; i++) {
        if (g_wm.windows[i].in_use == 0U) {
            return (int)i;
        }
    }
    return -1;
}

static void wm_clamp_window_position(struct wm_window *window)
{
    int32_t max_x;
    int32_t max_y;

    if (window == 0) {
        return;
    }

    max_x = g_wm.screen_w - (int32_t)window->width;
    max_y = wm_workspace_bottom() - (int32_t)window->height;

    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }

    window->x = wm_clamp_i32(window->x, 0, max_x);
    window->y = wm_clamp_i32(window->y, 0, max_y);
}

static int wm_create_window(const char *title,
                            int32_t x, int32_t y,
                            uint32_t width, uint32_t height,
                            uint32_t body_color,
                            uint32_t title_color)
{
    int slot;
    struct wm_window *window;
    uint32_t inactive_title;

    if (g_wm.window_count >= WM_MAX_WINDOWS) {
        return -1;
    }

    slot = wm_alloc_window_slot();
    if (slot < 0) {
        return -1;
    }

    if (width < WM_MIN_WINDOW_W) {
        width = WM_MIN_WINDOW_W;
    }
    if (height < WM_MIN_WINDOW_H) {
        height = WM_MIN_WINDOW_H;
    }

    if (width > (uint32_t)g_wm.screen_w) {
        width = (uint32_t)g_wm.screen_w;
    }
    if (height > (uint32_t)g_wm.screen_h) {
        height = (uint32_t)g_wm.screen_h;
    }

    inactive_title = wm_dim_color(title_color, 60U);
    window = &g_wm.windows[(uint32_t)slot];
    window->in_use = 1U;
    window->focused = 0U;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->body_color_a = body_color;
    window->body_color_b = wm_dim_color(body_color, 85U);
    window->body_color = window->body_color_a;
    window->title_color_active = title_color;
    window->title_color_inactive = inactive_title;
    window->title_color = inactive_title;
    window->click_count = 0U;
    wm_copy_title(window->title, title);
    window->on_event = wm_default_event_handler;
    window->on_draw = 0;
    window->app_kind = WM_APP_NONE;

    wm_clamp_window_position(window);

    g_wm.z_order[g_wm.window_count] = (uint8_t)slot;
    g_wm.window_count++;
    return slot;
}

static int wm_calculator_eval(int32_t lhs, int32_t rhs, char op, int32_t *out_value)
{
    int64_t result = 0;

    if (out_value == 0) {
        return 0;
    }

    if (op == '\0') {
        *out_value = rhs;
        return 1;
    }

    if (op == '+') {
        result = (int64_t)lhs + (int64_t)rhs;
    } else if (op == '-') {
        result = (int64_t)lhs - (int64_t)rhs;
    } else if (op == '*') {
        result = (int64_t)lhs * (int64_t)rhs;
    } else if (op == '/') {
        if (rhs == 0) {
            return 0;
        }
        result = (int64_t)(lhs / rhs);
    } else {
        *out_value = rhs;
        return 1;
    }

    if (result > WM_I32_MAX || result < WM_I32_MIN) {
        return 0;
    }

    *out_value = (int32_t)result;
    return 1;
}

static void wm_calc_refresh_display(struct wm_calculator_state *calc)
{
    if (calc == 0) {
        return;
    }

    if (calc->error != 0U) {
        wm_copy_string(calc->display, sizeof(calc->display), "ERR");
        return;
    }

    if (calc->has_input != 0U) {
        (void)wm_i32_to_string(calc->display, sizeof(calc->display), calc->input_value);
        return;
    }

    if (calc->has_accumulator != 0U) {
        (void)wm_i32_to_string(calc->display, sizeof(calc->display), calc->accumulator);
        return;
    }

    wm_copy_string(calc->display, sizeof(calc->display), "0");
}

static void wm_calc_reset(struct wm_calculator_state *calc)
{
    if (calc == 0) {
        return;
    }

    calc->accumulator = 0;
    calc->input_value = 0;
    calc->has_accumulator = 0U;
    calc->has_input = 0U;
    calc->pending_op = '\0';
    calc->error = 0U;
    calc->pressed_button = -1;
    wm_copy_string(calc->display, sizeof(calc->display), "0");
}

static void wm_calc_set_error(struct wm_calculator_state *calc)
{
    if (calc == 0) {
        return;
    }

    calc->error = 1U;
    calc->has_input = 0U;
    calc->pending_op = '\0';
    wm_copy_string(calc->display, sizeof(calc->display), "ERR");
}

static void wm_calc_append_digit(struct wm_calculator_state *calc, uint8_t digit)
{
    int64_t next;

    if (calc == 0) {
        return;
    }

    if (calc->error != 0U) {
        wm_calc_reset(calc);
    }

    if (calc->has_input == 0U) {
        calc->input_value = (int32_t)digit;
        calc->has_input = 1U;
        wm_calc_refresh_display(calc);
        return;
    }

    next = ((int64_t)calc->input_value * 10) + (int64_t)digit;
    if (next > WM_I32_MAX) {
        wm_calc_set_error(calc);
        return;
    }

    calc->input_value = (int32_t)next;
    wm_calc_refresh_display(calc);
}

static void wm_calc_apply_operator(struct wm_calculator_state *calc, char op)
{
    int32_t result;

    if (calc == 0) {
        return;
    }

    if (calc->error != 0U) {
        wm_calc_reset(calc);
    }

    if (calc->has_input != 0U) {
        if (calc->has_accumulator == 0U) {
            calc->accumulator = calc->input_value;
            calc->has_accumulator = 1U;
        } else {
            if (wm_calculator_eval(calc->accumulator, calc->input_value, calc->pending_op, &result) == 0) {
                wm_calc_set_error(calc);
                return;
            }
            calc->accumulator = result;
        }

        calc->has_input = 0U;
        calc->input_value = 0;
    } else if (calc->has_accumulator == 0U) {
        calc->accumulator = 0;
        calc->has_accumulator = 1U;
    }

    calc->pending_op = op;
    wm_calc_refresh_display(calc);
}

static void wm_calc_apply_equals(struct wm_calculator_state *calc)
{
    int32_t rhs;
    int32_t result;

    if (calc == 0) {
        return;
    }

    if (calc->error != 0U) {
        return;
    }

    if (calc->pending_op == '\0') {
        if (calc->has_input != 0U) {
            calc->accumulator = calc->input_value;
            calc->has_accumulator = 1U;
            calc->has_input = 0U;
            calc->input_value = 0;
        }

        wm_calc_refresh_display(calc);
        return;
    }

    if (calc->has_accumulator == 0U) {
        calc->accumulator = 0;
        calc->has_accumulator = 1U;
    }

    if (calc->has_input != 0U) {
        rhs = calc->input_value;
    } else {
        rhs = calc->accumulator;
    }

    if (wm_calculator_eval(calc->accumulator, rhs, calc->pending_op, &result) == 0) {
        wm_calc_set_error(calc);
        return;
    }

    calc->accumulator = result;
    calc->has_accumulator = 1U;
    calc->has_input = 0U;
    calc->input_value = 0;
    calc->pending_op = '\0';
    wm_calc_refresh_display(calc);
}

static void wm_calc_handle_button(struct wm_calculator_state *calc, char label)
{
    if (label >= '0' && label <= '9') {
        wm_calc_append_digit(calc, (uint8_t)(label - '0'));
        return;
    }

    if (label == 'C') {
        wm_calc_reset(calc);
        return;
    }

    if (label == '=') {
        wm_calc_apply_equals(calc);
        return;
    }

    wm_calc_apply_operator(calc, label);
}

static int wm_calc_button_rect(const struct wm_window *window, uint32_t button_index, struct wm_rect *out_rect)
{
    struct wm_rect content;
    int32_t grid_x;
    int32_t grid_y;
    int32_t grid_w;
    int32_t grid_h;
    int32_t cell_w;
    int32_t cell_h;
    uint32_t col;
    uint32_t row;

    if (window == 0 || out_rect == 0 || button_index >= WM_CALC_BUTTONS) {
        return 0;
    }

    if (wm_window_content_rect(window, &content) == 0) {
        return 0;
    }

    grid_x = content.x + 3;
    grid_y = content.y + 43;
    grid_w = (int32_t)content.w - 6;
    grid_h = (int32_t)content.h - 46;

    if (grid_w < 40 || grid_h < 40) {
        return 0;
    }

    cell_w = grid_w / (int32_t)WM_CALC_COLS;
    cell_h = grid_h / (int32_t)WM_CALC_ROWS;
    if (cell_w < 10 || cell_h < 10) {
        return 0;
    }

    col = button_index % WM_CALC_COLS;
    row = button_index / WM_CALC_COLS;

    out_rect->x = grid_x + (int32_t)col * cell_w + 2;
    out_rect->y = grid_y + (int32_t)row * cell_h + 2;
    out_rect->w = (uint32_t)((cell_w > 4) ? (cell_w - 4) : cell_w);
    out_rect->h = (uint32_t)((cell_h > 4) ? (cell_h - 4) : cell_h);
    return 1;
}

static int32_t wm_calc_hit_button(const struct wm_window *window, int32_t mouse_x, int32_t mouse_y)
{
    struct wm_rect button_rect;

    for (uint32_t i = 0U; i < WM_CALC_BUTTONS; i++) {
        if (wm_calc_button_rect(window, i, &button_rect) == 0) {
            continue;
        }

        if (wm_rect_contains(&button_rect, mouse_x, mouse_y) != 0) {
            return (int32_t)i;
        }
    }

    return -1;
}

static void wm_calculator_draw(const struct wm_window *window)
{
    struct wm_rect content;
    struct wm_rect display_rect;
    const struct wm_calculator_state *calc;
    char op_text[2];

    if (window == 0 || window->app_kind != WM_APP_CALCULATOR) {
        return;
    }

    calc = &window->app.calculator;
    if (wm_window_content_rect(window, &content) == 0) {
        return;
    }

    wm_fill_rect_clamped(content.x, content.y, content.w, content.h, 0x0B1324U);

    display_rect.x = content.x + 3;
    display_rect.y = content.y + 3;
    display_rect.w = (content.w > 6U) ? (content.w - 6U) : content.w;
    display_rect.h = 34U;
    wm_fill_rect_clamped(display_rect.x, display_rect.y, display_rect.w, display_rect.h, 0x111827U);
    wm_fill_rect_clamped(display_rect.x, display_rect.y, display_rect.w, 1U, 0x1F2937U);
    wm_fill_rect_clamped(display_rect.x, display_rect.y + (int32_t)display_rect.h - 1, display_rect.w, 1U, 0x000000U);

    if (calc->pending_op != '\0' && calc->error == 0U) {
        op_text[0] = calc->pending_op;
        op_text[1] = '\0';
        fb_draw_text((uint32_t)(display_rect.x + 4), (uint32_t)(display_rect.y + 9), op_text, 0x9CA3AFU, 0x111827U);
    }

    wm_draw_text_right(&display_rect, calc->display, 0xE5E7EBU, 0x111827U);

    for (uint32_t i = 0U; i < WM_CALC_BUTTONS; i++) {
        struct wm_rect button_rect;
        char label[2];
        uint32_t color;
        uint32_t text_color = 0xFFFFFFU;
        int32_t text_x;
        int32_t text_y;

        if (wm_calc_button_rect(window, i, &button_rect) == 0) {
            continue;
        }

        label[0] = g_calc_labels[i];
        label[1] = '\0';

        if (label[0] == 'C') {
            color = 0x991B1BU;
        } else if (label[0] == '+' || label[0] == '-' || label[0] == '*' || label[0] == '/' || label[0] == '=') {
            color = 0x1D4ED8U;
        } else {
            color = 0x334155U;
        }

        if ((int32_t)i == calc->pressed_button) {
            color = wm_dim_color(color, 140U);
            text_color = 0xF8FAFCU;
        }

        wm_fill_rect_clamped(button_rect.x, button_rect.y, button_rect.w, button_rect.h, color);
        wm_fill_rect_clamped(button_rect.x, button_rect.y, button_rect.w, 1U, wm_dim_color(color, 70U));

        text_x = button_rect.x + ((int32_t)button_rect.w - 8) / 2;
        text_y = button_rect.y + ((int32_t)button_rect.h - 16) / 2;
        fb_draw_text((uint32_t)text_x, (uint32_t)text_y, label, text_color, color);
    }
}

static void wm_calculator_event_handler(struct wm_window *window, const struct wm_event *event)
{
    int32_t button;

    if (window == 0 || event == 0 || window->app_kind != WM_APP_CALCULATOR) {
        return;
    }

    if (event->type == WM_EVENT_MOUSE_UP) {
        window->app.calculator.pressed_button = -1;
        g_wm.needs_redraw = 1U;
        return;
    }

    if (event->type != WM_EVENT_MOUSE_DOWN) {
        return;
    }

    button = wm_calc_hit_button(window, event->mouse_x, event->mouse_y);
    window->app.calculator.pressed_button = (int8_t)button;
    if (button < 0) {
        g_wm.needs_redraw = 1U;
        return;
    }

    wm_calc_handle_button(&window->app.calculator, g_calc_labels[(uint32_t)button]);
    g_wm.needs_redraw = 1U;
}

static uint32_t wm_uptime_stopwatch_ticks(const struct wm_uptime_state *uptime)
{
    uint32_t elapsed;

    if (uptime == 0) {
        return 0U;
    }

    elapsed = uptime->stopwatch_elapsed_ticks;
    if (uptime->stopwatch_running != 0U) {
        elapsed += (pit_get_ticks() - uptime->stopwatch_started_tick);
    }

    return elapsed;
}

static int wm_uptime_button_rect(const struct wm_window *window, uint32_t button_index, struct wm_rect *out_rect)
{
    struct wm_rect content;
    int32_t button_w;

    if (window == 0 || out_rect == 0 || button_index > 1U) {
        return 0;
    }

    if (wm_window_content_rect(window, &content) == 0) {
        return 0;
    }

    if (content.w < 32U || content.h < 72U) {
        return 0;
    }

    button_w = ((int32_t)content.w - 12) / 2;
    if (button_w <= 0) {
        return 0;
    }

    out_rect->x = content.x + 4 + ((int32_t)button_index * (button_w + 4));
    out_rect->y = content.y + (int32_t)content.h - 28;
    out_rect->w = (uint32_t)button_w;
    out_rect->h = 24U;
    return 1;
}

static void wm_uptime_draw(const struct wm_window *window)
{
    struct wm_rect content;
    const struct wm_uptime_state *uptime;
    struct wm_rect button_rect;
    char uptime_text[16];
    char stopwatch_text[16];
    uint32_t uptime_seconds;
    uint32_t stopwatch_seconds;

    if (window == 0 || window->app_kind != WM_APP_UPTIME) {
        return;
    }

    uptime = &window->app.uptime;
    if (wm_window_content_rect(window, &content) == 0) {
        return;
    }

    wm_fill_rect_clamped(content.x, content.y, content.w, content.h, 0x111827U);
    fb_draw_text((uint32_t)(content.x + 4), (uint32_t)(content.y + 4), "SYSTEM UPTIME", 0x93C5FDU, 0x111827U);

    uptime_seconds = pit_get_ticks() / PIT_TARGET_FREQ;
    wm_format_hms(uptime_seconds, uptime_text, sizeof(uptime_text));
    fb_draw_text((uint32_t)(content.x + 4), (uint32_t)(content.y + 24), uptime_text, 0xE2E8F0U, 0x111827U);

    fb_draw_text((uint32_t)(content.x + 4), (uint32_t)(content.y + 48), "STOPWATCH", 0x67E8F9U, 0x111827U);
    stopwatch_seconds = wm_uptime_stopwatch_ticks(uptime) / PIT_TARGET_FREQ;
    wm_format_hms(stopwatch_seconds, stopwatch_text, sizeof(stopwatch_text));
    fb_draw_text((uint32_t)(content.x + 4), (uint32_t)(content.y + 68), stopwatch_text, 0xF8FAFCU, 0x111827U);

    if (wm_uptime_button_rect(window, 0U, &button_rect) != 0) {
        uint32_t color = (uptime->stopwatch_running != 0U) ? 0xB91C1CU : 0x0F766EU;
        const char *label = (uptime->stopwatch_running != 0U) ? "STOP" : "START";
        if (uptime->pressed_button == 0) {
            color = wm_dim_color(color, 140U);
        }
        wm_fill_rect_clamped(button_rect.x, button_rect.y, button_rect.w, button_rect.h, color);
        fb_draw_text((uint32_t)(button_rect.x + 12), (uint32_t)(button_rect.y + 4), label, 0xFFFFFFU, color);
    }

    if (wm_uptime_button_rect(window, 1U, &button_rect) != 0) {
        uint32_t color = 0x334155U;
        if (uptime->pressed_button == 1) {
            color = wm_dim_color(color, 140U);
        }
        wm_fill_rect_clamped(button_rect.x, button_rect.y, button_rect.w, button_rect.h, color);
        fb_draw_text((uint32_t)(button_rect.x + 12), (uint32_t)(button_rect.y + 4), "RESET", 0xFFFFFFU, color);
    }
}

static void wm_uptime_event_handler(struct wm_window *window, const struct wm_event *event)
{
    struct wm_uptime_state *uptime;
    struct wm_rect button_rect;

    if (window == 0 || event == 0 || window->app_kind != WM_APP_UPTIME) {
        return;
    }

    uptime = &window->app.uptime;
    if (event->type == WM_EVENT_MOUSE_UP) {
        uptime->pressed_button = -1;
        g_wm.needs_redraw = 1U;
        return;
    }

    if (event->type != WM_EVENT_MOUSE_DOWN) {
        return;
    }

    uptime->pressed_button = -1;
    for (uint32_t i = 0U; i < 2U; i++) {
        if (wm_uptime_button_rect(window, i, &button_rect) == 0) {
            continue;
        }

        if (wm_rect_contains(&button_rect, event->mouse_x, event->mouse_y) == 0) {
            continue;
        }

        uptime->pressed_button = (int8_t)i;
        if (i == 0U) {
            if (uptime->stopwatch_running != 0U) {
                uptime->stopwatch_elapsed_ticks += (pit_get_ticks() - uptime->stopwatch_started_tick);
                uptime->stopwatch_running = 0U;
            } else {
                uptime->stopwatch_started_tick = pit_get_ticks();
                uptime->stopwatch_running = 1U;
            }
        } else {
            uptime->stopwatch_elapsed_ticks = 0U;
            if (uptime->stopwatch_running != 0U) {
                uptime->stopwatch_started_tick = pit_get_ticks();
            }
        }
        break;
    }

    g_wm.needs_redraw = 1U;
}

static int wm_checklist_row_rect(const struct wm_window *window, uint32_t row_index, struct wm_rect *out_rect)
{
    struct wm_rect content;
    int32_t row_h;

    if (window == 0 || out_rect == 0 || row_index >= WM_CHECKLIST_ITEMS) {
        return 0;
    }

    if (wm_window_content_rect(window, &content) == 0) {
        return 0;
    }

    if (content.h < 40U) {
        return 0;
    }

    row_h = ((int32_t)content.h - 26) / (int32_t)WM_CHECKLIST_ITEMS;
    if (row_h < 18) {
        row_h = 18;
    }

    out_rect->x = content.x + 4;
    out_rect->y = content.y + 22 + ((int32_t)row_index * row_h);
    out_rect->w = (content.w > 8U) ? (content.w - 8U) : content.w;
    out_rect->h = (uint32_t)(row_h - 2);
    return 1;
}

static void wm_checklist_draw(const struct wm_window *window)
{
    struct wm_rect content;
    const struct wm_checklist_state *checklist;

    if (window == 0 || window->app_kind != WM_APP_CHECKLIST) {
        return;
    }

    checklist = &window->app.checklist;
    if (wm_window_content_rect(window, &content) == 0) {
        return;
    }

    wm_fill_rect_clamped(content.x, content.y, content.w, content.h, 0x1F2937U);
    fb_draw_text((uint32_t)(content.x + 4), (uint32_t)(content.y + 4), "SYSTEM CHECKLIST", 0x6EE7B7U, 0x1F2937U);

    for (uint32_t i = 0U; i < WM_CHECKLIST_ITEMS; i++) {
        struct wm_rect row_rect;
        struct wm_rect box_rect;
        uint32_t row_color = (i & 1U) != 0U ? 0x253043U : 0x1E293BU;

        if (wm_checklist_row_rect(window, i, &row_rect) == 0) {
            continue;
        }

        if ((int32_t)i == checklist->pressed_item) {
            row_color = 0x334155U;
        }

        wm_fill_rect_clamped(row_rect.x, row_rect.y, row_rect.w, row_rect.h, row_color);

        box_rect.x = row_rect.x + 4;
        box_rect.y = row_rect.y + 3;
        box_rect.w = 14U;
        box_rect.h = 14U;
        wm_fill_rect_clamped(box_rect.x, box_rect.y, box_rect.w, box_rect.h, 0x0B1220U);
        wm_fill_rect_clamped(box_rect.x + 1, box_rect.y + 1, box_rect.w - 2U, box_rect.h - 2U, 0xF8FAFCU);
        if (checklist->checked[i] != 0U) {
            wm_fill_rect_clamped(box_rect.x + 3, box_rect.y + 3, box_rect.w - 6U, box_rect.h - 6U, 0x10B981U);
        }

        fb_draw_text((uint32_t)(row_rect.x + 24), (uint32_t)(row_rect.y + 2), g_checklist_labels[i], 0xE5E7EBU, row_color);
    }
}

static void wm_checklist_event_handler(struct wm_window *window, const struct wm_event *event)
{
    struct wm_checklist_state *checklist;
    struct wm_rect row_rect;

    if (window == 0 || event == 0 || window->app_kind != WM_APP_CHECKLIST) {
        return;
    }

    checklist = &window->app.checklist;
    if (event->type == WM_EVENT_MOUSE_UP) {
        checklist->pressed_item = -1;
        g_wm.needs_redraw = 1U;
        return;
    }

    if (event->type != WM_EVENT_MOUSE_DOWN) {
        return;
    }

    checklist->pressed_item = -1;
    for (uint32_t i = 0U; i < WM_CHECKLIST_ITEMS; i++) {
        if (wm_checklist_row_rect(window, i, &row_rect) == 0) {
            continue;
        }

        if (wm_rect_contains(&row_rect, event->mouse_x, event->mouse_y) == 0) {
            continue;
        }

        checklist->checked[i] ^= 1U;
        checklist->pressed_item = (int8_t)i;
        break;
    }

    g_wm.needs_redraw = 1U;
}

static void wm_init_calculator_app(struct wm_window *window)
{
    if (window == 0) {
        return;
    }

    window->app_kind = WM_APP_CALCULATOR;
    window->on_event = wm_calculator_event_handler;
    window->on_draw = wm_calculator_draw;
    wm_calc_reset(&window->app.calculator);
}

static void wm_init_uptime_app(struct wm_window *window)
{
    if (window == 0) {
        return;
    }

    window->app_kind = WM_APP_UPTIME;
    window->on_event = wm_uptime_event_handler;
    window->on_draw = wm_uptime_draw;
    window->app.uptime.stopwatch_running = 0U;
    window->app.uptime.stopwatch_started_tick = pit_get_ticks();
    window->app.uptime.stopwatch_elapsed_ticks = 0U;
    window->app.uptime.pressed_button = -1;
}

static void wm_init_checklist_app(struct wm_window *window)
{
    if (window == 0) {
        return;
    }

    window->app_kind = WM_APP_CHECKLIST;
    window->on_event = wm_checklist_event_handler;
    window->on_draw = wm_checklist_draw;
    for (uint32_t i = 0U; i < WM_CHECKLIST_ITEMS; i++) {
        window->app.checklist.checked[i] = 0U;
    }
    window->app.checklist.pressed_item = -1;
}

static void wm_terminal_new_line(struct wm_terminal_state *terminal)
{
    uint16_t row = terminal->line_head;

    if (terminal == 0) {
        return;
    }

    terminal->line_head = (uint16_t)((terminal->line_head + 1U) % WM_TERM_HISTORY_LINES);
    if (terminal->line_count < WM_TERM_HISTORY_LINES) {
        terminal->line_count++;
    }

    terminal->lines[row][0] = '\0';
}

static char *wm_terminal_current_line(struct wm_terminal_state *terminal)
{
    uint16_t idx;

    if (terminal == 0) {
        return 0;
    }

    if (terminal->line_count == 0U) {
        terminal->line_head = 0U;
        terminal->line_count = 1U;
        terminal->lines[0][0] = '\0';
    }

    idx = (uint16_t)((terminal->line_head + WM_TERM_HISTORY_LINES - 1U) % WM_TERM_HISTORY_LINES);
    return terminal->lines[idx];
}

static const char *wm_terminal_line_at(const struct wm_terminal_state *terminal, uint16_t logical_index)
{
    uint16_t oldest;
    uint16_t idx;

    if (terminal == 0 || logical_index >= terminal->line_count) {
        return "";
    }

    oldest = (terminal->line_count == WM_TERM_HISTORY_LINES) ? terminal->line_head : 0U;
    idx = (uint16_t)((oldest + logical_index) % WM_TERM_HISTORY_LINES);
    return terminal->lines[idx];
}

static void wm_terminal_append_char(struct wm_terminal_state *terminal, char c)
{
    char *line;
    uint32_t len;

    if (terminal == 0) {
        return;
    }

    if (c == '\r') {
        return;
    }

    if (c == '\n') {
        wm_terminal_new_line(terminal);
        return;
    }

    if (c == '\b') {
        line = wm_terminal_current_line(terminal);
        if (line == 0) {
            return;
        }

        len = wm_strnlen(line, WM_TERM_LINE_MAX);
        if (len > 0U) {
            line[len - 1U] = '\0';
        }
        return;
    }

    if (c == '\t') {
        wm_terminal_append_char(terminal, ' ');
        wm_terminal_append_char(terminal, ' ');
        wm_terminal_append_char(terminal, ' ');
        wm_terminal_append_char(terminal, ' ');
        return;
    }

    line = wm_terminal_current_line(terminal);
    if (line == 0) {
        return;
    }

    len = wm_strnlen(line, WM_TERM_LINE_MAX);
    if (len + 1U >= WM_TERM_LINE_MAX) {
        wm_terminal_new_line(terminal);
        line = wm_terminal_current_line(terminal);
        len = 0U;
    }

    line[len] = c;
    line[len + 1U] = '\0';
}

static void wm_terminal_append_text(struct wm_terminal_state *terminal, const char *text, uint32_t len)
{
    if (terminal == 0 || text == 0) {
        return;
    }

    for (uint32_t i = 0U; i < len; i++) {
        wm_terminal_append_char(terminal, text[i]);
    }
}

static void wm_terminal_append_cstr(struct wm_terminal_state *terminal, const char *text)
{
    uint32_t len = wm_strnlen(text, 4096U);
    wm_terminal_append_text(terminal, text, len);
}

static int32_t wm_find_window_by_app(enum wm_app_kind app_kind)
{
    for (uint32_t i = 0U; i < WM_MAX_WINDOWS; i++) {
        if (g_wm.windows[i].in_use == 0U) {
            continue;
        }

        if (g_wm.windows[i].app_kind == app_kind) {
            return (int32_t)i;
        }
    }

    return -1;
}

static void wm_terminal_console_hook(const char *text, uint32_t len, void *ctx)
{
    struct wm_window *window;

    (void)ctx;

    if (text == 0 || len == 0U || g_terminal_window_index < 0) {
        return;
    }

    if (g_terminal_window_index >= (int32_t)WM_MAX_WINDOWS) {
        g_terminal_window_index = -1;
        return;
    }

    window = &g_wm.windows[(uint32_t)g_terminal_window_index];
    if (window->in_use == 0U || window->app_kind != WM_APP_TERMINAL) {
        g_terminal_window_index = -1;
        return;
    }

    wm_terminal_append_text(&window->app.terminal, text, len);
    g_wm.needs_redraw = 1U;
}

static void wm_terminal_submit_input(struct wm_window *window)
{
    struct wm_terminal_state *terminal;
    char command[WM_TERM_INPUT_MAX];
    uint32_t cmd_len;

    if (window == 0 || window->app_kind != WM_APP_TERMINAL) {
        return;
    }

    terminal = &window->app.terminal;
    cmd_len = terminal->input_len;
    if (cmd_len >= WM_TERM_INPUT_MAX) {
        cmd_len = WM_TERM_INPUT_MAX - 1U;
    }

    for (uint32_t i = 0U; i < cmd_len; i++) {
        command[i] = terminal->input[i];
    }
    command[cmd_len] = '\0';

    wm_terminal_append_cstr(terminal, WM_TERMINAL_PROMPT);
    wm_terminal_append_cstr(terminal, command);
    wm_terminal_append_char(terminal, '\n');

    terminal->input_len = 0U;
    terminal->input[0] = '\0';

    if (command[0] != '\0') {
        console_execute_command(command);
    }
}

static void wm_terminal_handle_key(struct wm_window *window, char c)
{
    struct wm_terminal_state *terminal;

    if (window == 0 || window->app_kind != WM_APP_TERMINAL) {
        return;
    }

    terminal = &window->app.terminal;
    if (c == '\r') {
        return;
    }

    if (c == '\n') {
        wm_terminal_submit_input(window);
        g_wm.needs_redraw = 1U;
        return;
    }

    if (c == '\b') {
        if (terminal->input_len > 0U) {
            terminal->input_len--;
            terminal->input[terminal->input_len] = '\0';
            g_wm.needs_redraw = 1U;
        }
        return;
    }

    if (c == '\t') {
        c = ' ';
    }

    if (c < 32 || c > 126) {
        return;
    }

    if (terminal->input_len + 1U >= WM_TERM_INPUT_MAX) {
        return;
    }

    terminal->input[terminal->input_len++] = c;
    terminal->input[terminal->input_len] = '\0';
    g_wm.needs_redraw = 1U;
}

static void wm_terminal_draw(const struct wm_window *window)
{
    struct wm_rect content;
    const struct wm_terminal_state *terminal;
    int32_t draw_y;
    int32_t input_y;
    uint32_t max_rows;
    uint32_t start_row;
    uint32_t row_index;
    uint32_t input_text_len;
    uint32_t ticks;

    if (window == 0 || window->app_kind != WM_APP_TERMINAL) {
        return;
    }

    terminal = &window->app.terminal;
    if (wm_window_content_rect(window, &content) == 0) {
        return;
    }

    wm_fill_rect_clamped(content.x, content.y, content.w, content.h, 0x0A0F1AU);
    wm_fill_rect_clamped(content.x, content.y, content.w, 18U, 0x111827U);
    fb_draw_text((uint32_t)(content.x + 4), (uint32_t)(content.y + 1),
                 "TERMINAL EMULATOR", 0x93C5FDU, 0x111827U);

    input_y = content.y + (int32_t)content.h - 18;
    wm_fill_rect_clamped(content.x, input_y - 2, content.w, 1U, 0x1F2937U);
    wm_fill_rect_clamped(content.x, input_y, content.w, 18U, 0x111827U);
    fb_draw_text((uint32_t)(content.x + 4), (uint32_t)input_y, WM_TERMINAL_PROMPT, 0x6EE7B7U, 0x111827U);
    fb_draw_text((uint32_t)(content.x + 4 + (wm_strnlen(WM_TERMINAL_PROMPT, 32U) * 8U)),
                 (uint32_t)input_y, terminal->input, 0xE2E8F0U, 0x111827U);

    input_text_len = wm_strnlen(WM_TERMINAL_PROMPT, 32U) + terminal->input_len;
    ticks = pit_get_ticks();
    if (((ticks / 25U) & 1U) != 0U) {
        wm_fill_rect_clamped(content.x + 4 + (int32_t)(input_text_len * 8U),
                             input_y + 14, 8U, 2U, 0xE2E8F0U);
    }

    if (content.h <= 38U) {
        return;
    }

    max_rows = (content.h - 38U) / 16U;
    if (max_rows == 0U) {
        max_rows = 1U;
    }

    if (terminal->line_count > max_rows) {
        start_row = terminal->line_count - max_rows;
    } else {
        start_row = 0U;
    }

    draw_y = content.y + 20;
    for (row_index = start_row; row_index < terminal->line_count; row_index++) {
        const char *line = wm_terminal_line_at(terminal, (uint16_t)row_index);
        fb_draw_text((uint32_t)(content.x + 4), (uint32_t)draw_y, line, 0xE2E8F0U, 0x0A0F1AU);
        draw_y += 16;
        if (draw_y + 16 > input_y) {
            break;
        }
    }
}

static void wm_init_terminal_app(struct wm_window *window)
{
    struct wm_terminal_state *terminal;

    if (window == 0) {
        return;
    }

    terminal = &window->app.terminal;
    for (uint32_t row = 0U; row < WM_TERM_HISTORY_LINES; row++) {
        terminal->lines[row][0] = '\0';
    }
    terminal->line_head = 0U;
    terminal->line_count = 1U;
    terminal->lines[0][0] = '\0';
    terminal->input_len = 0U;
    terminal->input[0] = '\0';

    wm_terminal_append_cstr(terminal, "ClaudeOS terminal online.\n");
    wm_terminal_append_cstr(terminal, "Type help and press Enter.\n");

    window->app_kind = WM_APP_TERMINAL;
    window->on_event = wm_default_event_handler;
    window->on_draw = wm_terminal_draw;
    g_terminal_window_index = wm_find_window_by_app(WM_APP_TERMINAL);
}

static void wm_set_focus(int32_t window_index)
{
    for (uint32_t i = 0U; i < WM_MAX_WINDOWS; i++) {
        if (g_wm.windows[i].in_use == 0U) {
            continue;
        }

        g_wm.windows[i].focused = (uint8_t)((int32_t)i == window_index);
        if (g_wm.windows[i].focused != 0U) {
            g_wm.windows[i].title_color = g_wm.windows[i].title_color_active;
        } else {
            g_wm.windows[i].title_color = g_wm.windows[i].title_color_inactive;
        }
    }
}

static int32_t wm_hit_test(int32_t x, int32_t y)
{
    int32_t order_index = (int32_t)g_wm.window_count - 1;

    if (y >= wm_workspace_bottom()) {
        return -1;
    }

    while (order_index >= 0) {
        uint8_t idx = g_wm.z_order[(uint32_t)order_index];
        struct wm_window *window = &g_wm.windows[idx];
        int32_t x0;
        int32_t y0;
        int32_t x1;
        int32_t y1;

        if (window->in_use == 0U) {
            order_index--;
            continue;
        }

        x0 = window->x;
        y0 = window->y;
        x1 = x0 + (int32_t)window->width;
        y1 = y0 + (int32_t)window->height;

        if (x >= x0 && x < x1 && y >= y0 && y < y1) {
            return (int32_t)idx;
        }

        order_index--;
    }

    return -1;
}

static int wm_point_in_titlebar(const struct wm_window *window, int32_t x, int32_t y)
{
    int32_t title_x0;
    int32_t title_y0;
    int32_t title_x1;
    int32_t title_y1;

    if (window == 0 || window->in_use == 0U) {
        return 0;
    }

    title_x0 = window->x + (int32_t)WM_BORDER_SIZE;
    title_y0 = window->y + (int32_t)WM_BORDER_SIZE;
    title_x1 = window->x + (int32_t)window->width - (int32_t)WM_BORDER_SIZE;
    title_y1 = title_y0 + (int32_t)WM_TITLEBAR_HEIGHT;

    if (x < title_x0 || x >= title_x1) {
        return 0;
    }
    if (y < title_y0 || y >= title_y1) {
        return 0;
    }

    return 1;
}

static int wm_point_in_close_button(const struct wm_window *window, int32_t x, int32_t y)
{
    int32_t inner_x;
    int32_t inner_y;
    uint32_t inner_w;
    uint32_t inner_h;
    struct wm_rect close_rect;

    if (window == 0 || window->in_use == 0U) {
        return 0;
    }

    if (window->width <= (2U * WM_BORDER_SIZE) || window->height <= (2U * WM_BORDER_SIZE)) {
        return 0;
    }

    inner_x = window->x + (int32_t)WM_BORDER_SIZE;
    inner_y = window->y + (int32_t)WM_BORDER_SIZE;
    inner_w = window->width - (2U * WM_BORDER_SIZE);
    inner_h = window->height - (2U * WM_BORDER_SIZE);
    if (inner_h <= WM_TITLEBAR_HEIGHT || inner_w <= (WM_TITLEBAR_HEIGHT + 8U)) {
        return 0;
    }

    close_rect.x = inner_x + (int32_t)inner_w - (int32_t)WM_TITLEBAR_HEIGHT + 2;
    close_rect.y = inner_y + 4;
    close_rect.w = WM_TITLEBAR_HEIGHT - 6U;
    close_rect.h = WM_TITLEBAR_HEIGHT - 8U;
    return wm_rect_contains(&close_rect, x, y);
}

static void wm_bring_to_front(int32_t window_index)
{
    uint32_t pos = 0U;
    uint32_t found = 0U;

    for (uint32_t i = 0U; i < g_wm.window_count; i++) {
        if ((int32_t)g_wm.z_order[i] == window_index) {
            pos = i;
            found = 1U;
            break;
        }
    }

    if (found == 0U || pos == (g_wm.window_count - 1U)) {
        return;
    }

    for (uint32_t i = pos; i + 1U < g_wm.window_count; i++) {
        g_wm.z_order[i] = g_wm.z_order[i + 1U];
    }
    g_wm.z_order[g_wm.window_count - 1U] = (uint8_t)window_index;
}

static void wm_close_window(int32_t window_index)
{
    uint32_t pos = 0U;
    uint32_t found = 0U;

    if (window_index < 0 || window_index >= (int32_t)WM_MAX_WINDOWS) {
        return;
    }

    if (g_wm.windows[(uint32_t)window_index].in_use == 0U) {
        return;
    }

    g_wm.windows[(uint32_t)window_index].in_use = 0U;
    g_wm.windows[(uint32_t)window_index].focused = 0U;
    g_wm.windows[(uint32_t)window_index].on_draw = 0;
    g_wm.windows[(uint32_t)window_index].on_event = 0;
    if (g_wm.windows[(uint32_t)window_index].app_kind == WM_APP_TERMINAL) {
        g_terminal_window_index = -1;
    }
    g_wm.windows[(uint32_t)window_index].app_kind = WM_APP_NONE;
    if (g_wm.drag_window == window_index) {
        g_wm.drag_window = -1;
    }

    for (uint32_t i = 0U; i < g_wm.window_count; i++) {
        if ((int32_t)g_wm.z_order[i] == window_index) {
            pos = i;
            found = 1U;
            break;
        }
    }

    if (found != 0U) {
        for (uint32_t i = pos; i + 1U < g_wm.window_count; i++) {
            g_wm.z_order[i] = g_wm.z_order[i + 1U];
        }

        if (g_wm.window_count > 0U) {
            g_wm.window_count--;
            g_wm.z_order[g_wm.window_count] = 0U;
        }
    }

    if (g_wm.window_count > 0U) {
        wm_set_focus((int32_t)g_wm.z_order[g_wm.window_count - 1U]);
    } else {
        wm_set_focus(-1);
    }
}

static void wm_work_area_rect(struct wm_rect *out_rect)
{
    int32_t bottom;
    int32_t width;
    int32_t height;

    if (out_rect == 0) {
        return;
    }

    bottom = wm_workspace_bottom();
    width = g_wm.screen_w - 16;
    height = bottom - 10;
    if (width < (int32_t)WM_MIN_WINDOW_W) {
        width = (int32_t)WM_MIN_WINDOW_W;
    }
    if (height < (int32_t)WM_MIN_WINDOW_H) {
        height = (int32_t)WM_MIN_WINDOW_H;
    }

    out_rect->x = 8;
    out_rect->y = 8;
    out_rect->w = (uint32_t)width;
    out_rect->h = (uint32_t)height;
}

static void wm_app_default_geometry(enum wm_app_kind app_kind,
                                    int32_t *x, int32_t *y,
                                    uint32_t *w, uint32_t *h)
{
    struct wm_rect work;
    int32_t right_x;
    int32_t right_w;
    int32_t calc_h;
    int32_t uptime_h;
    int32_t checklist_h;

    wm_work_area_rect(&work);

    right_x = work.x + (int32_t)((work.w * 62U) / 100U) + 8;
    if (right_x + (int32_t)WM_MIN_WINDOW_W > (work.x + (int32_t)work.w)) {
        right_x = work.x + (int32_t)work.w - (int32_t)WM_MIN_WINDOW_W;
    }
    if (right_x < work.x + 220) {
        right_x = work.x + 220;
    }

    right_w = (work.x + (int32_t)work.w) - right_x;
    if (right_w < (int32_t)WM_MIN_WINDOW_W) {
        right_w = (int32_t)WM_MIN_WINDOW_W;
    }

    calc_h = (int32_t)((work.h * 45U) / 100U);
    uptime_h = 150;
    if (calc_h < 170) {
        calc_h = 170;
    }

    checklist_h = (int32_t)work.h - calc_h - uptime_h - 16;
    if (checklist_h < 140) {
        int32_t deficit = 140 - checklist_h;

        if (calc_h > 150) {
            int32_t reduce = calc_h - 150;
            if (reduce > deficit) {
                reduce = deficit;
            }
            calc_h -= reduce;
            deficit -= reduce;
        }

        if (deficit > 0 && uptime_h > 110) {
            int32_t reduce = uptime_h - 110;
            if (reduce > deficit) {
                reduce = deficit;
            }
            uptime_h -= reduce;
            deficit -= reduce;
        }

        checklist_h = (int32_t)work.h - calc_h - uptime_h - 16;
        if (checklist_h < 100) {
            checklist_h = 100;
        }
    }

    switch (app_kind) {
    case WM_APP_TERMINAL:
        *x = work.x;
        *y = work.y;
        *w = (uint32_t)(right_x - work.x - 8);
        *h = work.h;
        break;
    case WM_APP_CALCULATOR:
        *x = right_x;
        *y = work.y;
        *w = (uint32_t)right_w;
        *h = (uint32_t)calc_h;
        break;
    case WM_APP_UPTIME:
        *x = right_x;
        *y = work.y + calc_h + 8;
        *w = (uint32_t)right_w;
        *h = (uint32_t)uptime_h;
        break;
    case WM_APP_CHECKLIST:
        *x = right_x;
        *y = work.y + calc_h + uptime_h + 16;
        *w = (uint32_t)right_w;
        *h = (uint32_t)checklist_h;
        break;
    default:
        *x = work.x;
        *y = work.y;
        *w = WM_MIN_WINDOW_W;
        *h = WM_MIN_WINDOW_H;
        break;
    }
}

static int32_t wm_spawn_app_window(enum wm_app_kind app_kind)
{
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
    int32_t slot;

    wm_app_default_geometry(app_kind, &x, &y, &w, &h);

    if (app_kind == WM_APP_TERMINAL) {
        slot = wm_create_window("Terminal", x, y, w, h, 0x0A0F1AU, 0x2563EBU);
        if (slot >= 0) {
            wm_init_terminal_app(&g_wm.windows[(uint32_t)slot]);
            g_terminal_window_index = slot;
        }
        return slot;
    }

    if (app_kind == WM_APP_CALCULATOR) {
        slot = wm_create_window("Calculator", x, y, w, h, 0x101827U, 0x2563EBU);
        if (slot >= 0) {
            wm_init_calculator_app(&g_wm.windows[(uint32_t)slot]);
        }
        return slot;
    }

    if (app_kind == WM_APP_UPTIME) {
        slot = wm_create_window("Uptime", x, y, w, h, 0x14213DU, 0x06B6D4U);
        if (slot >= 0) {
            wm_init_uptime_app(&g_wm.windows[(uint32_t)slot]);
        }
        return slot;
    }

    if (app_kind == WM_APP_CHECKLIST) {
        slot = wm_create_window("Checklist", x, y, w, h, 0x1F2937U, 0x059669U);
        if (slot >= 0) {
            wm_init_checklist_app(&g_wm.windows[(uint32_t)slot]);
        }
        return slot;
    }

    return -1;
}

static int32_t wm_focus_or_launch_app(enum wm_app_kind app_kind)
{
    int32_t target = wm_find_window_by_app(app_kind);

    if (target < 0) {
        target = wm_spawn_app_window(app_kind);
    }

    if (target >= 0) {
        wm_bring_to_front(target);
        wm_set_focus(target);
    }

    return target;
}

static int32_t wm_focused_window_index(void)
{
    for (uint32_t i = 0U; i < WM_MAX_WINDOWS; i++) {
        if (g_wm.windows[i].in_use != 0U && g_wm.windows[i].focused != 0U) {
            return (int32_t)i;
        }
    }

    return -1;
}

static void wm_taskbar_rect(struct wm_rect *out_rect)
{
    int32_t y = wm_workspace_bottom();

    if (out_rect == 0) {
        return;
    }

    if (y < 0) {
        y = 0;
    }

    out_rect->x = 0;
    out_rect->y = y;
    out_rect->w = (uint32_t)g_wm.screen_w;
    out_rect->h = (uint32_t)(g_wm.screen_h - y);
}

static int wm_taskbar_button_rect(uint32_t index, struct wm_rect *out_rect)
{
    struct wm_rect bar;
    int32_t button_w = 90;
    int32_t gap = 6;
    int32_t total_w;

    if (out_rect == 0 || index >= WM_DOCK_BUTTONS) {
        return 0;
    }

    wm_taskbar_rect(&bar);
    if (bar.h < 8U || bar.w < 32U) {
        return 0;
    }

    total_w = (int32_t)(WM_DOCK_BUTTONS * (uint32_t)button_w) + ((int32_t)WM_DOCK_BUTTONS - 1) * gap;
    if (total_w > (int32_t)bar.w - 16) {
        button_w = ((int32_t)bar.w - 16 - (((int32_t)WM_DOCK_BUTTONS - 1) * gap)) / (int32_t)WM_DOCK_BUTTONS;
        if (button_w < 40) {
            button_w = 40;
        }
    }

    out_rect->x = bar.x + 8 + (int32_t)index * (button_w + gap);
    out_rect->y = bar.y + 4;
    out_rect->w = (uint32_t)button_w;
    out_rect->h = bar.h - 8U;
    return 1;
}

static int wm_handle_taskbar_click(int32_t x, int32_t y)
{
    struct wm_rect bar;
    struct wm_rect button_rect;

    wm_taskbar_rect(&bar);
    if (wm_rect_contains(&bar, x, y) == 0) {
        return 0;
    }

    for (uint32_t i = 0U; i < WM_DOCK_BUTTONS; i++) {
        if (wm_taskbar_button_rect(i, &button_rect) == 0) {
            continue;
        }

        if (wm_rect_contains(&button_rect, x, y) != 0) {
            (void)wm_focus_or_launch_app(g_dock_apps[i]);
            g_wm.needs_redraw = 1U;
            return 1;
        }
    }

    g_wm.needs_redraw = 1U;
    return 1;
}

static void wm_draw_taskbar(void)
{
    struct wm_rect bar;
    char clock[16];

    wm_taskbar_rect(&bar);
    if (bar.h == 0U) {
        return;
    }

    wm_fill_rect_clamped(bar.x, bar.y, bar.w, bar.h, 0x0B1220U);
    wm_fill_rect_clamped(bar.x, bar.y, bar.w, 1U, 0x23324DU);

    for (uint32_t i = 0U; i < WM_DOCK_BUTTONS; i++) {
        struct wm_rect button_rect;
        int32_t window_idx;
        uint32_t bg;

        if (wm_taskbar_button_rect(i, &button_rect) == 0) {
            continue;
        }

        window_idx = wm_find_window_by_app(g_dock_apps[i]);
        if (window_idx >= 0 && g_wm.windows[(uint32_t)window_idx].focused != 0U) {
            bg = 0x2563EBU;
        } else if (window_idx >= 0) {
            bg = 0x334155U;
        } else {
            bg = 0x1E293BU;
        }

        wm_fill_rect_clamped(button_rect.x, button_rect.y, button_rect.w, button_rect.h, bg);
        wm_fill_rect_clamped(button_rect.x, button_rect.y, button_rect.w, 1U, wm_dim_color(bg, 65U));
        fb_draw_text((uint32_t)(button_rect.x + 8), (uint32_t)(button_rect.y + 3),
                     g_dock_labels[i], 0xFFFFFFU, bg);
    }

    wm_format_hms(pit_get_ticks() / PIT_TARGET_FREQ, clock, sizeof(clock));
    fb_draw_text((uint32_t)(bar.x + (int32_t)bar.w - 74), (uint32_t)(bar.y + 5), clock, 0xBFDBFEU, 0x0B1220U);
    fb_draw_text((uint32_t)(bar.x + (int32_t)bar.w - 146), (uint32_t)(bar.y + 5), "ESC EXIT", 0x94A3B8U, 0x0B1220U);
}

static void wm_dispatch_event(int32_t window_index, const struct wm_event *event)
{
    struct wm_window *window;

    if (window_index < 0 || window_index >= (int32_t)WM_MAX_WINDOWS || event == 0) {
        return;
    }

    window = &g_wm.windows[(uint32_t)window_index];
    if (window->in_use == 0U || window->on_event == 0) {
        return;
    }

    window->on_event(window, event);
}

static void wm_draw_window(const struct wm_window *window)
{
    uint32_t inner_w;
    uint32_t inner_h;
    int32_t inner_x;
    int32_t inner_y;
    int32_t title_y;
    uint32_t close_color;
    uint32_t close_border;

    if (window == 0 || window->in_use == 0U) {
        return;
    }

    wm_fill_rect_clamped(window->x, window->y, window->width, window->height, WM_BORDER_COLOR);

    if (window->width <= (2U * WM_BORDER_SIZE) || window->height <= (2U * WM_BORDER_SIZE)) {
        return;
    }

    inner_x = window->x + (int32_t)WM_BORDER_SIZE;
    inner_y = window->y + (int32_t)WM_BORDER_SIZE;
    inner_w = window->width - (2U * WM_BORDER_SIZE);
    inner_h = window->height - (2U * WM_BORDER_SIZE);

    wm_fill_rect_clamped(inner_x, inner_y, inner_w, inner_h, window->body_color);

    title_y = inner_y;
    if (inner_h > WM_TITLEBAR_HEIGHT) {
        wm_fill_rect_clamped(inner_x, title_y, inner_w, WM_TITLEBAR_HEIGHT, window->title_color);
        wm_fill_rect_clamped(inner_x, title_y + (int32_t)WM_TITLEBAR_HEIGHT - 1, inner_w, 1U,
                             wm_dim_color(window->title_color, 45U));
    }

    close_color = 0xAA2222U;
    close_border = 0x330000U;
    if (inner_w > (WM_TITLEBAR_HEIGHT + 8U)) {
        int32_t close_x = inner_x + (int32_t)inner_w - (int32_t)WM_TITLEBAR_HEIGHT + 2;
        int32_t close_y = title_y + 4;
        wm_fill_rect_clamped(close_x, close_y, WM_TITLEBAR_HEIGHT - 6U, WM_TITLEBAR_HEIGHT - 8U, close_border);
        wm_fill_rect_clamped(close_x + 1, close_y + 1, WM_TITLEBAR_HEIGHT - 8U, WM_TITLEBAR_HEIGHT - 10U, close_color);
    }

    if (window->title[0] != '\0' && inner_w > 40U) {
        fb_draw_text((uint32_t)(inner_x + 6), (uint32_t)(title_y + 2),
                     window->title, WM_TITLE_TEXT_COLOR, window->title_color);
    }

    if (window->on_draw != 0) {
        window->on_draw(window);
    }
}

static void wm_draw_cursor(void)
{
    uint32_t color = (g_wm.mouse_buttons & 0x01U) != 0U ? WM_CURSOR_PRESSED : WM_CURSOR_FILL;
    int32_t x = g_wm.mouse_x;
    int32_t y = g_wm.mouse_y;

    wm_fill_rect_clamped(x, y, 10U, 2U, WM_CURSOR_OUTLINE);
    wm_fill_rect_clamped(x, y, 2U, 14U, WM_CURSOR_OUTLINE);
    wm_fill_rect_clamped(x + 1, y + 1, 8U, 1U, color);
    wm_fill_rect_clamped(x + 1, y + 2, 1U, 11U, color);
    wm_fill_rect_clamped(x + 2, y + 3, 4U, 1U, WM_CURSOR_OUTLINE);
}

static void wm_render(void)
{
    if (g_wm.active == 0U || g_wm.ready == 0U) {
        return;
    }

    fb_clear(WM_DESKTOP_COLOR);

    for (uint32_t i = 0U; i < g_wm.window_count; i++) {
        uint8_t idx = g_wm.z_order[i];
        wm_draw_window(&g_wm.windows[idx]);
    }

    wm_draw_taskbar();
    wm_draw_cursor();
    fb_swap_buffers();
    g_wm.needs_redraw = 0U;
}

static void wm_move_window_to(int32_t window_index, int32_t new_x, int32_t new_y)
{
    struct wm_window *window;

    if (window_index < 0 || window_index >= (int32_t)WM_MAX_WINDOWS) {
        return;
    }

    window = &g_wm.windows[(uint32_t)window_index];
    if (window->in_use == 0U) {
        return;
    }

    window->x = new_x;
    window->y = new_y;
    wm_clamp_window_position(window);
}

static void wm_handle_mouse_event(const struct mouse_event *mouse_evt)
{
    struct wm_event event;
    int32_t dx;
    int32_t dy;
    int32_t target;
    uint8_t prev_left;
    uint8_t now_left;

    if (mouse_evt == 0) {
        return;
    }

    g_wm.mouse_x = wm_clamp_i32(mouse_evt->x, 0, g_wm.screen_w - 1);
    g_wm.mouse_y = wm_clamp_i32(mouse_evt->y, 0, g_wm.screen_h - 1);

    dx = mouse_evt->dx;
    dy = mouse_evt->dy;
    prev_left = (uint8_t)(g_wm.mouse_buttons & 0x01U);
    g_wm.mouse_buttons = (uint8_t)(mouse_evt->buttons & 0x07U);
    now_left = (uint8_t)(g_wm.mouse_buttons & 0x01U);

    event.mouse_x = g_wm.mouse_x;
    event.mouse_y = g_wm.mouse_y;
    event.dx = dx;
    event.dy = dy;
    event.buttons = g_wm.mouse_buttons;

    if (dx != 0 || dy != 0) {
        target = wm_hit_test(g_wm.mouse_x, g_wm.mouse_y);
        if (target >= 0) {
            event.type = WM_EVENT_MOUSE_MOVE;
            wm_dispatch_event(target, &event);
        }
        g_wm.needs_redraw = 1U;
    }

    if (prev_left == 0U && now_left != 0U) {
        if (wm_handle_taskbar_click(g_wm.mouse_x, g_wm.mouse_y) != 0) {
            g_wm.drag_window = -1;
            return;
        }

        target = wm_hit_test(g_wm.mouse_x, g_wm.mouse_y);
        if (target >= 0) {
            wm_bring_to_front(target);
            wm_set_focus(target);

            if (wm_point_in_close_button(&g_wm.windows[(uint32_t)target], g_wm.mouse_x, g_wm.mouse_y) != 0) {
                wm_close_window(target);
                g_wm.drag_window = -1;
                g_wm.needs_redraw = 1U;
                return;
            }

            event.type = WM_EVENT_FOCUS;
            wm_dispatch_event(target, &event);

            event.type = WM_EVENT_MOUSE_DOWN;
            wm_dispatch_event(target, &event);

            if (wm_point_in_titlebar(&g_wm.windows[(uint32_t)target], g_wm.mouse_x, g_wm.mouse_y) != 0) {
                g_wm.drag_window = (int8_t)target;
                g_wm.drag_off_x = g_wm.mouse_x - g_wm.windows[(uint32_t)target].x;
                g_wm.drag_off_y = g_wm.mouse_y - g_wm.windows[(uint32_t)target].y;
                event.type = WM_EVENT_DRAG_START;
                wm_dispatch_event(target, &event);
            } else {
                g_wm.drag_window = -1;
            }
        } else {
            wm_set_focus(-1);
            g_wm.drag_window = -1;
        }
        g_wm.needs_redraw = 1U;
    }

    if (prev_left != 0U && now_left != 0U && g_wm.drag_window >= 0 && (dx != 0 || dy != 0)) {
        int32_t drag_index = g_wm.drag_window;
        wm_move_window_to(drag_index, g_wm.mouse_x - g_wm.drag_off_x, g_wm.mouse_y - g_wm.drag_off_y);
        event.type = WM_EVENT_DRAG_MOVE;
        wm_dispatch_event(drag_index, &event);
        g_wm.needs_redraw = 1U;
    }

    if (prev_left != 0U && now_left == 0U) {
        target = wm_hit_test(g_wm.mouse_x, g_wm.mouse_y);
        if (target >= 0) {
            event.type = WM_EVENT_MOUSE_UP;
            wm_dispatch_event(target, &event);
        }

        if (g_wm.drag_window >= 0) {
            event.type = WM_EVENT_DRAG_END;
            wm_dispatch_event(g_wm.drag_window, &event);
            g_wm.drag_window = -1;
        }
        g_wm.needs_redraw = 1U;
    }
}

static void wm_create_demo_layout(void)
{
    (void)wm_spawn_app_window(WM_APP_TERMINAL);
    (void)wm_spawn_app_window(WM_APP_CALCULATOR);
    (void)wm_spawn_app_window(WM_APP_UPTIME);
    (void)wm_spawn_app_window(WM_APP_CHECKLIST);

    if (wm_focus_or_launch_app(WM_APP_TERMINAL) < 0 && g_wm.window_count > 0U) {
        wm_set_focus((int32_t)g_wm.z_order[g_wm.window_count - 1U]);
    }
}

int wm_init(void)
{
    struct vbe_mode mode;

    wm_clear_state();
    g_wm.ready = 0U;

    if (fb_is_ready() == 0 || mouse_is_initialized() == 0) {
        serial_puts("[WM] unavailable (requires framebuffer + mouse)\n");
        return -1;
    }

    if (vbe_get_mode(&mode) != 0) {
        serial_puts("[WM] failed to query active VBE mode\n");
        return -1;
    }

    g_wm.screen_w = (int32_t)mode.width;
    g_wm.screen_h = (int32_t)mode.height;
    if (g_wm.screen_w <= 0 || g_wm.screen_h <= 0) {
        serial_puts("[WM] invalid screen geometry\n");
        return -1;
    }

    wm_create_demo_layout();
    g_wm.ready = 1U;

    serial_puts("[WM] initialized ");
    serial_put_dec((uint32_t)g_wm.screen_w);
    serial_putchar('x');
    serial_put_dec((uint32_t)g_wm.screen_h);
    serial_puts(" windows=");
    serial_put_dec(g_wm.window_count);
    serial_puts("\n");
    return 0;
}

int wm_is_ready(void)
{
    return (g_wm.ready != 0U) ? 1 : 0;
}

int wm_start(void)
{
    struct mouse_event state;

    if (g_wm.ready == 0U) {
        return -1;
    }

    if (g_wm.active != 0U) {
        return 0;
    }

    g_wm.active = 1U;
    g_wm.drag_window = -1;
    g_wm.drag_off_x = 0;
    g_wm.drag_off_y = 0;
    g_wm.last_animated_tick = pit_get_ticks();

    if (mouse_get_state(&state) != 0) {
        g_wm.mouse_x = wm_clamp_i32(state.x, 0, g_wm.screen_w - 1);
        g_wm.mouse_y = wm_clamp_i32(state.y, 0, g_wm.screen_h - 1);
        g_wm.mouse_buttons = (uint8_t)(state.buttons & 0x07U);
    } else {
        g_wm.mouse_x = g_wm.screen_w / 2;
        g_wm.mouse_y = g_wm.screen_h / 2;
        g_wm.mouse_buttons = 0U;
    }

    g_wm.needs_redraw = 1U;
    console_set_output_hook(wm_terminal_console_hook, 0);
    wm_render();
    serial_puts("[WM] started (apps: terminal, calculator, uptime, checklist; use dock, Esc exits)\n");
    return 0;
}

void wm_stop(void)
{
    if (g_wm.active == 0U) {
        return;
    }

    g_wm.active = 0U;
    g_wm.drag_window = -1;
    g_wm.needs_redraw = 0U;
    console_set_output_hook(0, 0);
    serial_puts("[WM] stopped\n");
}

int wm_is_active(void)
{
    return (g_wm.active != 0U) ? 1 : 0;
}

void wm_update(void)
{
    struct mouse_event event;
    uint8_t processed = 0U;
    uint32_t now_tick;

    if (g_wm.active == 0U || g_wm.ready == 0U) {
        return;
    }

    while (mouse_read_event(&event) != 0) {
        wm_handle_mouse_event(&event);
        processed = 1U;
    }

    now_tick = pit_get_ticks();
    if ((now_tick - g_wm.last_animated_tick) >= 10U) {
        g_wm.last_animated_tick = now_tick;
        g_wm.needs_redraw = 1U;
    }

    if (processed != 0U || g_wm.needs_redraw != 0U) {
        wm_render();
    }
}

void wm_handle_key(char c)
{
    int32_t focused;

    if (g_wm.active == 0U || g_wm.ready == 0U) {
        return;
    }

    if (c == '`') {
        (void)wm_focus_or_launch_app(WM_APP_TERMINAL);
        return;
    }

    focused = wm_focused_window_index();
    if (focused < 0 || focused >= (int32_t)WM_MAX_WINDOWS) {
        return;
    }

    if (g_wm.windows[(uint32_t)focused].app_kind == WM_APP_TERMINAL) {
        wm_terminal_handle_key(&g_wm.windows[(uint32_t)focused], c);
    }
}
