#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include <stdint.h>

#define LIBCTEST_BUF_LEN  160U
#define LIBCTEST_HEAP_PROBE_BYTES  (20U * 1024U * 1024U)
#define LIBCTEST_HEAP_PROBE_STEP   4096U

int main(void)
{
    char *buf;
    FILE *fp;
    ssize_t nread;
    char status[64];
    void *heap_probe;
    uint32_t offset;

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
    buf = 0;

    heap_probe = sbrk((int32_t)LIBCTEST_HEAP_PROBE_BYTES);
    if (heap_probe == (void *)0xFFFFFFFFU) {
        printf("[LIBC] sbrk 20MiB failed\n");
        goto done;
    }

    for (offset = 0U; offset < LIBCTEST_HEAP_PROBE_BYTES;
         offset += LIBCTEST_HEAP_PROBE_STEP) {
        ((volatile uint8_t *)heap_probe)[offset] = (uint8_t)(offset / LIBCTEST_HEAP_PROBE_STEP);
    }
    printf("[LIBC] sbrk 20MiB ok\n");

    if (sbrk(-((int32_t)LIBCTEST_HEAP_PROBE_BYTES)) == (void *)0xFFFFFFFFU) {
        printf("[LIBC] sbrk 20MiB release failed\n");
    }

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
