#include "vfs.h"

#include <stdint.h>

#include "process.h"
#include "serial.h"
#include "spinlock.h"

struct vfs_mount {
    uint8_t in_use;
    uint32_t path_len;
    char path[VFS_PATH_MAX];
    struct vfs_node root_node;
};

struct vfs_open_file {
    uint8_t in_use;
    uint32_t owner_pid;
    uint32_t flags;
    uint32_t position;
    struct vfs_node node;
};

static struct vfs_mount vfs_mounts[VFS_MAX_MOUNTS];
static struct vfs_open_file vfs_open_files[VFS_MAX_OPEN_FILES];
static struct spinlock vfs_lock = SPINLOCK_INITIALIZER;
static uint8_t vfs_initialized = 0U;

static int32_t vfs_empty_lookup(const struct vfs_node *dir, const char *name,
                                struct vfs_node *out_node)
{
    (void)dir;
    (void)name;
    (void)out_node;
    return VFS_ERR_NOT_FOUND;
}

static const struct vfs_node_ops vfs_empty_dir_ops = {
    .lookup = vfs_empty_lookup,
    .read = 0,
    .write = 0
};

static int32_t vfs_canonicalize_path(const char *input, char *output,
                                     uint32_t output_size,
                                     uint32_t *output_len)
{
    uint32_t src;
    uint32_t dst;

    if (input == 0 || output == 0 || output_len == 0 || output_size < 2U) {
        return VFS_ERR_INVALID;
    }

    if (input[0] != '/') {
        return VFS_ERR_INVALID;
    }

    src = 1U;
    dst = 0U;
    output[dst++] = '/';

    while (input[src] != '\0') {
        if (input[src] == '/') {
            while (input[src] == '/') {
                src++;
            }

            if (input[src] == '\0') {
                break;
            }

            if (dst + 1U >= output_size) {
                return VFS_ERR_NAME_TOO_LONG;
            }

            output[dst++] = '/';
            continue;
        }

        if (dst + 1U >= output_size) {
            return VFS_ERR_NAME_TOO_LONG;
        }

        output[dst++] = input[src];
        src++;
    }

    if (dst > 1U && output[dst - 1U] == '/') {
        dst--;
    }

    output[dst] = '\0';
    *output_len = dst;
    return VFS_OK;
}

