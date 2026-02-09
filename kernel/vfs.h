#ifndef CLAUDE_VFS_H
#define CLAUDE_VFS_H

#include <stdint.h>

#define VFS_PATH_MAX        256U
#define VFS_NAME_MAX        64U
#define VFS_MAX_MOUNTS      8U
#define VFS_MAX_OPEN_FILES  32U
#define VFS_FD_BASE         3U

#define VFS_OPEN_READ       0x1U
#define VFS_OPEN_WRITE      0x2U

#define VFS_OK                 0
#define VFS_ERR_INVALID       -1
#define VFS_ERR_NOT_FOUND     -2
#define VFS_ERR_NOT_DIR       -3
#define VFS_ERR_NOT_FILE      -4
#define VFS_ERR_NOT_SUPPORTED -5
#define VFS_ERR_NO_SPACE      -6
#define VFS_ERR_BAD_FD        -7
#define VFS_ERR_ACCESS        -8
#define VFS_ERR_NAME_TOO_LONG -9

enum vfs_node_type {
    VFS_NODE_UNKNOWN = 0,
    VFS_NODE_FILE = 1,
    VFS_NODE_DIRECTORY = 2
};

struct vfs_node;

struct vfs_node_ops {
    int32_t (*lookup)(const struct vfs_node *dir, const char *name,
                      struct vfs_node *out_node);
    int32_t (*read)(const struct vfs_node *node, uint32_t offset,
                    uint8_t *buffer, uint32_t size);
    int32_t (*write)(const struct vfs_node *node, uint32_t offset,
                     const uint8_t *buffer, uint32_t size);
};

struct vfs_node {
    uint32_t type;
    uint32_t inode;
    uint32_t size;
    uint32_t flags;
    const struct vfs_node_ops *ops;
    void *fs_data;
};

/* Initialize VFS core state and install an empty root mount at "/". */
void vfs_init(void);

/* Mount (or replace) a filesystem root node at an absolute path. */
int32_t vfs_mount(const char *path, const struct vfs_node *root_node);

/* Resolve an absolute path to a vnode. */
int32_t vfs_resolve(const char *path, struct vfs_node *out_node);

/* Open/read/write/close kernel-side file handles over resolved vnodes. */
int32_t vfs_open(const char *path, uint32_t flags);
int32_t vfs_read(int32_t fd, void *buffer, uint32_t size);
int32_t vfs_write(int32_t fd, const void *buffer, uint32_t size);
int32_t vfs_close(int32_t fd);

/* Close all open descriptors owned by the given process id. */
void vfs_close_owned_by_pid(uint32_t pid);

#endif /* CLAUDE_VFS_H */
