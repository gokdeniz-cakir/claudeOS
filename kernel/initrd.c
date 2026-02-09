#include "initrd.h"

#include <stdint.h>

#include "serial.h"
#include "vfs.h"

#define INITRD_MAX_ENTRIES    64U
#define TAR_BLOCK_SIZE        512U

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __attribute__((packed));

struct initrd_entry {
    uint8_t in_use;
    uint32_t type;
    int32_t parent;
    char path[VFS_PATH_MAX];
    char name[VFS_NAME_MAX];
    uint32_t size;
    const uint8_t *data;
};

static struct initrd_entry initrd_entries[INITRD_MAX_ENTRIES];
static uint32_t initrd_entry_count = 0U;

extern const uint8_t _binary_build_initrd_tar_start[];
extern const uint8_t _binary_build_initrd_tar_end[];

static int32_t initrd_lookup(const struct vfs_node *dir, const char *name,
                             struct vfs_node *out_node);
static int32_t initrd_read(const struct vfs_node *node, uint32_t offset,
                           uint8_t *buffer, uint32_t size);
static int32_t initrd_write(const struct vfs_node *node, uint32_t offset,
                            const uint8_t *buffer, uint32_t size);

static const struct vfs_node_ops initrd_dir_ops = {
    .lookup = initrd_lookup,
    .read = 0,
    .write = 0
};

static const struct vfs_node_ops initrd_file_ops = {
    .lookup = 0,
    .read = initrd_read,
    .write = initrd_write
};

static uint32_t str_len(const char *s)
{
    uint32_t len = 0U;

    if (s == 0) {
        return 0U;
    }

    while (s[len] != '\0') {
        len++;
    }

    return len;
}

static uint8_t str_equal(const char *a, const char *b)
{
    uint32_t i = 0U;

    if (a == 0 || b == 0) {
        return 0U;
    }

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0U;
        }
        i++;
    }

    return (uint8_t)(a[i] == '\0' && b[i] == '\0');
}

