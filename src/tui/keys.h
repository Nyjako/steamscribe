#ifndef STEAMSCRIBE_TUI_KEYS_H
#define STEAMSCRIBE_TUI_KEYS_H

/* Mirrors the handful of termbox2 TB_KEY_* values the state machine in
 * tui/state.h reacts to, so that pure, unit-testable module doesn't need to
 * include (or link against) termbox2 itself. These values are part of
 * termbox2's own public, ASCII-control-code-based API and are not expected
 * to change. tui/app.c forwards the real tb_event's `key`/`ch` fields here
 * unchanged. */
#define TUI_KEY_ENTER      0x0d
#define TUI_KEY_ESC        0x1b
#define TUI_KEY_BACKSPACE  0x08
#define TUI_KEY_BACKSPACE2 0x7f
#define TUI_KEY_CTRL_C     0x03
#define TUI_KEY_DELETE       (0xffffu - 13)
#define TUI_KEY_HOME         (0xffffu - 14)
#define TUI_KEY_END          (0xffffu - 15)
#define TUI_KEY_ARROW_UP    (0xffffu - 18)
#define TUI_KEY_ARROW_DOWN  (0xffffu - 19)
#define TUI_KEY_ARROW_LEFT  (0xffffu - 20)
#define TUI_KEY_ARROW_RIGHT (0xffffu - 21)
#define TUI_KEY_F2          (0xffffu - 1)
#define TUI_KEY_F3          (0xffffu - 2)
#define TUI_KEY_CTRL_S      0x13

#endif