static uint8_t vfs_path_is_exact_match(const struct vfs_mount *mount,
                                       const char *path, uint32_t path_len)
{
    uint32_t i;

    if (mount == 0 || path == 0 || mount->path_len != path_len) {
        return 0U;
    }

    for (i = 0U; i < path_len; i++) {
        if (mount->path[i] != path[i]) {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t vfs_path_has_mount_prefix(const struct vfs_mount *mount,
                                         const char *path)
{
    uint32_t i;

    if (mount == 0 || path == 0 || mount->in_use == 0U) {
        return 0U;
    }

    if (mount->path_len == 1U && mount->path[0] == '/') {
        return 1U;
    }

    for (i = 0U; i < mount->path_len; i++) {
        if (mount->path[i] != path[i]) {
            return 0U;
        }
    }

    return (uint8_t)(path[mount->path_len] == '\0' ||
                     path[mount->path_len] == '/');
}

static int32_t vfs_find_mount_for_path(const char *path)
{
    uint32_t i;
    int32_t best_index = -1;
    uint32_t best_len = 0U;

    for (i = 0U; i < VFS_MAX_MOUNTS; i++) {
        if (vfs_path_has_mount_prefix(&vfs_mounts[i], path) == 0U) {
            continue;
        }

        if (best_index < 0 || vfs_mounts[i].path_len > best_len) {
            best_index = (int32_t)i;
            best_len = vfs_mounts[i].path_len;
        }
    }

    return best_index;
}

static int32_t vfs_copy_component(const char *src, uint32_t len, char *dst)
{
    uint32_t i;

    if (src == 0 || dst == 0 || len == 0U) {
        return VFS_ERR_INVALID;
    }

    if (len >= VFS_NAME_MAX) {
        return VFS_ERR_NAME_TOO_LONG;
    }

    for (i = 0U; i < len; i++) {
        dst[i] = src[i];
    }
    dst[len] = '\0';

    return VFS_OK;
}

static int32_t vfs_resolve_from_mount(const struct vfs_mount *mount,
                                      const char *path,
                                      struct vfs_node *out_node)
{
    struct vfs_node current;
    const char *cursor;
    char component[VFS_NAME_MAX];

    if (mount == 0 || path == 0 || out_node == 0) {
        return VFS_ERR_INVALID;
    }

    current = mount->root_node;

    if (mount->path_len == 1U) {
        cursor = path + 1;
    } else if (path[mount->path_len] == '\0') {
        cursor = path + mount->path_len;
    } else {
        cursor = path + mount->path_len + 1U;
    }

    while (*cursor != '\0') {
        uint32_t comp_len = 0U;
        int32_t rc;
        struct vfs_node next;

        while (*cursor == '/') {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        while (cursor[comp_len] != '\0' && cursor[comp_len] != '/') {
            comp_len++;
        }

        rc = vfs_copy_component(cursor, comp_len, component);
        if (rc != VFS_OK) {
            return rc;
        }

        if (current.type != VFS_NODE_DIRECTORY) {
            return VFS_ERR_NOT_DIR;
        }

        if (current.ops == 0 || current.ops->lookup == 0) {
            return VFS_ERR_NOT_SUPPORTED;
        }

        rc = current.ops->lookup(&current, component, &next);
        if (rc != VFS_OK) {
            return rc;
        }

        current = next;
        cursor += comp_len;
    }

    *out_node = current;
    return VFS_OK;
}

static int32_t vfs_fd_to_index(int32_t fd)
{
    int32_t index = fd - (int32_t)VFS_FD_BASE;

    if (index < 0 || (uint32_t)index >= VFS_MAX_OPEN_FILES) {
        return -1;
    }

    return index;
}

static void vfs_reset_open_file(uint32_t index)
{
    if (index >= VFS_MAX_OPEN_FILES) {
        return;
    }

    vfs_open_files[index].in_use = 0U;
    vfs_open_files[index].owner_pid = 0U;
    vfs_open_files[index].flags = 0U;
    vfs_open_files[index].position = 0U;
    vfs_open_files[index].node.type = VFS_NODE_UNKNOWN;
    vfs_open_files[index].node.inode = 0U;
    vfs_open_files[index].node.size = 0U;
    vfs_open_files[index].node.flags = 0U;
    vfs_open_files[index].node.ops = 0;
    vfs_open_files[index].node.fs_data = 0;
}

static uint8_t vfs_owner_allows_access(const struct vfs_open_file *file,
                                       uint32_t caller_pid)
{
    if (file == 0) {
        return 0U;
    }

    if (file->owner_pid == 0U || caller_pid == 0U) {
        return 1U;
    }

    return (uint8_t)(file->owner_pid == caller_pid);
}

void vfs_init(void)
{
    uint32_t i;

    spinlock_init(&vfs_lock);

    for (i = 0U; i < VFS_MAX_MOUNTS; i++) {
        vfs_mounts[i].in_use = 0U;
        vfs_mounts[i].path_len = 0U;
        vfs_mounts[i].path[0] = '\0';
        vfs_mounts[i].root_node.type = VFS_NODE_UNKNOWN;
        vfs_mounts[i].root_node.inode = 0U;
        vfs_mounts[i].root_node.size = 0U;
        vfs_mounts[i].root_node.flags = 0U;
        vfs_mounts[i].root_node.ops = 0;
        vfs_mounts[i].root_node.fs_data = 0;
    }

    for (i = 0U; i < VFS_MAX_OPEN_FILES; i++) {
        vfs_reset_open_file(i);
    }

    vfs_mounts[0].in_use = 1U;
    vfs_mounts[0].path_len = 1U;
    vfs_mounts[0].path[0] = '/';
    vfs_mounts[0].path[1] = '\0';
    vfs_mounts[0].root_node.type = VFS_NODE_DIRECTORY;
    vfs_mounts[0].root_node.inode = 0U;
    vfs_mounts[0].root_node.size = 0U;
    vfs_mounts[0].root_node.flags = 0U;
    vfs_mounts[0].root_node.ops = &vfs_empty_dir_ops;
    vfs_mounts[0].root_node.fs_data = 0;

    vfs_initialized = 1U;
    serial_puts("[VFS] initialized\n");
}

int32_t vfs_mount(const char *path, const struct vfs_node *root_node)
{
    char mount_path[VFS_PATH_MAX];
    uint32_t mount_path_len = 0U;
    uint32_t flags;
    int32_t slot = -1;
    uint32_t i;
    int32_t rc;

    if (vfs_initialized == 0U || path == 0 || root_node == 0) {
        return VFS_ERR_INVALID;
    }

    if (root_node->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }

    rc = vfs_canonicalize_path(path, mount_path, sizeof(mount_path), &mount_path_len);
    if (rc != VFS_OK) {
        return rc;
    }

    flags = spinlock_lock_irqsave(&vfs_lock);

    for (i = 0U; i < VFS_MAX_MOUNTS; i++) {
        if (vfs_mounts[i].in_use == 0U) {
            continue;
        }

        if (vfs_path_is_exact_match(&vfs_mounts[i], mount_path,
                                    mount_path_len) != 0U) {
            slot = (int32_t)i;
            break;
        }
    }

    if (slot < 0) {
        for (i = 0U; i < VFS_MAX_MOUNTS; i++) {
            if (vfs_mounts[i].in_use == 0U) {
                slot = (int32_t)i;
                break;
            }
        }
    }

    if (slot < 0) {
        spinlock_unlock_irqrestore(&vfs_lock, flags);
        return VFS_ERR_NO_SPACE;
    }

    vfs_mounts[(uint32_t)slot].in_use = 1U;
    vfs_mounts[(uint32_t)slot].path_len = mount_path_len;
    for (i = 0U; i <= mount_path_len; i++) {
        vfs_mounts[(uint32_t)slot].path[i] = mount_path[i];
    }
    vfs_mounts[(uint32_t)slot].root_node = *root_node;

    spinlock_unlock_irqrestore(&vfs_lock, flags);
    return VFS_OK;
}

int32_t vfs_resolve(const char *path, struct vfs_node *out_node)
{
    char canonical_path[VFS_PATH_MAX];
    uint32_t canonical_len = 0U;
    uint32_t flags;
    int32_t mount_idx;
    int32_t rc;
    struct vfs_mount mount;

    if (vfs_initialized == 0U || path == 0 || out_node == 0) {
        return VFS_ERR_INVALID;
    }

    rc = vfs_canonicalize_path(path, canonical_path, sizeof(canonical_path),
                               &canonical_len);
    (void)canonical_len;
    if (rc != VFS_OK) {
        return rc;
    }

    flags = spinlock_lock_irqsave(&vfs_lock);
    mount_idx = vfs_find_mount_for_path(canonical_path);
    if (mount_idx < 0) {
        spinlock_unlock_irqrestore(&vfs_lock, flags);
        return VFS_ERR_NOT_FOUND;
    }

    mount = vfs_mounts[(uint32_t)mount_idx];
    spinlock_unlock_irqrestore(&vfs_lock, flags);

    return vfs_resolve_from_mount(&mount, canonical_path, out_node);
}

int32_t vfs_open(const char *path, uint32_t flags)
{
    struct vfs_node node;
    uint32_t irq_flags;
    uint32_t owner_pid;
    uint32_t i;
    int32_t rc;

    if (vfs_initialized == 0U || path == 0) {
        return VFS_ERR_INVALID;
    }

    if ((flags & (VFS_OPEN_READ | VFS_OPEN_WRITE)) == 0U) {
        flags = VFS_OPEN_READ;
    }

    rc = vfs_resolve(path, &node);
    if (rc != VFS_OK) {
        return rc;
    }

    if (node.type != VFS_NODE_FILE) {
        return VFS_ERR_NOT_FILE;
    }

    if ((flags & VFS_OPEN_READ) != 0U &&
        (node.ops == 0 || node.ops->read == 0)) {
        return VFS_ERR_NOT_SUPPORTED;
    }

    if ((flags & VFS_OPEN_WRITE) != 0U &&
        (node.ops == 0 || node.ops->write == 0)) {
        return VFS_ERR_NOT_SUPPORTED;
    }

    owner_pid = process_get_current_pid();

    irq_flags = spinlock_lock_irqsave(&vfs_lock);
    for (i = 0U; i < VFS_MAX_OPEN_FILES; i++) {
        if (vfs_open_files[i].in_use == 0U) {
            vfs_open_files[i].in_use = 1U;
            vfs_open_files[i].owner_pid = owner_pid;
            vfs_open_files[i].flags = flags;
            vfs_open_files[i].position = 0U;
            vfs_open_files[i].node = node;
            spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
            return (int32_t)(i + VFS_FD_BASE);
        }
    }
    spinlock_unlock_irqrestore(&vfs_lock, irq_flags);

    return VFS_ERR_NO_SPACE;
}

int32_t vfs_read(int32_t fd, void *buffer, uint32_t size)
{
    int32_t idx;
    uint32_t irq_flags;
    uint32_t caller_pid;
    struct vfs_open_file *file;
    int32_t bytes_read;

    if (buffer == 0 && size != 0U) {
        return VFS_ERR_INVALID;
    }

    idx = vfs_fd_to_index(fd);
    if (idx < 0) {
        return VFS_ERR_BAD_FD;
    }

    caller_pid = process_get_current_pid();
    irq_flags = spinlock_lock_irqsave(&vfs_lock);
    file = &vfs_open_files[(uint32_t)idx];
    if (file->in_use == 0U) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_BAD_FD;
    }

    if (vfs_owner_allows_access(file, caller_pid) == 0U) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_ACCESS;
    }

    if ((file->flags & VFS_OPEN_READ) == 0U) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_ACCESS;
    }

    if (file->node.ops == 0 || file->node.ops->read == 0) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_NOT_SUPPORTED;
    }

    bytes_read = file->node.ops->read(&file->node, file->position,
                                      (uint8_t *)buffer, size);
    if (bytes_read > 0) {
        if ((uint32_t)bytes_read > (0xFFFFFFFFU - file->position)) {
            file->position = 0xFFFFFFFFU;
        } else {
            file->position += (uint32_t)bytes_read;
        }
    }
    spinlock_unlock_irqrestore(&vfs_lock, irq_flags);

    return bytes_read;
}