static void str_copy(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t i = 0U;

    if (dst == 0 || dst_size == 0U) {
        return;
    }

    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    while (i + 1U < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static uint32_t align_up_u32(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static uint32_t add_overflow_u32(uint32_t a, uint32_t b, uint32_t *sum)
{
    if (sum == 0 || a > (0xFFFFFFFFU - b)) {
        return 1U;
    }

    *sum = a + b;
    return 0U;
}

static void serial_put_u32(uint32_t value)
{
    char buffer[11];
    uint32_t i = 0U;

    if (value == 0U) {
        serial_putchar('0');
        return;
    }

    while (value != 0U && i < (uint32_t)sizeof(buffer)) {
        buffer[i] = (char)('0' + (value % 10U));
        value /= 10U;
        i++;
    }

    while (i > 0U) {
        i--;
        serial_putchar(buffer[i]);
    }
}

static uint8_t is_zero_block(const uint8_t *block)
{
    uint32_t i;

    if (block == 0) {
        return 0U;
    }

    for (i = 0U; i < TAR_BLOCK_SIZE; i++) {
        if (block[i] != 0U) {
            return 0U;
        }
    }

    return 1U;
}

static uint32_t tar_field_len(const char *field, uint32_t max_len)
{
    uint32_t i = 0U;

    while (i < max_len && field[i] != '\0') {
        i++;
    }

    return i;
}

static int32_t parse_octal_field(const char *field, uint32_t field_len,
                                 uint32_t *value_out)
{
    uint32_t value = 0U;
    uint32_t i = 0U;

    if (field == 0 || value_out == 0) {
        return -1;
    }

    while (i < field_len && (field[i] == ' ' || field[i] == '\0')) {
        i++;
    }

    while (i < field_len && field[i] != '\0' && field[i] != ' ') {
        if (field[i] < '0' || field[i] > '7') {
            return -1;
        }

        if (value > 0x1FFFFFFFU) {
            return -1;
        }

        value = (value << 3U) + (uint32_t)(field[i] - '0');
        i++;
    }

    *value_out = value;
    return 0;
}

static int32_t build_tar_raw_path(const struct tar_header *header, char *out,
                                  uint32_t out_size)
{
    uint32_t i;
    uint32_t out_len = 0U;
    uint32_t prefix_len;
    uint32_t name_len;

    if (header == 0 || out == 0 || out_size < 2U) {
        return -1;
    }

    prefix_len = tar_field_len(header->prefix, sizeof(header->prefix));
    name_len = tar_field_len(header->name, sizeof(header->name));

    if (name_len == 0U) {
        return -1;
    }

    if (prefix_len > 0U) {
        for (i = 0U; i < prefix_len; i++) {
            if (out_len + 1U >= out_size) {
                return -1;
            }
            out[out_len++] = header->prefix[i];
        }

        if (out_len + 1U >= out_size) {
            return -1;
        }

        out[out_len++] = '/';
    }

    for (i = 0U; i < name_len; i++) {
        if (out_len + 1U >= out_size) {
            return -1;
        }
        out[out_len++] = header->name[i];
    }

    out[out_len] = '\0';
    return 0;
}

static int32_t canonicalize_archive_path(const char *raw_path, char *out_path,
                                         uint32_t out_size)
{
    uint32_t src = 0U;
    uint32_t dst = 1U;

    if (raw_path == 0 || out_path == 0 || out_size < 2U) {
        return -1;
    }

    out_path[0] = '/';

    while (raw_path[src] != '\0') {
        uint32_t comp_len = 0U;
        uint32_t i;

        while (raw_path[src] == '/') {
            src++;
        }

        if (raw_path[src] == '\0') {
            break;
        }

        while (raw_path[src + comp_len] != '\0' &&
               raw_path[src + comp_len] != '/') {
            comp_len++;
        }

        if (comp_len == 1U && raw_path[src] == '.') {
            src += comp_len;
            continue;
        }

        if (comp_len == 2U && raw_path[src] == '.' &&
            raw_path[src + 1U] == '.') {
            return -1;
        }

        if (comp_len >= VFS_NAME_MAX) {
            return -1;
        }

        if (dst != 1U) {
            if (dst + 1U >= out_size) {
                return -1;
            }
            out_path[dst++] = '/';
        }

        if (dst + comp_len >= out_size) {
            return -1;
        }

        for (i = 0U; i < comp_len; i++) {
            out_path[dst++] = raw_path[src + i];
        }

        src += comp_len;
    }

    out_path[dst] = '\0';
    return 0;
}

static int32_t split_parent_name(const char *path, char *parent_out,
                                 uint32_t parent_out_size, char *name_out,
                                 uint32_t name_out_size)
{
    uint32_t len;
    uint32_t i;
    uint32_t last_sep = 0U;

    if (path == 0 || parent_out == 0 || name_out == 0 ||
        parent_out_size < 2U || name_out_size < 2U) {
        return -1;
    }

    len = str_len(path);
    if (len == 0U || path[0] != '/') {
        return -1;
    }

    if (str_equal(path, "/") != 0U) {
        str_copy(parent_out, parent_out_size, "/");
        str_copy(name_out, name_out_size, "/");
        return 0;
    }

    for (i = 1U; i < len; i++) {
        if (path[i] == '/') {
            last_sep = i;
        }
    }

    if (last_sep == 0U) {
        if (parent_out_size < 2U || len >= name_out_size) {
            return -1;
        }

        parent_out[0] = '/';
        parent_out[1] = '\0';

        for (i = 1U; i < len; i++) {
            name_out[i - 1U] = path[i];
        }
        name_out[len - 1U] = '\0';
        return 0;
    }

    if (last_sep >= parent_out_size || (len - last_sep) >= name_out_size) {
        return -1;
    }

    for (i = 0U; i < last_sep; i++) {
        parent_out[i] = path[i];
    }
    parent_out[last_sep] = '\0';

    for (i = last_sep + 1U; i < len; i++) {
        name_out[i - last_sep - 1U] = path[i];
    }
    name_out[len - last_sep - 1U] = '\0';

    return 0;
}

static int32_t find_entry_by_path(const char *path)
{
    uint32_t i;

    if (path == 0) {
        return -1;
    }

    for (i = 0U; i < INITRD_MAX_ENTRIES; i++) {
        if (initrd_entries[i].in_use != 0U &&
            str_equal(initrd_entries[i].path, path) != 0U) {
            return (int32_t)i;
        }
    }

    return -1;
}

static int32_t alloc_entry_slot(void)
{
    uint32_t i;

    for (i = 0U; i < INITRD_MAX_ENTRIES; i++) {
        if (initrd_entries[i].in_use == 0U) {
            return (int32_t)i;
        }
    }

    return -1;
}

static int32_t ensure_directory(const char *dir_path)
{
    int32_t existing;
    char parent_path[VFS_PATH_MAX];
    char name[VFS_NAME_MAX];
    int32_t parent_idx;
    int32_t slot;

    if (dir_path == 0 || dir_path[0] != '/') {
        return -1;
    }

    existing = find_entry_by_path(dir_path);
    if (existing >= 0) {
        if (initrd_entries[(uint32_t)existing].type != VFS_NODE_DIRECTORY) {
            return -1;
        }
        return existing;
    }

    if (split_parent_name(dir_path, parent_path, sizeof(parent_path), name,
                          sizeof(name)) != 0) {
        return -1;
    }

    parent_idx = ensure_directory(parent_path);
    if (parent_idx < 0) {
        return -1;
    }

    slot = alloc_entry_slot();
    if (slot < 0) {
        return -1;
    }

    initrd_entries[(uint32_t)slot].in_use = 1U;
    initrd_entries[(uint32_t)slot].type = VFS_NODE_DIRECTORY;
    initrd_entries[(uint32_t)slot].parent = parent_idx;
    str_copy(initrd_entries[(uint32_t)slot].path, sizeof(initrd_entries[slot].path),
             dir_path);
    str_copy(initrd_entries[(uint32_t)slot].name, sizeof(initrd_entries[slot].name),
             name);
    initrd_entries[(uint32_t)slot].size = 0U;
    initrd_entries[(uint32_t)slot].data = 0;
    initrd_entry_count++;
    return slot;
}

static int32_t add_file_entry(const char *file_path, uint32_t size,
                              const uint8_t *data)
{
    int32_t existing;
    char parent_path[VFS_PATH_MAX];
    char name[VFS_NAME_MAX];
    int32_t parent_idx;
    int32_t slot;

    if (file_path == 0 || file_path[0] != '/' || data == 0) {
        return -1;
    }

    if (split_parent_name(file_path, parent_path, sizeof(parent_path), name,
                          sizeof(name)) != 0) {
        return -1;
    }

    parent_idx = ensure_directory(parent_path);
    if (parent_idx < 0) {
        return -1;
    }

    existing = find_entry_by_path(file_path);
    if (existing >= 0) {
        if (initrd_entries[(uint32_t)existing].type != VFS_NODE_FILE) {
            return -1;
        }

        initrd_entries[(uint32_t)existing].size = size;
        initrd_entries[(uint32_t)existing].data = data;
        initrd_entries[(uint32_t)existing].parent = parent_idx;
        str_copy(initrd_entries[(uint32_t)existing].name,
                 sizeof(initrd_entries[existing].name), name);
        return existing;
    }

    slot = alloc_entry_slot();
    if (slot < 0) {
        return -1;
    }

    initrd_entries[(uint32_t)slot].in_use = 1U;
    initrd_entries[(uint32_t)slot].type = VFS_NODE_FILE;
    initrd_entries[(uint32_t)slot].parent = parent_idx;
    str_copy(initrd_entries[(uint32_t)slot].path, sizeof(initrd_entries[slot].path),
             file_path);
    str_copy(initrd_entries[(uint32_t)slot].name, sizeof(initrd_entries[slot].name),
             name);
    initrd_entries[(uint32_t)slot].size = size;
    initrd_entries[(uint32_t)slot].data = data;
    initrd_entry_count++;
    return slot;
}

static void fill_vfs_node(uint32_t entry_index, struct vfs_node *out_node)
{
    struct initrd_entry *entry;

    if (out_node == 0 || entry_index >= INITRD_MAX_ENTRIES) {
        return;
    }

    entry = &initrd_entries[entry_index];
    out_node->type = entry->type;
    out_node->inode = entry_index + 1U;
    out_node->size = entry->size;
    out_node->flags = 0U;
    out_node->ops = (entry->type == VFS_NODE_DIRECTORY) ?
                    &initrd_dir_ops : &initrd_file_ops;
    out_node->fs_data = entry;
}

static int32_t parse_archive(const uint8_t *archive, uint32_t archive_size)
{
    uint32_t offset = 0U;

    while (offset + TAR_BLOCK_SIZE <= archive_size) {
        const struct tar_header *header;
        uint32_t file_size;
        uint32_t data_offset;
        uint32_t padded_size;
        uint32_t next_offset;
        uint8_t is_dir;
        char raw_path[VFS_PATH_MAX];
        char canonical_path[VFS_PATH_MAX];

        header = (const struct tar_header *)(const void *)(archive + offset);

        if (is_zero_block((const uint8_t *)header) != 0U) {
            break;
        }

        if (parse_octal_field(header->size, sizeof(header->size), &file_size) != 0) {
            return -1;
        }

        if (build_tar_raw_path(header, raw_path, sizeof(raw_path)) != 0) {
            return -1;
        }

        if (canonicalize_archive_path(raw_path, canonical_path,
                                      sizeof(canonical_path)) != 0) {
            return -1;
        }

        data_offset = offset + TAR_BLOCK_SIZE;
        padded_size = align_up_u32(file_size, TAR_BLOCK_SIZE);
        if (add_overflow_u32(data_offset, padded_size, &next_offset) != 0U ||
            next_offset > archive_size) {
            return -1;
        }

        is_dir = (uint8_t)(header->typeflag == '5');
        if (is_dir == 0U && canonical_path[0] != '\0') {
            uint32_t path_len = str_len(canonical_path);
            if (path_len > 1U && canonical_path[path_len - 1U] == '/') {
                is_dir = 1U;
            }
        }

        if (is_dir != 0U) {
            if (ensure_directory(canonical_path) < 0) {
                return -1;
            }
        } else if (header->typeflag == '0' || header->typeflag == '\0') {
            if (add_file_entry(canonical_path, file_size,
                               archive + data_offset) < 0) {
                return -1;
            }
        }

        offset = next_offset;
    }

    return 0;
}

static void reset_entries(void)
{
    uint32_t i;

    for (i = 0U; i < INITRD_MAX_ENTRIES; i++) {
        initrd_entries[i].in_use = 0U;
        initrd_entries[i].type = VFS_NODE_UNKNOWN;
        initrd_entries[i].parent = -1;
        initrd_entries[i].path[0] = '\0';
        initrd_entries[i].name[0] = '\0';
        initrd_entries[i].size = 0U;
        initrd_entries[i].data = 0;
    }

    initrd_entries[0].in_use = 1U;
    initrd_entries[0].type = VFS_NODE_DIRECTORY;
    initrd_entries[0].parent = -1;
    str_copy(initrd_entries[0].path, sizeof(initrd_entries[0].path), "/");
    str_copy(initrd_entries[0].name, sizeof(initrd_entries[0].name), "/");
    initrd_entries[0].size = 0U;
    initrd_entries[0].data = 0;
    initrd_entry_count = 1U;
}

static void run_self_test(void)
{
    char buffer[64];
    int32_t fd;
    int32_t read_len;

    fd = vfs_open("/hello.txt", VFS_OPEN_READ);
    if (fd < 0) {
        serial_puts("[INITRD] self-test open /hello.txt failed\n");
        return;
    }

    read_len = vfs_read(fd, buffer, (uint32_t)(sizeof(buffer) - 1U));
    if (read_len < 0) {
        serial_puts("[INITRD] self-test read /hello.txt failed\n");
        (void)vfs_close(fd);
        return;
    }

    buffer[(uint32_t)read_len] = '\0';
    serial_puts("[INITRD] self-test /hello.txt: ");
    serial_puts(buffer);
    if (read_len == 0 || buffer[(uint32_t)read_len - 1U] != '\n') {
        serial_puts("\n");
    }

    (void)vfs_close(fd);
}

static int32_t initrd_lookup(const struct vfs_node *dir, const char *name,
                             struct vfs_node *out_node)
{
    struct initrd_entry *dir_entry;
    int32_t dir_index;
    uint32_t i;

    if (dir == 0 || name == 0 || out_node == 0 || dir->fs_data == 0) {
        return VFS_ERR_INVALID;
    }

    dir_entry = (struct initrd_entry *)dir->fs_data;
    if (dir_entry->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }

    if (str_equal(name, ".") != 0U) {
        dir_index = (int32_t)(dir_entry - initrd_entries);
        if (dir_index < 0 || (uint32_t)dir_index >= INITRD_MAX_ENTRIES) {
            return VFS_ERR_INVALID;
        }
        fill_vfs_node((uint32_t)dir_index, out_node);
        return VFS_OK;
    }

    if (str_equal(name, "..") != 0U) {
        if (dir_entry->parent < 0) {
            fill_vfs_node(0U, out_node);
        } else {
            fill_vfs_node((uint32_t)dir_entry->parent, out_node);
        }
        return VFS_OK;
    }

    dir_index = (int32_t)(dir_entry - initrd_entries);
    if (dir_index < 0 || (uint32_t)dir_index >= INITRD_MAX_ENTRIES) {
        return VFS_ERR_INVALID;
    }

    for (i = 0U; i < INITRD_MAX_ENTRIES; i++) {
        if (initrd_entries[i].in_use == 0U) {
            continue;
        }

        if (initrd_entries[i].parent == dir_index &&
            str_equal(initrd_entries[i].name, name) != 0U) {
            fill_vfs_node(i, out_node);
            return VFS_OK;
        }
    }

    return VFS_ERR_NOT_FOUND;
}

static int32_t initrd_read(const struct vfs_node *node, uint32_t offset,
                           uint8_t *buffer, uint32_t size)
{
    const struct initrd_entry *entry;
    uint32_t remaining;
    uint32_t to_copy;
    uint32_t i;

    if (node == 0 || node->fs_data == 0 || (buffer == 0 && size != 0U)) {
        return VFS_ERR_INVALID;
    }

    entry = (const struct initrd_entry *)node->fs_data;
    if (entry->type != VFS_NODE_FILE || entry->data == 0) {
        return VFS_ERR_NOT_FILE;
    }

    if (offset >= entry->size) {
        return 0;
    }

    remaining = entry->size - offset;
    to_copy = (size < remaining) ? size : remaining;

    for (i = 0U; i < to_copy; i++) {
        buffer[i] = entry->data[offset + i];
    }

    return (int32_t)to_copy;
}

static int32_t initrd_write(const struct vfs_node *node, uint32_t offset,
                            const uint8_t *buffer, uint32_t size)
{
    (void)node;
    (void)offset;
    (void)buffer;
    (void)size;
    return VFS_ERR_NOT_SUPPORTED;
}

int32_t initrd_init(void)
{
    const uint8_t *archive_start = _binary_build_initrd_tar_start;
    uint32_t archive_size;
    struct vfs_node root_node;
    int32_t rc;

    archive_size = (uint32_t)(uintptr_t)(_binary_build_initrd_tar_end -
                                         _binary_build_initrd_tar_start);
    if (archive_size < TAR_BLOCK_SIZE) {
        serial_puts("[INITRD] image too small\n");
        return -1;
    }

    reset_entries();
    if (parse_archive(archive_start, archive_size) != 0) {
        serial_puts("[INITRD] parse failed\n");
        return -1;
    }

    fill_vfs_node(0U, &root_node);
    rc = vfs_mount("/", &root_node);
    if (rc != VFS_OK) {
        serial_puts("[INITRD] mount failed rc=");
        serial_put_u32((uint32_t)(-rc));
        serial_puts("\n");
        return -1;
    }

    serial_puts("[INITRD] mounted tar initrd entries=");
    serial_put_u32(initrd_entry_count);
    serial_puts("\n");

    run_self_test();
    return 0;
}
