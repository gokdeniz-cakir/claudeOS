#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

#define LIBCTEST_BUF_LEN  160U

int main(void)
{
    char *buf;
    FILE *fp;
    ssize_t nread;
    char status[64];

    printf("[LIBC] user C program started\n");

    buf = (char *)malloc(LIBCTEST_BUF_LEN);
    if (buf == 0) {
        printf("[LIBC] malloc failed\n");
        goto done;
    }

    buf = (char *)realloc(buf, LIBCTEST_BUF_LEN + 32U);
    if (buf == 0) {
        printf("[LIBC] realloc failed\n");
        goto done;
    }

    strcpy(buf, "[LIBC] malloc+string ready");
    printf("%s (len=%u)\n", buf, (unsigned)strlen(buf));

    (void)sprintf(status, "[LIBC] pid=%d", getpid());
    printf("%s\n", status);

    fp = fopen("/fat/HELLO.TXT", "rb");
    if (fp == 0) {
        printf("[LIBC] fopen failed\n");
        free(buf);
        goto done;
    }

    if (fseek(fp, 0L, SEEK_END) == 0) {
        long size = ftell(fp);
        if (size >= 0) {
            printf("[LIBC] file size=%u\n", (unsigned)size);
        }
        rewind(fp);
    }

    nread = (ssize_t)fread(buf, 1U, LIBCTEST_BUF_LEN - 1U, fp);
    if (nread > 0) {
        buf[(uint32_t)nread] = '\0';
        printf("[LIBC] read bytes=%u\n", (unsigned)nread);
        printf("%s", buf);
    } else {
        printf("[LIBC] fread failed\n");
    }

    (void)fclose(fp);
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
