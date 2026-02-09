#ifndef CLAUDE_INITRD_H
#define CLAUDE_INITRD_H

#include <stdint.h>

/* Initialize and mount embedded tar-based initrd on the VFS root. */
int32_t initrd_init(void);

#endif /* CLAUDE_INITRD_H */
