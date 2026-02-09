#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

#define LIBCTEST_BUF_LEN  160U

int main(void)
{
    char *buf;
    int fd;
    ssize_t nread;

    printf("[LIBC] user C program started\n");

    buf = (char *)malloc(LIBCTEST_BUF_LEN);
    if (buf == 0) {
        printf("[LIBC] malloc failed\n");
        goto done;
    }

    strcpy(buf, "[LIBC] malloc+string ready");
    printf("%s (len=%u)\n", buf, (unsigned)strlen(buf));

    fd = open("/fat/HELLO.TXT", O_READ);
    if (fd < 0) {
        printf("[LIBC] open failed\n");
        free(buf);
        goto done;
    }

    nread = read(fd, buf, LIBCTEST_BUF_LEN - 1U);
    if (nread > 0) {
        buf[(uint32_t)nread] = '\0';
        printf("[LIBC] read bytes=%u\n", (unsigned)nread);
        printf("%s", buf);
    } else {
        printf("[LIBC] read failed\n");
    }

    (void)close(fd);
    free(buf);

done:
    exit(0);

    /*
     * Current console command path reuses bootstrap pid=1 where exit() is
     * rejected; trigger a deterministic ring-3 fault in that specific case.
     */
    __asm__ volatile ("cli");
    for (;;) {
    }
}
