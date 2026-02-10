#include "stdio.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "string.h"
#include "unistd.h"

#define CLAUDE_STDIO_MAX_FILES  16U
#define CLAUDE_FILE_PATH_MAX     128U
#define CLAUDE_FREAD_SKIP_CHUNK  128U

#define CLAUDE_FILE_READ_FLAG    0x01U
#define CLAUDE_FILE_WRITE_FLAG   0x02U
#define CLAUDE_FILE_STATIC_FLAG  0x80U

struct claude_file {
    int fd;
    uint8_t in_use;
    uint8_t flags;
    uint8_t eof;
    uint8_t error;
    uint32_t position;
    char path[CLAUDE_FILE_PATH_MAX];
};

FILE *stdin = 0;
FILE *stdout = 0;
FILE *stderr = 0;

static struct claude_file g_streams[CLAUDE_STDIO_MAX_FILES];
static uint8_t g_stdio_initialized = 0U;

static void stdio_copy_path(char *dst, const char *src)
{
    uint32_t i = 0U;

    if (dst == 0) {
        return;
    }

    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    while (i + 1U < CLAUDE_FILE_PATH_MAX && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void stdio_init_streams(void)
{
    uint32_t i;

    if (g_stdio_initialized != 0U) {
        return;
    }

    for (i = 0U; i < CLAUDE_STDIO_MAX_FILES; i++) {
        g_streams[i].fd = -1;
        g_streams[i].in_use = 0U;
        g_streams[i].flags = 0U;
        g_streams[i].eof = 0U;
        g_streams[i].error = 0U;
        g_streams[i].position = 0U;
        g_streams[i].path[0] = '\0';
    }

    g_streams[0].fd = 0;
    g_streams[0].in_use = 1U;
    g_streams[0].flags = CLAUDE_FILE_READ_FLAG | CLAUDE_FILE_STATIC_FLAG;

    g_streams[1].fd = 1;
    g_streams[1].in_use = 1U;
    g_streams[1].flags = CLAUDE_FILE_WRITE_FLAG | CLAUDE_FILE_STATIC_FLAG;

    g_streams[2].fd = 2;
    g_streams[2].in_use = 1U;
    g_streams[2].flags = CLAUDE_FILE_WRITE_FLAG | CLAUDE_FILE_STATIC_FLAG;

    stdin = &g_streams[0];
    stdout = &g_streams[1];
    stderr = &g_streams[2];

    g_stdio_initialized = 1U;
}

static uint32_t u32_to_base(char *out, uint32_t value, uint32_t base, uint8_t uppercase)
{
    static const char digits_lo[] = "0123456789abcdef";
    static const char digits_hi[] = "0123456789ABCDEF";
    const char *digits = uppercase != 0U ? digits_hi : digits_lo;
    char tmp[65];
    uint32_t len = 0U;
    uint32_t i;

    if (out == 0 || base < 2U || base > 16U) {
        return 0U;
    }

    if (value == 0U) {
        out[0] = '0';
        out[1] = '\0';
        return 1U;
    }

    while (value != 0U && len + 1U < sizeof(tmp)) {
        tmp[len++] = digits[value % base];
        value /= base;
    }

    for (i = 0U; i < len; i++) {
        out[i] = tmp[len - 1U - i];
    }
    out[len] = '\0';
    return len;
}

struct fmt_sink {
    int (*putc_fn)(void *ctx, char ch);
    void *ctx;
    int error;
    int count;
};

static void fmt_emit_char(struct fmt_sink *sink, char ch)
{
    if (sink == 0 || sink->putc_fn == 0) {
        return;
    }

    sink->count++;
    if (sink->error != 0) {
        return;
    }

    if (sink->putc_fn(sink->ctx, ch) != 0) {
        sink->error = 1;
    }
}

static void fmt_emit_repeat(struct fmt_sink *sink, char ch, uint32_t count)
{
    uint32_t i;
    for (i = 0U; i < count; i++) {
        fmt_emit_char(sink, ch);
    }
}

static void fmt_emit_bytes(struct fmt_sink *sink, const char *buf, uint32_t len)
{
    uint32_t i;
    if (buf == 0) {
        return;
    }

    for (i = 0U; i < len; i++) {
        fmt_emit_char(sink, buf[i]);
    }
}

struct fmt_spec {
    uint8_t left_align;
    uint8_t zero_pad;
    uint8_t uppercase;
    uint8_t length;      /* 0=default, 1=l, 2=ll, 3=z */
    uint32_t width;
    int32_t precision;   /* -1 means unspecified */
    char conv;
};

static uint8_t fmt_is_digit(char c)
{
    return (uint8_t)(c >= '0' && c <= '9');
}

static uint32_t fmt_parse_u32(const char *text, size_t *index)
{
    uint32_t value = 0U;
    size_t i = index != 0 ? *index : 0U;

    while (text != 0 && fmt_is_digit(text[i]) != 0U) {
        value = (value * 10U) + (uint32_t)(text[i] - '0');
        i++;
    }

    if (index != 0) {
        *index = i;
    }

    return value;
}

static void fmt_emit_formatted_string(struct fmt_sink *sink, const char *text,
                                      uint32_t width, int32_t precision,
                                      uint8_t left_align)
{
    uint32_t len;
    uint32_t pad = 0U;

    if (text == 0) {
        text = "(null)";
    }

    len = (uint32_t)strlen(text);
    if (precision >= 0 && len > (uint32_t)precision) {
        len = (uint32_t)precision;
    }

    if (width > len) {
        pad = width - len;
    }

    if (left_align == 0U) {
        fmt_emit_repeat(sink, ' ', pad);
    }
    fmt_emit_bytes(sink, text, len);
    if (left_align != 0U) {
        fmt_emit_repeat(sink, ' ', pad);
    }
}

static void fmt_emit_formatted_number(struct fmt_sink *sink,
                                      uint32_t magnitude,
                                      uint8_t negative,
                                      uint32_t base,
                                      struct fmt_spec *spec,
                                      uint8_t force_prefix)
{
    char digits[65];
    uint32_t digits_len;
    uint32_t zeros = 0U;
    uint32_t spaces = 0U;
    uint32_t prefix_len = 0U;
    char prefix[3];

    digits_len = u32_to_base(digits, magnitude, base, spec->uppercase);
    if (spec->precision == 0 && magnitude == 0U) {
        digits_len = 0U;
        digits[0] = '\0';
    }

    if (negative != 0U) {
        prefix[prefix_len++] = '-';
    }

    if (force_prefix != 0U) {
        prefix[prefix_len++] = '0';
        prefix[prefix_len++] = (char)(spec->uppercase != 0U ? 'X' : 'x');
    }

    if (spec->precision > 0 && (uint32_t)spec->precision > digits_len) {
        zeros = (uint32_t)spec->precision - digits_len;
    } else if (spec->precision < 0 && spec->zero_pad != 0U &&
               spec->left_align == 0U &&
               spec->width > prefix_len + digits_len) {
        zeros = spec->width - prefix_len - digits_len;
    }

    if (spec->width > prefix_len + zeros + digits_len) {
        spaces = spec->width - prefix_len - zeros - digits_len;
    }

    if (spec->left_align == 0U) {
        fmt_emit_repeat(sink, ' ', spaces);
    }

    fmt_emit_bytes(sink, prefix, prefix_len);
    fmt_emit_repeat(sink, '0', zeros);
    fmt_emit_bytes(sink, digits, digits_len);

    if (spec->left_align != 0U) {
        fmt_emit_repeat(sink, ' ', spaces);
    }
}

static int fmt_vformat(struct fmt_sink *sink, const char *fmt, va_list args)
{
    size_t i = 0U;

    if (sink == 0 || fmt == 0) {
        return -1;
    }

    while (fmt[i] != '\0') {
        struct fmt_spec spec;

        if (fmt[i] != '%') {
            fmt_emit_char(sink, fmt[i]);
            i++;
            continue;
        }

        i++;
        if (fmt[i] == '\0') {
            break;
        }

        spec.left_align = 0U;
        spec.zero_pad = 0U;
        spec.uppercase = 0U;
        spec.length = 0U;
        spec.width = 0U;
        spec.precision = -1;
        spec.conv = fmt[i];

        while (fmt[i] == '-' || fmt[i] == '0') {
            if (fmt[i] == '-') {
                spec.left_align = 1U;
            } else if (fmt[i] == '0') {
                spec.zero_pad = 1U;
            }
            i++;
        }

        if (fmt_is_digit(fmt[i]) != 0U) {
            spec.width = fmt_parse_u32(fmt, &i);
        }

        if (fmt[i] == '.') {
            i++;
            spec.precision = (int32_t)fmt_parse_u32(fmt, &i);
        }

        if (fmt[i] == 'l') {
            spec.length = 1U;
            i++;
            if (fmt[i] == 'l') {
                spec.length = 2U;
                i++;
            }
        } else if (fmt[i] == 'z') {
            spec.length = 3U;
            i++;
        }

        spec.conv = fmt[i];
        switch (spec.conv) {
            case '%':
                fmt_emit_char(sink, '%');
                break;
            case 'c': {
                char ch = (char)va_arg(args, int);
                uint32_t pad = 0U;
                if (spec.width > 1U) {
                    pad = spec.width - 1U;
                }

                if (spec.left_align == 0U) {
                    fmt_emit_repeat(sink, ' ', pad);
                }
                fmt_emit_char(sink, ch);
                if (spec.left_align != 0U) {
                    fmt_emit_repeat(sink, ' ', pad);
                }
                break;
            }
            case 's': {
                const char *text = va_arg(args, const char *);
                fmt_emit_formatted_string(sink, text, spec.width, spec.precision,
                                          spec.left_align);
                break;
            }
            case 'd':
            case 'i': {
                uint8_t negative = 0U;
                uint32_t magnitude;

                if (spec.length == 3U) {
                    ptrdiff_t value = va_arg(args, ptrdiff_t);
                    if (value < 0) {
                        negative = 1U;
                        magnitude = (uint32_t)(0U - (uint32_t)value);
                    } else {
                        magnitude = (uint32_t)value;
                    }
                } else if (spec.length >= 1U) {
                    long value = va_arg(args, long);
                    if (value < 0L) {
                        negative = 1U;
                        magnitude = (uint32_t)(0U - (uint32_t)value);
                    } else {
                        magnitude = (uint32_t)value;
                    }
                } else {
                    int value = va_arg(args, int);
                    if (value < 0) {
                        negative = 1U;
                        magnitude = (uint32_t)(0U - (uint32_t)value);
                    } else {
                        magnitude = (uint32_t)value;
                    }
                }

                fmt_emit_formatted_number(sink, magnitude, negative, 10U, &spec, 0U);
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'p': {
                uint32_t value;
                uint8_t add_prefix = 0U;
                uint32_t base = 10U;

                if (spec.conv == 'x' || spec.conv == 'X' || spec.conv == 'p') {
                    base = 16U;
                }

                if (spec.conv == 'X') {
                    spec.uppercase = 1U;
                }

                if (spec.conv == 'p') {
                    value = (uint32_t)(uintptr_t)va_arg(args, void *);
                    add_prefix = 1U;
                } else if (spec.length == 3U) {
                    value = (uint32_t)va_arg(args, size_t);
                } else if (spec.length >= 1U) {
                    value = (uint32_t)va_arg(args, unsigned long);
                } else {
                    value = (uint32_t)va_arg(args, unsigned int);
                }

                fmt_emit_formatted_number(sink, value, 0U, base, &spec, add_prefix);
                break;
            }
            default:
                fmt_emit_char(sink, '%');
                fmt_emit_char(sink, spec.conv);
                break;
        }

        if (fmt[i] != '\0') {
            i++;
        }
    }

    if (sink->error != 0) {
        return -1;
    }

    return sink->count;
}

struct fd_sink_ctx {
    int fd;
};

static int fd_sink_putc(void *ctx, char ch)
{
    struct fd_sink_ctx *fd_ctx = (struct fd_sink_ctx *)ctx;
    ssize_t rc;

    if (fd_ctx == 0 || fd_ctx->fd < 0) {
        return -1;
    }

    rc = write(fd_ctx->fd, &ch, 1U);
    return (rc == 1) ? 0 : -1;
}

struct str_sink_ctx {
    char *dst;
    size_t size;
    size_t pos;
};

static int str_sink_putc(void *ctx, char ch)
{
    struct str_sink_ctx *str_ctx = (struct str_sink_ctx *)ctx;

    if (str_ctx == 0) {
        return -1;
    }

    if (str_ctx->size != 0U && str_ctx->dst != 0 &&
        str_ctx->pos + 1U < str_ctx->size) {
        str_ctx->dst[str_ctx->pos] = ch;
    }

    str_ctx->pos++;
    return 0;
}

static int parse_open_mode(const char *mode, uint8_t *out_flags)
{
    uint8_t flags = 0U;
    uint32_t i;

    if (mode == 0 || out_flags == 0 || mode[0] == '\0') {
        return -1;
    }

    switch (mode[0]) {
        case 'r':
            flags |= CLAUDE_FILE_READ_FLAG;
            break;
        case 'w':
        case 'a':
            flags |= CLAUDE_FILE_WRITE_FLAG;
            break;
        default:
            return -1;
    }

    for (i = 1U; mode[i] != '\0'; i++) {
        if (mode[i] == '+') {
            flags |= CLAUDE_FILE_READ_FLAG | CLAUDE_FILE_WRITE_FLAG;
        } else if (mode[i] == 'b') {
            continue;
        } else {
            return -1;
        }
    }

    *out_flags = flags;
    return 0;
}

static uint32_t file_flags_to_open_flags(uint8_t flags)
{
    uint32_t open_flags = 0U;

    if ((flags & CLAUDE_FILE_READ_FLAG) != 0U) {
        open_flags |= O_READ;
    }
    if ((flags & CLAUDE_FILE_WRITE_FLAG) != 0U) {
        open_flags |= O_WRITE;
    }

    if (open_flags == 0U) {
        open_flags = O_READ;
    }

    return open_flags;
}

static int file_reopen(struct claude_file *stream)
{
    int new_fd;
    uint32_t open_flags;

    if (stream == 0 || stream->in_use == 0U || stream->path[0] == '\0') {
        return -1;
    }

    open_flags = file_flags_to_open_flags(stream->flags);
    new_fd = open(stream->path, open_flags);
    if (new_fd < 0) {
        stream->error = 1U;
        return -1;
    }

    if (close(stream->fd) < 0) {
        (void)close(new_fd);
        stream->error = 1U;
        return -1;
    }

    stream->fd = new_fd;
    stream->position = 0U;
    stream->eof = 0U;
    stream->error = 0U;
    return 0;
}

static int file_skip_forward(struct claude_file *stream, uint32_t target)
{
    char scratch[CLAUDE_FREAD_SKIP_CHUNK];

    while (stream->position < target) {
        uint32_t remaining = target - stream->position;
        uint32_t chunk = remaining;
        ssize_t rc;

        if (chunk > (uint32_t)sizeof(scratch)) {
            chunk = (uint32_t)sizeof(scratch);
        }

        rc = read(stream->fd, scratch, chunk);
        if (rc < 0) {
            stream->error = 1U;
            return -1;
        }
        if (rc == 0) {
            stream->eof = 1U;
            return -1;
        }

        stream->position += (uint32_t)rc;
    }

    stream->eof = 0U;
    return 0;
}

static int file_seek_abs(struct claude_file *stream, uint32_t target)
{
    if (stream->position == target) {
        stream->eof = 0U;
        stream->error = 0U;
        return 0;
    }

    if (target > stream->position) {
        return file_skip_forward(stream, target);
    }

    if (file_reopen(stream) != 0) {
        return -1;
    }

    return file_skip_forward(stream, target);
}

static int file_measure_size(struct claude_file *stream, uint32_t *out_size)
{
    uint32_t saved_pos;

    if (stream == 0 || out_size == 0) {
        return -1;
    }

    saved_pos = stream->position;
    if (file_reopen(stream) != 0) {
        return -1;
    }

    for (;;) {
        char scratch[CLAUDE_FREAD_SKIP_CHUNK];
        ssize_t rc = read(stream->fd, scratch, (uint32_t)sizeof(scratch));

        if (rc < 0) {
            stream->error = 1U;
            return -1;
        }

        if (rc == 0) {
            break;
        }

        stream->position += (uint32_t)rc;
    }

    *out_size = stream->position;

    if (file_reopen(stream) != 0) {
        return -1;
    }

    return file_skip_forward(stream, saved_pos);
}

int putchar(int c)
{
    char ch = (char)c;
    ssize_t rc;

    stdio_init_streams();
    rc = write(1, &ch, 1U);
    if (rc != 1) {
        return EOF;
    }

    return (unsigned char)ch;
}

int puts(const char *s)
{
    size_t len;

    stdio_init_streams();
    if (s == 0) {
        return EOF;
    }

    len = strlen(s);
    if (fwrite(s, 1U, len, stdout) != len) {
        return EOF;
    }
    if (putchar('\n') == EOF) {
        return EOF;
    }

    return (int)(len + 1U);
}

int vfprintf(FILE *stream, const char *fmt, va_list args)
{
    struct fd_sink_ctx fd_ctx;
    struct fmt_sink sink;

    stdio_init_streams();
    if (stream == 0 || stream->in_use == 0U || (stream->flags & CLAUDE_FILE_WRITE_FLAG) == 0U) {
        return -1;
    }

    fd_ctx.fd = stream->fd;
    sink.putc_fn = fd_sink_putc;
    sink.ctx = &fd_ctx;
    sink.error = 0;
    sink.count = 0;
    return fmt_vformat(&sink, fmt, args);
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vfprintf(stream, fmt, args);
    va_end(args);
    return rc;
}

int vprintf(const char *fmt, va_list args)
{
    stdio_init_streams();
    return vfprintf(stdout, fmt, args);
}

int printf(const char *fmt, ...)
{
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vprintf(fmt, args);
    va_end(args);
    return rc;
}

int vsnprintf(char *dst, size_t size, const char *fmt, va_list args)
{
    struct str_sink_ctx str_ctx;
    struct fmt_sink sink;
    int rc;

    if (fmt == 0 || (dst == 0 && size != 0U)) {
        return -1;
    }

    str_ctx.dst = dst;
    str_ctx.size = size;
    str_ctx.pos = 0U;

    sink.putc_fn = str_sink_putc;
    sink.ctx = &str_ctx;
    sink.error = 0;
    sink.count = 0;
    rc = fmt_vformat(&sink, fmt, args);

    if (dst != 0 && size != 0U) {
        if (str_ctx.pos < size) {
            dst[str_ctx.pos] = '\0';
        } else {
            dst[size - 1U] = '\0';
        }
    }

    return rc;
}

int snprintf(char *dst, size_t size, const char *fmt, ...)
{
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vsnprintf(dst, size, fmt, args);
    va_end(args);
    return rc;
}

int vsprintf(char *dst, const char *fmt, va_list args)
{
    return vsnprintf(dst, (size_t)-1, fmt, args);
}

int sprintf(char *dst, const char *fmt, ...)
{
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vsnprintf(dst, (size_t)-1, fmt, args);
    va_end(args);
    return rc;
}

FILE *fopen(const char *path, const char *mode)
{
    uint8_t flags;
    uint32_t open_flags;
    int fd;
    uint32_t i;

    stdio_init_streams();
    if (path == 0 || parse_open_mode(mode, &flags) != 0) {
        return 0;
    }

    open_flags = file_flags_to_open_flags(flags);
    fd = open(path, open_flags);
    if (fd < 0) {
        return 0;
    }

    for (i = 3U; i < CLAUDE_STDIO_MAX_FILES; i++) {
        if (g_streams[i].in_use == 0U) {
            g_streams[i].fd = fd;
            g_streams[i].in_use = 1U;
            g_streams[i].flags = flags;
            g_streams[i].eof = 0U;
            g_streams[i].error = 0U;
            g_streams[i].position = 0U;
            stdio_copy_path(g_streams[i].path, path);
            return &g_streams[i];
        }
    }

    (void)close(fd);
    return 0;
}

size_t fread(void *ptr, size_t size, size_t count, FILE *stream)
{
    size_t total;
    size_t done = 0U;
    char *dst = (char *)ptr;

    stdio_init_streams();
    if (stream == 0 || stream->in_use == 0U || ptr == 0 ||
        (stream->flags & CLAUDE_FILE_READ_FLAG) == 0U) {
        return 0U;
    }

    if (size == 0U || count == 0U) {
        return 0U;
    }

    if (count > ((size_t)-1) / size) {
        stream->error = 1U;
        return 0U;
    }

    total = size * count;
    while (done < total) {
        ssize_t rc = read(stream->fd, dst + done, (uint32_t)(total - done));
        if (rc < 0) {
            stream->error = 1U;
            break;
        }
        if (rc == 0) {
            stream->eof = 1U;
            break;
        }

        done += (size_t)rc;
        stream->position += (uint32_t)rc;
    }

    return done / size;
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
    size_t total;
    size_t done = 0U;
    const char *src = (const char *)ptr;

    stdio_init_streams();
    if (stream == 0 || stream->in_use == 0U || ptr == 0 ||
        (stream->flags & CLAUDE_FILE_WRITE_FLAG) == 0U) {
        return 0U;
    }

    if (size == 0U || count == 0U) {
        return 0U;
    }

    if (count > ((size_t)-1) / size) {
        stream->error = 1U;
        return 0U;
    }

    total = size * count;
    while (done < total) {
        ssize_t rc = write(stream->fd, src + done, (uint32_t)(total - done));
        if (rc <= 0) {
            stream->error = 1U;
            break;
        }

        done += (size_t)rc;
        stream->position += (uint32_t)rc;
    }

    return done / size;
}

int fflush(FILE *stream)
{
    stdio_init_streams();
    if (stream == 0) {
        return 0;
    }

    if (stream->in_use == 0U) {
        return -1;
    }

    return 0;
}

int fclose(FILE *stream)
{
    int rc;

    stdio_init_streams();
    if (stream == 0 || stream->in_use == 0U) {
        return -1;
    }

    if ((stream->flags & CLAUDE_FILE_STATIC_FLAG) != 0U) {
        return 0;
    }

    rc = close(stream->fd);
    if (rc < 0) {
        stream->error = 1U;
        return -1;
    }

    stream->fd = -1;
    stream->in_use = 0U;
    stream->flags = 0U;
    stream->eof = 0U;
    stream->error = 0U;
    stream->position = 0U;
    stream->path[0] = '\0';
    return 0;
}

int fseek(FILE *stream, long offset, int whence)
{
    int32_t base;
    int32_t target;

    stdio_init_streams();
    if (stream == 0 || stream->in_use == 0U) {
        return -1;
    }

    if (whence == SEEK_SET) {
        base = 0;
    } else if (whence == SEEK_CUR) {
        base = (int32_t)stream->position;
    } else if (whence == SEEK_END) {
        uint32_t size;
        if ((stream->flags & CLAUDE_FILE_READ_FLAG) == 0U ||
            file_measure_size(stream, &size) != 0) {
            stream->error = 1U;
            return -1;
        }
        base = (int32_t)size;
    } else {
        return -1;
    }

    if (offset > 0L && base > (0x7FFFFFFF - (int32_t)offset)) {
        stream->error = 1U;
        return -1;
    }

    if (offset < 0L) {
        int32_t neg = (int32_t)offset;
        if (neg == (int32_t)0x80000000) {
            stream->error = 1U;
            return -1;
        }
        if (base < -neg) {
            stream->error = 1U;
            return -1;
        }
    }

    target = base + (int32_t)offset;
    if (target < 0) {
        stream->error = 1U;
        return -1;
    }

    if (file_seek_abs(stream, (uint32_t)target) != 0) {
        return -1;
    }

    stream->eof = 0U;
    return 0;
}

long ftell(FILE *stream)
{
    stdio_init_streams();
    if (stream == 0 || stream->in_use == 0U) {
        return -1L;
    }

    return (long)stream->position;
}

void rewind(FILE *stream)
{
    (void)fseek(stream, 0L, SEEK_SET);
    if (stream != 0) {
        stream->eof = 0U;
        stream->error = 0U;
    }
}

int feof(FILE *stream)
{
    stdio_init_streams();
    if (stream == 0 || stream->in_use == 0U) {
        return 0;
    }

    return (int)stream->eof;
}

int ferror(FILE *stream)
{
    stdio_init_streams();
    if (stream == 0 || stream->in_use == 0U) {
        return 0;
    }

    return (int)stream->error;
}

int remove(const char *path)
{
    (void)path;
    return -1;
}

int rename(const char *old_path, const char *new_path)
{
    (void)old_path;
    (void)new_path;
    return -1;
}
