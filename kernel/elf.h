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

/* Load an ELF32 executable from VFS path into user virtual memory. */
int elf_load_user_image_from_vfs(const char *path, struct elf_user_image *loaded);

/* Drop ELF loader bookkeeping for an address space being destroyed. */
void elf_forget_address_space(uint32_t cr3_phys);

/* Load and run the embedded demo ELF binary in ring 3. */
void elf_run_embedded_test(void);

/* Run the embedded fork+exec probe binary in ring 3. */
void elf_run_fork_exec_test(void);

/* Load libc smoke test ELF from initrd and run it in ring 3. */
void elf_run_libc_test(void);

/* Spawn userspace shell process from initrd ELF image. */
void elf_run_shell(void);

/* Spawn standalone userspace demos from initrd ELF images. */
void elf_run_uhello(void);
void elf_run_ucat(void);
void elf_run_uexec(void);
void elf_run_doom(void);
void elf_run_apps_demo(void);

#endif /* CLAUDE_ELF_H */
