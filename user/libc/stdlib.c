#include "stdlib.h"

#include "ctype.h"

static const char *skip_space(const char *s)
{
    if (s == 0) {
        return 0;
    }

    while (*s != '\0' && isspace((unsigned char)*s) != 0) {
        s++;
    }

    return s;
}

long atol(const char *str)
{
    const char *p = skip_space(str);
    long sign = 1;
    long value = 0;

    if (p == 0) {
        return 0;
    }

    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    while (*p != '\0' && isdigit((unsigned char)*p) != 0) {
        value = (value * 10L) + (long)(*p - '0');
        p++;
    }

    return sign * value;
}

int atoi(const char *str)
{
    return (int)atol(str);
}

double atof(const char *str)
{
    const char *p = skip_space(str);
    double sign = 1.0;
    double integer_part = 0.0;
    double fractional_part = 0.0;
    double scale = 1.0;
    int exponent_sign = 1;
    long exponent_value = 0;

    if (p == 0) {
        return 0.0;
    }

    if (*p == '-') {
        sign = -1.0;
        p++;
    } else if (*p == '+') {
        p++;
    }

    while (*p != '\0' && isdigit((unsigned char)*p) != 0) {
        integer_part = integer_part * 10.0 + (double)(*p - '0');
        p++;
    }

    if (*p == '.') {
        p++;
        while (*p != '\0' && isdigit((unsigned char)*p) != 0) {
            fractional_part = fractional_part * 10.0 + (double)(*p - '0');
            scale *= 10.0;
            p++;
        }
    }

    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '-') {
            exponent_sign = -1;
            p++;
        } else if (*p == '+') {
            p++;
        }

        while (*p != '\0' && isdigit((unsigned char)*p) != 0) {
            exponent_value = exponent_value * 10L + (long)(*p - '0');
            p++;
        }
    }

    {
        double value = sign * (integer_part + (fractional_part / scale));
        long i;

        if (exponent_sign > 0) {
            for (i = 0; i < exponent_value; i++) {
                value *= 10.0;
            }
        } else {
            for (i = 0; i < exponent_value; i++) {
                value /= 10.0;
            }
        }

        return value;
    }
}

int abs(int value)
{
    if (value < 0) {
        return -value;
    }

    return value;
}

long labs(long value)
{
    if (value < 0L) {
        return -value;
    }

    return value;
}

char *getenv(const char *name)
{
    (void)name;
    return 0;
}

int system(const char *command)
{
    (void)command;
    return -1;
}