int32_t vfs_write(int32_t fd, const void *buffer, uint32_t size)
{
    int32_t idx;
    uint32_t irq_flags;
    uint32_t caller_pid;
    struct vfs_open_file *file;
    int32_t bytes_written;

    if (buffer == 0 && size != 0U) {
        return VFS_ERR_INVALID;
    }

    idx = vfs_fd_to_index(fd);
    if (idx < 0) {
        return VFS_ERR_BAD_FD;
    }

    caller_pid = process_get_current_pid();
    irq_flags = spinlock_lock_irqsave(&vfs_lock);
    file = &vfs_open_files[(uint32_t)idx];
    if (file->in_use == 0U) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_BAD_FD;
    }

    if (vfs_owner_allows_access(file, caller_pid) == 0U) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_ACCESS;
    }

    if ((file->flags & VFS_OPEN_WRITE) == 0U) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_ACCESS;
    }

    if (file->node.ops == 0 || file->node.ops->write == 0) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_NOT_SUPPORTED;
    }

    bytes_written = file->node.ops->write(&file->node, file->position,
                                          (const uint8_t *)buffer, size);
    if (bytes_written > 0) {
        if ((uint32_t)bytes_written > (0xFFFFFFFFU - file->position)) {
            file->position = 0xFFFFFFFFU;
        } else {
            file->position += (uint32_t)bytes_written;
        }
    }
    spinlock_unlock_irqrestore(&vfs_lock, irq_flags);

    return bytes_written;
}

