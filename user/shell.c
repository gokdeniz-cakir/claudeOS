#include "stdio.h"
#include "string.h"
#include "unistd.h"

#define SHELL_IO_CHUNK   128U
#define SHELL_MAX_LINE   128U
#define SHELL_MAX_ARGS   8U
#define SHELL_CLEAR_ROWS 30U

static int shell_streq(const char *a, const char *b)
{
    return (strcmp(a, b) == 0);
}

static uint32_t shell_copy_line(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t i = 0U;

    if (dst == 0 || src == 0 || dst_size == 0U) {
        return 0U;
    }

    while (i + 1U < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
    return i;
}

static uint32_t shell_tokenize(char *line, char **argv, uint32_t argv_max)
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

        argv[argc] = &line[i];
        argc++;

        while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t') {
            i++;
        }
    }

    return argc;
}

static void shell_cat_file(const char *path)
{
    char buffer[SHELL_IO_CHUNK];
    int fd;

    fd = open(path, O_READ);
    if (fd < 0) {
        printf("cat: cannot open %s\n", path);
        return;
    }

    for (;;) {
        ssize_t nread = read(fd, buffer, SHELL_IO_CHUNK);
        if (nread < 0) {
            printf("cat: read failed for %s\n", path);
            break;
        }

        if (nread == 0) {
            break;
        }

        (void)write(1, buffer, (uint32_t)nread);
    }

    (void)close(fd);
}

static int shell_file_exists(const char *path)
{
    int fd = open(path, O_READ);
    if (fd < 0) {
        return 0;
    }

    (void)close(fd);
    return 1;
}

static void shell_print_ls_root(void)
{
    puts("etc");
    puts("fat");

    if (shell_file_exists("/hello.txt") != 0) {
        puts("hello.txt");
    }
    if (shell_file_exists("/elf_demo.elf") != 0) {
        puts("elf_demo.elf");
    }
    if (shell_file_exists("/libctest.elf") != 0) {
        puts("libctest.elf");
    }
    if (shell_file_exists("/shell.elf") != 0) {
        puts("shell.elf");
    }
    if (shell_file_exists("/uhello.elf") != 0) {
        puts("uhello.elf");
    }
    if (shell_file_exists("/ucat.elf") != 0) {
        puts("ucat.elf");
    }
    if (shell_file_exists("/uexec.elf") != 0) {
        puts("uexec.elf");
    }
}

static void shell_print_ls_etc(void)
{
    if (shell_file_exists("/etc/motd.txt") != 0) {
        puts("motd.txt");
    }
}

static void shell_print_ls_fat(void)
{
    if (shell_file_exists("/fat/HELLO.TXT") != 0) {
        puts("HELLO.TXT");
    }
    if (shell_file_exists("/fat/DOCS/INFO.TXT") != 0) {
        puts("DOCS");
    }
}

static void shell_print_ls_fat_docs(void)
{
    if (shell_file_exists("/fat/DOCS/INFO.TXT") != 0) {
        puts("INFO.TXT");
    }
}

static void shell_builtin_help(void)
{
    puts("builtins: ls cat echo clear help ps exit");
}

static void shell_builtin_echo(uint32_t argc, char **argv)
{
    uint32_t i;

    for (i = 1U; i < argc; i++) {
        if (i > 1U) {
            putchar(' ');
        }
        printf("%s", argv[i]);
    }
    putchar('\n');
}

static void shell_builtin_clear(void)
{
    uint32_t i;
    for (i = 0U; i < SHELL_CLEAR_ROWS; i++) {
        putchar('\n');
    }
}

static void shell_builtin_ps(void)
{
    int pid = getpid();
    int total = proc_count();

    puts("PID   STATE    NAME");
    printf("%d   RUNNING  claudesh\n", pid);
    printf("[ps] total_processes=%d\n", total);
}

static void shell_builtin_ls(uint32_t argc, char **argv)
{
    const char *path = (argc > 1U) ? argv[1] : "/";

    if (shell_streq(path, "/") != 0) {
        shell_print_ls_root();
        return;
    }

    if (shell_streq(path, "/etc") != 0) {
        shell_print_ls_etc();
        return;
    }

    if (shell_streq(path, "/fat") != 0) {
        shell_print_ls_fat();
        return;
    }

    if (shell_streq(path, "/fat/DOCS") != 0 || shell_streq(path, "/fat/docs") != 0) {
        shell_print_ls_fat_docs();
        return;
    }

    printf("ls: unsupported path %s\n", path);
}

static int shell_execute(uint32_t argc, char **argv)
{
    if (argc == 0U) {
        return 0;
    }

    if (shell_streq(argv[0], "help") != 0) {
        shell_builtin_help();
        return 0;
    }

    if (shell_streq(argv[0], "echo") != 0) {
        shell_builtin_echo(argc, argv);
        return 0;
    }

    if (shell_streq(argv[0], "clear") != 0) {
        shell_builtin_clear();
        return 0;
    }

    if (shell_streq(argv[0], "cat") != 0) {
        if (argc < 2U) {
            puts("cat: usage: cat <path>");
            return 0;
        }

        shell_cat_file(argv[1]);
        return 0;
    }

    if (shell_streq(argv[0], "ls") != 0) {
        shell_builtin_ls(argc, argv);
        return 0;
    }

    if (shell_streq(argv[0], "ps") != 0) {
        shell_builtin_ps();
        return 0;
    }

    if (shell_streq(argv[0], "exit") != 0) {
        return 1;
    }

    printf("unknown command: %s\n", argv[0]);
    return 0;
}

static int shell_run_script_line(const char *line_text)
{
    char line[SHELL_MAX_LINE];
    char *argv[SHELL_MAX_ARGS];
    uint32_t argc;

    (void)shell_copy_line(line, sizeof(line), line_text);
    printf("claudesh$ %s\n", line);
    argc = shell_tokenize(line, argv, SHELL_MAX_ARGS);
    return shell_execute(argc, argv);
}

int main(void)
{
    static const char *script_lines[] = {
        "help",
        "echo ClaudeOS userspace shell builtins ready",
        "ls /",
        "cat /hello.txt",
        "ls /fat",
        "cat /fat/HELLO.TXT",
        "ps",
        "clear",
        "help",
        "exit"
    };
    uint32_t i;

    puts("[SHELL] ClaudeOS userspace shell started");
    puts("[SHELL] builtins are currently script-driven (stdin syscall pending)");

    for (i = 0U; i < (uint32_t)(sizeof(script_lines) / sizeof(script_lines[0])); i++) {
        if (shell_run_script_line(script_lines[i]) != 0) {
            break;
        }
    }

    exit(0);
}
