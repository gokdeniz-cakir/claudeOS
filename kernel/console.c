#include "console.h"

#include <stdint.h>

#include "elf.h"
#include "process.h"
#include "serial.h"
#include "usermode.h"
#include "vfs.h"
#include "vga.h"
#include "wm.h"

#define CONSOLE_PROMPT       "claudeos> "
#define CONSOLE_LINE_MAX     128u
#define CONSOLE_ARG_MAX      12u
#define CONSOLE_IO_CHUNK     128u
#define CONSOLE_CLEAR_ROWS   30u

static char console_line[CONSOLE_LINE_MAX];
static uint32_t console_len = 0;
static console_output_hook_fn console_output_hook = 0;
static void *console_output_ctx = 0;

static uint32_t console_strnlen(const char *text, uint32_t max_len)
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

static char console_to_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static uint8_t console_text_equals_ci(const char *a, const char *b)
{
    uint32_t i = 0U;

    if (a == 0 || b == 0) {
        return 0U;
    }

    while (a[i] != '\0' && b[i] != '\0') {
        if (console_to_lower(a[i]) != console_to_lower(b[i])) {
            return 0U;
        }
        i++;
    }

    return (uint8_t)(a[i] == '\0' && b[i] == '\0');
}

static void console_copy_bounded(char *dst, uint32_t dst_size, const char *src)
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

static void console_emit_buffer(const char *text, uint32_t len)
{
    if (text == 0 || len == 0U) {
        return;
    }

    for (uint32_t i = 0U; i < len; i++) {
        vga_putchar(text[i]);
    }

    if (console_output_hook != 0) {
        console_output_hook(text, len, console_output_ctx);
    }
}

static void console_emit_text(const char *text)
{
    console_emit_buffer(text, console_strnlen(text, 4096U));
}

static void console_emit_char(char c)
{
    if (c == '\0') {
        return;
    }

    vga_putchar(c);

    if (console_output_hook != 0) {
        console_output_hook(&c, 1U, console_output_ctx);
    }
}

static void console_print_prompt(void)
{
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_emit_text(CONSOLE_PROMPT);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static int console_file_exists(const char *path)
{
    int32_t fd = vfs_open(path, VFS_OPEN_READ);

    if (fd < 0) {
        return 0;
    }

    (void)vfs_close(fd);
    return 1;
}

static void console_builtin_ls(uint32_t argc, char **argv)
{
    const char *path = (argc > 1U) ? argv[1] : "/";

    if (console_text_equals_ci(path, "/") != 0U) {
        console_emit_text("etc\n");
        console_emit_text("fat\n");
        if (console_file_exists("/hello.txt") != 0) {
            console_emit_text("hello.txt\n");
        }
        if (console_file_exists("/elf_demo.elf") != 0) {
            console_emit_text("elf_demo.elf\n");
        }
        if (console_file_exists("/libctest.elf") != 0) {
            console_emit_text("libctest.elf\n");
        }
        if (console_file_exists("/shell.elf") != 0) {
            console_emit_text("shell.elf\n");
        }
        if (console_file_exists("/uhello.elf") != 0) {
            console_emit_text("uhello.elf\n");
        }
        if (console_file_exists("/ucat.elf") != 0) {
            console_emit_text("ucat.elf\n");
        }
        if (console_file_exists("/uexec.elf") != 0) {
            console_emit_text("uexec.elf\n");
        }
        return;
    }

    if (console_text_equals_ci(path, "/etc") != 0U) {
        if (console_file_exists("/etc/motd.txt") != 0) {
            console_emit_text("motd.txt\n");
        }
        return;
    }

    if (console_text_equals_ci(path, "/fat") != 0U) {
        if (console_file_exists("/fat/HELLO.TXT") != 0) {
            console_emit_text("HELLO.TXT\n");
        }
        if (console_file_exists("/fat/DOCS/INFO.TXT") != 0) {
            console_emit_text("DOCS\n");
        }
        return;
    }

    if (console_text_equals_ci(path, "/fat/docs") != 0U) {
        if (console_file_exists("/fat/DOCS/INFO.TXT") != 0) {
            console_emit_text("INFO.TXT\n");
        }
        return;
    }

    console_emit_text("ls: unsupported path ");
    console_emit_text(path);
    console_emit_char('\n');
}

static void console_builtin_cat(const char *path)
{
    int32_t fd;
    char buffer[CONSOLE_IO_CHUNK];

    if (path == 0 || path[0] == '\0') {
        console_emit_text("cat: usage: cat <path>\n");
        return;
    }

    fd = vfs_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        console_emit_text("cat: cannot open ");
        console_emit_text(path);
        console_emit_char('\n');
        return;
    }

    for (;;) {
        int32_t nread = vfs_read(fd, buffer, sizeof(buffer));
        if (nread < 0) {
            console_emit_text("cat: read failed for ");
            console_emit_text(path);
            console_emit_char('\n');
            break;
        }

        if (nread == 0) {
            break;
        }

        console_emit_buffer(buffer, (uint32_t)nread);
    }

    (void)vfs_close(fd);
}

