#ifndef CLAUDE_WM_H
#define CLAUDE_WM_H

/* Initialize the basic window manager subsystem.
 * Returns 0 when the subsystem is ready for wm_start(), -1 otherwise. */
int wm_init(void);

/* Returns 1 when the window manager can be started, 0 otherwise. */
int wm_is_ready(void);

/* Enter window-manager mode.
 * Returns 0 on success, -1 when unavailable. */
int wm_start(void);

/* Leave window-manager mode. Safe to call when already stopped. */
void wm_stop(void);

/* Returns 1 when window-manager mode is active, 0 otherwise. */
int wm_is_active(void);

/* Process pending mouse events and repaint the desktop as needed. */
void wm_update(void);

/* Route one translated keyboard character to active GUI app(s). */
void wm_handle_key(char c);

#endif /* CLAUDE_WM_H */
