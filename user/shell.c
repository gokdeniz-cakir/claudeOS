#include "stdio.h"
#include "unistd.h"

#define SHELL_IO_CHUNK  128U

static void shell_cat_file(const char *path)
{
    char buffer[SHELL_IO_CHUNK];
    int fd;

    fd = open(path, O_READ);
    if (fd < 0) {
        printf("[SHELL] open failed: %s\n", path);
        return;
    }

    for (;;) {
        ssize_t nread = read(fd, buffer, SHELL_IO_CHUNK);
        if (nread < 0) {
            printf("[SHELL] read failed: %s\n", path);
            break;
        }

        if (nread == 0) {
            break;
        }

        (void)write(1, buffer, (uint32_t)nread);
    }

    (void)close(fd);
}

int main(void)
{
    printf("[SHELL] ClaudeOS userspace shell started\n");
    printf("[SHELL] note: interactive stdin is pending (Task 32)\n");
    printf("claudesh$ help\n");
    printf("builtins planned: ls cat echo clear help ps\n");
    printf("claudesh$ cat /fat/HELLO.TXT\n");
    shell_cat_file("/fat/HELLO.TXT");
    printf("claudesh$ cat /etc/motd.txt\n");
    shell_cat_file("/etc/motd.txt");
    printf("claudesh$ exit\n");

    exit(0);
}
