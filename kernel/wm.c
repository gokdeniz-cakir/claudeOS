#include "wm.h"

#include <stdint.h>

#include "fb.h"
#include "mouse.h"
#include "serial.h"
#include "vbe.h"

#define WM_MAX_WINDOWS          8U
#define WM_TITLE_MAX            31U

#define WM_BORDER_SIZE          2U
#define WM_TITLEBAR_HEIGHT      18U
#define WM_MIN_WINDOW_W         120U
#define WM_MIN_WINDOW_H         80U

#define WM_DESKTOP_COLOR        0x1B263BU
#define WM_BORDER_COLOR         0x1E1E1EU
#define WM_TITLE_TEXT_COLOR     0xFFFFFFU
#define WM_CURSOR_OUTLINE       0x000000U
#define WM_CURSOR_FILL          0xFFFFFFU
#define WM_CURSOR_PRESSED       0xFFAA00U

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

struct wm_window;
typedef void (*wm_window_event_fn)(struct wm_window *window, const struct wm_event *event);

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
};

static struct wm_state g_wm;

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

    for (uint32_t i = 0U; i < WM_MAX_WINDOWS; i++) {
        g_wm.z_order[i] = 0U;
        g_wm.windows[i].in_use = 0U;
    }
}

static void wm_copy_title(char *dst, const char *src)
{
    uint32_t i = 0U;

    if (dst == 0 || src == 0) {
        return;
    }

    while (i < WM_TITLE_MAX && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
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
    max_y = g_wm.screen_h - (int32_t)window->height;

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

    wm_clamp_window_position(window);

    g_wm.z_order[g_wm.window_count] = (uint8_t)slot;
    g_wm.window_count++;
    return slot;
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
        target = wm_hit_test(g_wm.mouse_x, g_wm.mouse_y);
        if (target >= 0) {
            wm_bring_to_front(target);
            wm_set_focus(target);

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
    int32_t margin_x = 40;
    int32_t margin_y = 40;
    uint32_t base_w = (uint32_t)g_wm.screen_w / 2U;
    uint32_t base_h = (uint32_t)g_wm.screen_h / 2U;

    if (base_w < WM_MIN_WINDOW_W) {
        base_w = WM_MIN_WINDOW_W;
    }
    if (base_h < WM_MIN_WINDOW_H) {
        base_h = WM_MIN_WINDOW_H;
    }
    if (base_w > (uint32_t)g_wm.screen_w) {
        base_w = (uint32_t)g_wm.screen_w;
    }
    if (base_h > (uint32_t)g_wm.screen_h) {
        base_h = (uint32_t)g_wm.screen_h;
    }

    (void)wm_create_window("System Monitor", margin_x, margin_y,
                           base_w, base_h, 0x2C3E50U, 0x3A7BD5U);
    (void)wm_create_window("Task Log", margin_x + 130, margin_y + 90,
                           (uint32_t)(base_w - 40U), (uint32_t)(base_h - 30U),
                           0x34495EU, 0x16A085U);
    (void)wm_create_window("Files", margin_x + 260, margin_y + 30,
                           (uint32_t)(base_w - 90U), (uint32_t)(base_h - 60U),
                           0x3A3A5AU, 0x8E44ADU);

    if (g_wm.window_count > 0U) {
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
    wm_render();
    serial_puts("[WM] started (drag title bars with mouse, press q to exit)\n");
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

    if (g_wm.active == 0U || g_wm.ready == 0U) {
        return;
    }

    while (mouse_read_event(&event) != 0) {
        wm_handle_mouse_event(&event);
        processed = 1U;
    }

    if (processed != 0U || g_wm.needs_redraw != 0U) {
        wm_render();
    }
}