static void console_builtin_echo(uint32_t argc, char **argv)
{
    for (uint32_t i = 1U; i < argc; i++) {
        if (i > 1U) {
            console_emit_char(' ');
        }
        console_emit_text(argv[i]);
    }
    console_emit_char('\n');
}

static void console_builtin_clear(void)
{
    for (uint32_t i = 0U; i < CONSOLE_CLEAR_ROWS; i++) {
        console_emit_char('\n');
    }
}

static void console_builtin_ps(void)
{
    uint32_t count = process_count();

    console_emit_text("PID   STATE    NAME\n");
    console_emit_text("1   RUNNING  kernel-main\n");
    console_emit_text("[ps] total_processes=");

    {
        char digits[16];
        uint32_t value = count;
        uint32_t idx = 0U;

        if (value == 0U) {
            digits[idx++] = '0';
        } else {
            while (value != 0U && idx < sizeof(digits)) {
                digits[idx++] = (char)('0' + (value % 10U));
                value /= 10U;
            }
        }

        while (idx > 0U) {
            idx--;
            console_emit_char(digits[idx]);
        }
    }

    console_emit_char('\n');
}

static void console_builtin_help(void)
{
    console_emit_text("Builtins: ls cat echo clear help ps exit\n");
    console_emit_text("Commands: ring3test elftest forkexec libctest shell uhello ucat uexec appsdemo doom wmstart\n");
    console_emit_text("wmstart: GUI with terminal/calculator/uptime/checklist + dock (Esc exits)\n");
}

static uint32_t console_tokenize(char *line, char **argv, uint32_t argv_max)
{
    uint32_t argc = 0U;
    uint32_t i = 0U;

    while (line[i] != '\0') {
        while (line[i] == ' ' || line[i] == '\t') {
            line[i] = '\0';
            i++;
        }

        if (line[i] == '\0') {
            break;
        }

        if (argc >= argv_max) {
            break;
        }

        argv[argc++] = &line[i];

        while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t') {
            i++;
        }
    }

    return argc;
}

