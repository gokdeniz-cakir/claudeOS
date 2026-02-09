#ifndef CLAUDE_ELF_H
#define CLAUDE_ELF_H

#include <stdint.h>

struct elf_user_image {
    uint32_t entry;
    uint32_t stack_top;
};

/* Load an in-memory ELF32 executable into user virtual memory. */
int elf_load_user_image(const uint8_t *image, uint32_t image_size,
                        struct elf_user_image *loaded);

/* Load and run the embedded demo ELF binary in ring 3. */
void elf_run_embedded_test(void);

#endif /* CLAUDE_ELF_H */