int32_t vfs_seek(int32_t fd, int32_t offset, uint32_t whence)
{
    int32_t idx;
    uint32_t irq_flags;
    uint32_t caller_pid;
    struct vfs_open_file *file;
    int64_t base;
    int64_t target;

    idx = vfs_fd_to_index(fd);
    if (idx < 0) {
        return VFS_ERR_BAD_FD;
    }

    caller_pid = process_get_current_pid();
    irq_flags = spinlock_lock_irqsave(&vfs_lock);
    file = &vfs_open_files[(uint32_t)idx];
    if (file->in_use == 0U) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_BAD_FD;
    }

    if (vfs_owner_allows_access(file, caller_pid) == 0U) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_ACCESS;
    }

    switch (whence) {
        case VFS_SEEK_SET:
            base = 0;
            break;
        case VFS_SEEK_CUR:
            base = (int64_t)(uint64_t)file->position;
            break;
        case VFS_SEEK_END:
            base = (int64_t)(uint64_t)file->node.size;
            break;
        default:
            spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
            return VFS_ERR_INVALID;
    }

    target = base + (int64_t)offset;
    if (target < 0 || target > (int64_t)(uint64_t)file->node.size) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_INVALID;
    }

    file->position = (uint32_t)(uint64_t)target;
    spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
    return (int32_t)file->position;
}

int32_t vfs_close(int32_t fd)
{
    int32_t idx;
    uint32_t irq_flags;
    uint32_t caller_pid;

    idx = vfs_fd_to_index(fd);
    if (idx < 0) {
        return VFS_ERR_BAD_FD;
    }

    caller_pid = process_get_current_pid();
    irq_flags = spinlock_lock_irqsave(&vfs_lock);
    if (vfs_open_files[(uint32_t)idx].in_use == 0U) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_BAD_FD;
    }

    if (vfs_owner_allows_access(&vfs_open_files[(uint32_t)idx], caller_pid) == 0U) {
        spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
        return VFS_ERR_ACCESS;
    }

    vfs_reset_open_file((uint32_t)idx);

    spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
    return VFS_OK;
}

void vfs_close_owned_by_pid(uint32_t pid)
{
    uint32_t irq_flags;
    uint32_t i;

    if (pid == 0U || vfs_initialized == 0U) {
        return;
    }

    irq_flags = spinlock_lock_irqsave(&vfs_lock);
    for (i = 0U; i < VFS_MAX_OPEN_FILES; i++) {
        if (vfs_open_files[i].in_use == 0U || vfs_open_files[i].owner_pid != pid) {
            continue;
        }

        vfs_reset_open_file(i);
    }
    spinlock_unlock_irqrestore(&vfs_lock, irq_flags);
}