static void console_execute_tokens(uint32_t argc, char **argv)
{
    if (argc == 0U) {
        return;
    }

    if (console_text_equals_ci(argv[0], "help") != 0U) {
        console_builtin_help();
        serial_puts("[CONSOLE] help shown\n");
        return;
    }

    if (console_text_equals_ci(argv[0], "echo") != 0U) {
        console_builtin_echo(argc, argv);
        return;
    }

    if (console_text_equals_ci(argv[0], "clear") != 0U) {
        console_builtin_clear();
        return;
    }

    if (console_text_equals_ci(argv[0], "cat") != 0U) {
        if (argc < 2U) {
            console_emit_text("cat: usage: cat <path>\n");
            return;
        }
        console_builtin_cat(argv[1]);
        return;
    }

    if (console_text_equals_ci(argv[0], "ls") != 0U) {
        console_builtin_ls(argc, argv);
        return;
    }

    if (console_text_equals_ci(argv[0], "ps") != 0U) {
        console_builtin_ps();
        return;
    }

    if (console_text_equals_ci(argv[0], "exit") != 0U) {
        if (wm_is_active() != 0) {
            wm_stop();
            vga_clear();
            console_emit_text("Window manager stopped.\n");
            console_show_prompt();
        }
        return;
    }

    if (console_text_equals_ci(argv[0], "ring3test") != 0U) {
        usermode_run_ring3_test();
        return;
    }

    if (console_text_equals_ci(argv[0], "elftest") != 0U) {
        elf_run_embedded_test();
        return;
    }

    if (console_text_equals_ci(argv[0], "forkexec") != 0U) {
        elf_run_fork_exec_test();
        return;
    }

    if (console_text_equals_ci(argv[0], "libctest") != 0U) {
        elf_run_libc_test();
        return;
    }

    if (console_text_equals_ci(argv[0], "shell") != 0U) {
        if (wm_is_active() != 0) {
            console_emit_text("shell: not interactive in WM mode; use terminal builtins (ls/cat/echo/help).\n");
            return;
        }
        elf_run_shell();
        return;
    }

    if (console_text_equals_ci(argv[0], "uhello") != 0U) {
        elf_run_uhello();
        return;
    }

    if (console_text_equals_ci(argv[0], "ucat") != 0U) {
        elf_run_ucat();
        return;
    }

    if (console_text_equals_ci(argv[0], "uexec") != 0U) {
        elf_run_uexec();
        return;
    }

    if (console_text_equals_ci(argv[0], "appsdemo") != 0U) {
        elf_run_apps_demo();
        return;
    }

    if (console_text_equals_ci(argv[0], "doom") != 0U) {
        if (wm_is_active() != 0) {
            console_emit_text("doom: run outside WM mode (press Esc, then run doom from console).\n");
            return;
        }
        elf_run_doom();
        return;
    }

    if (console_text_equals_ci(argv[0], "wmstart") != 0U) {
        if (wm_start() == 0) {
            console_emit_text("Window manager started (drag title bars, use dock, press Esc to exit).\n");
        } else {
            console_emit_text("Window manager unavailable (needs framebuffer + mouse).\n");
        }
        return;
    }

    console_emit_text("unknown command: ");
    console_emit_text(argv[0]);
    console_emit_char('\n');
}

static void console_execute_command_internal(const char *command)
{
    char line_buf[CONSOLE_LINE_MAX];
    char *argv[CONSOLE_ARG_MAX];
    uint32_t argc;

    if (command == 0) {
        return;
    }

    console_copy_bounded(line_buf, sizeof(line_buf), command);
    argc = console_tokenize(line_buf, argv, CONSOLE_ARG_MAX);
    console_execute_tokens(argc, argv);
}

void console_init(void)
{
    console_len = 0;
    console_emit_text("\nConsole ready. Type and press Enter.\n");
    console_emit_text("Type 'help' for commands.\n");
    console_print_prompt();
    serial_puts("[CONSOLE] Initialized\n");
}

void console_show_prompt(void)
{
    console_print_prompt();
}

void console_handle_char(char c)
{
    if (c == '\r') {
        return;
    }

    if (c == '\n') {
        console_line[console_len] = '\0';
        console_emit_char('\n');

        serial_puts("[CONSOLE] ");
        serial_puts(console_line);
        serial_puts("\n");

        console_execute_command_internal(console_line);

        console_len = 0;
        console_print_prompt();
        return;
    }

    if (c == '\b') {
        if (console_len == 0u) {
            return;
        }

        console_len--;
        console_line[console_len] = '\0';
        console_emit_char('\b');
        return;
    }

    if (c == '\t') {
        c = ' ';
    }

    if ((c < 32) || (c > 126)) {
        return;
    }

    if (console_len >= (CONSOLE_LINE_MAX - 1u)) {
        return;
    }

    console_line[console_len] = c;
    console_len++;
    console_emit_char(c);
}

void console_execute_command(const char *command)
{
    console_execute_command_internal(command);
}

void console_set_output_hook(console_output_hook_fn hook, void *ctx)
{
    console_output_hook = hook;
    console_output_ctx = ctx;
}

void console_mirror_output(const char *text, uint32_t len)
{
    if (text == 0 || len == 0U || console_output_hook == 0) {
        return;
    }

    console_output_hook(text, len, console_output_ctx);
}
