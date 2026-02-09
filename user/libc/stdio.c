#include "stdio.h"

#include <stdarg.h>
#include <stdint.h>

#include "string.h"
#include "unistd.h"

static int write_all(const char *buf, uint32_t len)
{
    uint32_t total = 0U;

    while (total < len) {
        ssize_t rc = write(1, buf + total, len - total);
        if (rc <= 0) {
            return -1;
        }

        total += (uint32_t)rc;
    }

    return (int)total;
}

static uint32_t u32_to_base(char *out, uint32_t value, uint32_t base)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[32];
    uint32_t len = 0U;
    uint32_t i;

    if (base < 2U || base > 16U) {
        out[0] = '0';
        out[1] = '\0';
        return 1U;
    }

    if (value == 0U) {
        out[0] = '0';
        out[1] = '\0';
        return 1U;
    }

    while (value != 0U) {
        tmp[len++] = digits[value % base];
        value /= base;
    }

    for (i = 0U; i < len; i++) {
        out[i] = tmp[len - 1U - i];
    }
    out[len] = '\0';
    return len;
}

int putchar(int c)
{
    char ch = (char)c;
    if (write_all(&ch, 1U) < 0) {
        return -1;
    }
    return (unsigned char)ch;
}

int puts(const char *s)
{
    int count = 0;
    size_t len = strlen(s);

    if (write_all(s, (uint32_t)len) < 0) {
        return -1;
    }
    count += (int)len;

    if (putchar('\n') < 0) {
        return -1;
    }
    count++;

    return count;
}

int printf(const char *fmt, ...)
{
    va_list args;
    int written = 0;
    size_t i = 0U;

    if (fmt == 0) {
        return -1;
    }

    va_start(args, fmt);
    while (fmt[i] != '\0') {
        char num_buf[34];
        int rc;

        if (fmt[i] != '%') {
            rc = putchar(fmt[i]);
            if (rc < 0) {
                va_end(args);
                return -1;
            }
            written++;
            i++;
            continue;
        }

        i++;
        if (fmt[i] == '\0') {
            break;
        }

        switch (fmt[i]) {
            case '%': {
                rc = putchar('%');
                if (rc < 0) {
                    va_end(args);
                    return -1;
                }
                written++;
                break;
            }
            case 'c': {
                rc = putchar(va_arg(args, int));
                if (rc < 0) {
                    va_end(args);
                    return -1;
                }
                written++;
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                size_t len;

                if (s == 0) {
                    s = "(null)";
                }
                len = strlen(s);
                if (write_all(s, (uint32_t)len) < 0) {
                    va_end(args);
                    return -1;
                }
                written += (int)len;
                break;
            }
            case 'd': {
                int32_t v = va_arg(args, int32_t);
                uint32_t len = 0U;
                uint32_t mag;

                if (v < 0) {
                    if (putchar('-') < 0) {
                        va_end(args);
                        return -1;
                    }
                    written++;
                    mag = (uint32_t)(0U - (uint32_t)v);
                } else {
                    mag = (uint32_t)v;
                }

                len = u32_to_base(num_buf, mag, 10U);
                if (write_all(num_buf, len) < 0) {
                    va_end(args);
                    return -1;
                }
                written += (int)len;
                break;
            }
            case 'u': {
                uint32_t len = u32_to_base(num_buf, va_arg(args, uint32_t), 10U);
                if (write_all(num_buf, len) < 0) {
                    va_end(args);
                    return -1;
                }
                written += (int)len;
                break;
            }
            case 'x': {
                uint32_t len = u32_to_base(num_buf, va_arg(args, uint32_t), 16U);
                if (write_all(num_buf, len) < 0) {
                    va_end(args);
                    return -1;
                }
                written += (int)len;
                break;
            }
            default: {
                if (putchar('%') < 0 || putchar(fmt[i]) < 0) {
                    va_end(args);
                    return -1;
                }
                written += 2;
                break;
            }
        }

        i++;
    }
    va_end(args);

    return written;
}
