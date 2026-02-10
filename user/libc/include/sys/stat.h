#ifndef CLAUDE_USER_LIBC_SYS_STAT_H
#define CLAUDE_USER_LIBC_SYS_STAT_H

#include "sys/types.h"

struct stat {
    uint32_t st_mode;
    uint32_t st_size;
};

#endif /* CLAUDE_USER_LIBC_SYS_STAT_H */
