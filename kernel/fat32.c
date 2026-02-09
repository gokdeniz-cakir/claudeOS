#include "fat32.h"

#include <stdint.h>

#include "ata.h"
#include "serial.h"
#include "vfs.h"

#define FAT32_MOUNT_PATH            "/fat"

#define FAT32_ATTR_READ_ONLY        0x01U
#define FAT32_ATTR_HIDDEN           0x02U
#define FAT32_ATTR_SYSTEM           0x04U
#define FAT32_ATTR_VOLUME_ID        0x08U
#define FAT32_ATTR_DIRECTORY        0x10U
#define FAT32_ATTR_ARCHIVE          0x20U
#define FAT32_ATTR_LFN              0x0FU

#define FAT32_EOC_MIN               0x0FFFFFF8U
#define FAT32_BAD_CLUSTER           0x0FFFFFF7U

#define FAT32_SECTOR_SIZE           512U

struct fat32_fs {
    uint8_t mounted;
    uint8_t ata_drive;
    uint32_t partition_lba;
    uint32_t total_sectors;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fats;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t cluster_size_bytes;
    uint32_t fat_cache_lba;
    uint8_t fat_cache_valid;
    uint8_t fat_cache[FAT32_SECTOR_SIZE];
};

struct fat32_dirent {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t create_time_tenth;
    uint8_t create_time[2];
    uint8_t create_date[2];
    uint8_t access_date[2];
    uint8_t first_cluster_hi[2];
    uint8_t modify_time[2];
    uint8_t modify_date[2];
    uint8_t first_cluster_lo[2];
    uint8_t file_size[4];
} __attribute__((packed));

struct fat32_partition_entry {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint8_t lba_first[4];
    uint8_t sector_count[4];
} __attribute__((packed));

static struct fat32_fs fat32_state;

static int32_t fat32_lookup(const struct vfs_node *dir, const char *name,
                            struct vfs_node *out_node);
static int32_t fat32_read(const struct vfs_node *node, uint32_t offset,
                          uint8_t *buffer, uint32_t size);
static int32_t fat32_write(const struct vfs_node *node, uint32_t offset,
                           const uint8_t *buffer, uint32_t size);

static const struct vfs_node_ops fat32_dir_ops = {
    .lookup = fat32_lookup,
    .read = 0,
    .write = 0
};

static const struct vfs_node_ops fat32_file_ops = {
    .lookup = 0,
    .read = fat32_read,
    .write = fat32_write
};

static uint8_t to_upper_ascii(uint8_t c)
{
    if (c >= 'a' && c <= 'z') {
        return (uint8_t)(c - ('a' - 'A'));
    }
    return c;
}

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

static uint8_t str_equal_ci(const char *a, const char *b)
{
    uint32_t i = 0U;

    if (a == 0 || b == 0) {
        return 0U;
    }

    while (a[i] != '\0' && b[i] != '\0') {
        if (to_upper_ascii((uint8_t)a[i]) != to_upper_ascii((uint8_t)b[i])) {
            return 0U;
        }
        i++;
    }

    return (uint8_t)(a[i] == '\0' && b[i] == '\0');
}

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static uint32_t fat32_is_eoc(uint32_t cluster)
{
    return (uint32_t)(cluster >= FAT32_EOC_MIN);
}

static uint32_t fat32_cluster_to_lba(const struct fat32_fs *fs, uint32_t cluster)
{
    if (fs == 0 || cluster < 2U) {
        return 0U;
    }

    return fs->data_start_lba + ((cluster - 2U) * fs->sectors_per_cluster);
}

static int fat32_read_sector(const struct fat32_fs *fs, uint32_t lba, uint8_t *buffer)
{
    if (fs == 0 || buffer == 0) {
        return -1;
    }

    return ata_pio_read28(fs->ata_drive, lba, 1U, buffer);
}

static int fat32_read_fat_entry(struct fat32_fs *fs, uint32_t cluster,
                                uint32_t *next_cluster)
{
    uint32_t fat_offset;
    uint32_t fat_sector;
    uint32_t ent_offset;
    uint32_t value;

    if (fs == 0 || next_cluster == 0 || cluster < 2U) {
        return -1;
    }

    fat_offset = cluster * 4U;
    fat_sector = fs->fat_start_lba + (fat_offset / FAT32_SECTOR_SIZE);
    ent_offset = fat_offset % FAT32_SECTOR_SIZE;

    if (fs->fat_cache_valid == 0U || fs->fat_cache_lba != fat_sector) {
        if (fat32_read_sector(fs, fat_sector, fs->fat_cache) != 0) {
            return -1;
        }
        fs->fat_cache_lba = fat_sector;
        fs->fat_cache_valid = 1U;
    }

    value = read_u32_le(&fs->fat_cache[ent_offset]) & 0x0FFFFFFFU;
    *next_cluster = value;
    return 0;
}

static void fat32_format_short_name(const struct fat32_dirent *entry, char *out_name,
                                    uint32_t out_size)
{
    uint32_t i;
    uint32_t out_idx = 0U;
    uint8_t has_ext = 0U;

    if (entry == 0 || out_name == 0 || out_size < 2U) {
        return;
    }

    for (i = 8U; i < 11U; i++) {
        if (entry->name[i] != ' ') {
            has_ext = 1U;
            break;
        }
    }

    for (i = 0U; i < 8U; i++) {
        uint8_t c = entry->name[i];
        if (c == ' ') {
            break;
        }
        if (out_idx + 1U >= out_size) {
            break;
        }
        out_name[out_idx++] = (char)to_upper_ascii(c);
    }

    if (has_ext != 0U && out_idx + 2U < out_size) {
        out_name[out_idx++] = '.';
        for (i = 8U; i < 11U; i++) {
            uint8_t c = entry->name[i];
            if (c == ' ') {
                break;
            }
            if (out_idx + 1U >= out_size) {
                break;
            }
            out_name[out_idx++] = (char)to_upper_ascii(c);
        }
    }

    out_name[out_idx] = '\0';
}

static uint32_t fat32_entry_first_cluster(const struct fat32_dirent *entry)
{
    uint32_t high;
    uint32_t low;

    if (entry == 0) {
        return 0U;
    }

    high = (uint32_t)read_u16_le(entry->first_cluster_hi);
    low = (uint32_t)read_u16_le(entry->first_cluster_lo);

    return (high << 16U) | low;
}

static uint32_t fat32_entry_file_size(const struct fat32_dirent *entry)
{
    if (entry == 0) {
        return 0U;
    }
    return read_u32_le(entry->file_size);
}

static void fat32_fill_node(const struct fat32_fs *fs, uint32_t cluster,
                            uint32_t size, uint8_t is_dir,
                            struct vfs_node *out_node)
{
    if (fs == 0 || out_node == 0) {
        return;
    }

    out_node->type = (is_dir != 0U) ? VFS_NODE_DIRECTORY : VFS_NODE_FILE;
    out_node->inode = cluster;
    out_node->size = size;
    out_node->flags = 0U;
    out_node->ops = (is_dir != 0U) ? &fat32_dir_ops : &fat32_file_ops;
    out_node->fs_data = (void *)fs;
}

static int fat32_find_in_directory(struct fat32_fs *fs, uint32_t dir_cluster,
                                   const char *name, struct vfs_node *out_node)
{
    uint32_t cluster = dir_cluster;
    uint8_t sector_buffer[FAT32_SECTOR_SIZE];
    char short_name[13];

    if (fs == 0 || name == 0 || out_node == 0 || cluster < 2U) {
        return VFS_ERR_INVALID;
    }

    while (fat32_is_eoc(cluster) == 0U) {
        uint32_t lba = fat32_cluster_to_lba(fs, cluster);
        uint32_t sec;

        if (lba == 0U) {
            return VFS_ERR_NOT_FOUND;
        }

        for (sec = 0U; sec < fs->sectors_per_cluster; sec++) {
            uint32_t off;

            if (fat32_read_sector(fs, lba + sec, sector_buffer) != 0) {
                return VFS_ERR_NOT_FOUND;
            }

            for (off = 0U; off < FAT32_SECTOR_SIZE; off += sizeof(struct fat32_dirent)) {
                const struct fat32_dirent *entry;
                uint8_t first;
                uint8_t attr;

                entry = (const struct fat32_dirent *)(const void *)(sector_buffer + off);
                first = entry->name[0];
                attr = entry->attr;

                if (first == 0x00U) {
                    return VFS_ERR_NOT_FOUND;
                }

                if (first == 0xE5U || attr == FAT32_ATTR_LFN ||
                    (attr & FAT32_ATTR_VOLUME_ID) != 0U) {
                    continue;
                }

                fat32_format_short_name(entry, short_name, sizeof(short_name));
                if (str_equal_ci(short_name, name) == 0U) {
                    continue;
                }

                fat32_fill_node(fs,
                                fat32_entry_first_cluster(entry),
                                fat32_entry_file_size(entry),
                                (uint8_t)((attr & FAT32_ATTR_DIRECTORY) != 0U),
                                out_node);
                return VFS_OK;
            }
        }

        if (fat32_read_fat_entry(fs, cluster, &cluster) != 0) {
            return VFS_ERR_NOT_FOUND;
        }
        if (cluster == FAT32_BAD_CLUSTER || cluster < 2U) {
            return VFS_ERR_NOT_FOUND;
        }
    }

    return VFS_ERR_NOT_FOUND;
}

static int fat32_detect_partition(struct fat32_fs *fs, uint32_t *out_partition_lba)
{
    uint8_t sector[FAT32_SECTOR_SIZE];
    const struct fat32_partition_entry *ptable;
    uint16_t signature;
    uint32_t i;

    if (fs == 0 || out_partition_lba == 0) {
        return -1;
    }

    if (fat32_read_sector(fs, 0U, sector) != 0) {
        return -1;
    }

    signature = read_u16_le(&sector[510]);
    if (signature != 0xAA55U) {
        return -1;
    }

    if (sector[0x52] == 'F' && sector[0x53] == 'A' &&
        sector[0x54] == 'T' && sector[0x55] == '3' && sector[0x56] == '2') {
        *out_partition_lba = 0U;
        return 0;
    }

    ptable = (const struct fat32_partition_entry *)(const void *)&sector[446];
    for (i = 0U; i < 4U; i++) {
        uint8_t type = ptable[i].type;
        if (type == 0x0BU || type == 0x0CU) {
            *out_partition_lba = read_u32_le(ptable[i].lba_first);
            return 0;
        }
    }

    return -1;
}

static int fat32_load_bpb(struct fat32_fs *fs)
{
    uint8_t sector[FAT32_SECTOR_SIZE];
    uint32_t total_sectors_16;
    uint32_t total_sectors_32;

    if (fs == 0) {
        return -1;
    }

    if (fat32_read_sector(fs, fs->partition_lba, sector) != 0) {
        return -1;
    }

    if (read_u16_le(&sector[510]) != 0xAA55U) {
        return -1;
    }

    fs->bytes_per_sector = read_u16_le(&sector[11]);
    fs->sectors_per_cluster = sector[13];
    fs->reserved_sectors = read_u16_le(&sector[14]);
    fs->fats = sector[16];
    fs->sectors_per_fat = read_u32_le(&sector[36]);
    fs->root_cluster = read_u32_le(&sector[44]);

    total_sectors_16 = read_u16_le(&sector[19]);
    total_sectors_32 = read_u32_le(&sector[32]);
    fs->total_sectors = (total_sectors_16 != 0U) ? total_sectors_16 : total_sectors_32;

    if (fs->bytes_per_sector != FAT32_SECTOR_SIZE ||
        fs->sectors_per_cluster == 0U ||
        fs->reserved_sectors == 0U ||
        fs->fats == 0U ||
        fs->sectors_per_fat == 0U ||
        fs->root_cluster < 2U ||
        fs->total_sectors == 0U) {
        return -1;
    }

    fs->fat_start_lba = fs->partition_lba + fs->reserved_sectors;
    fs->data_start_lba = fs->fat_start_lba + (fs->fats * fs->sectors_per_fat);
    fs->cluster_size_bytes = fs->sectors_per_cluster * fs->bytes_per_sector;
    fs->fat_cache_valid = 0U;
    fs->fat_cache_lba = 0U;

    if (fs->data_start_lba >= fs->partition_lba + fs->total_sectors) {
        return -1;
    }

    return 0;
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

static void fat32_self_test(void)
{
    char buffer[96];
    int32_t fd;
    int32_t read_len;

    fd = vfs_open("/fat/HELLO.TXT", VFS_OPEN_READ);
    if (fd < 0) {
        serial_puts("[FAT32] self-test open /fat/HELLO.TXT failed\n");
        return;
    }

    read_len = vfs_read(fd, buffer, (uint32_t)(sizeof(buffer) - 1U));
    if (read_len < 0) {
        serial_puts("[FAT32] self-test read /fat/HELLO.TXT failed\n");
        (void)vfs_close(fd);
        return;
    }

    buffer[(uint32_t)read_len] = '\0';
    serial_puts("[FAT32] self-test /fat/HELLO.TXT: ");
    serial_puts(buffer);
    if (read_len == 0 || buffer[(uint32_t)read_len - 1U] != '\n') {
        serial_puts("\n");
    }

    (void)vfs_close(fd);

    fd = vfs_open("/fat/DOCS/INFO.TXT", VFS_OPEN_READ);
    if (fd < 0) {
        serial_puts("[FAT32] self-test open /fat/DOCS/INFO.TXT failed\n");
        return;
    }

    read_len = vfs_read(fd, buffer, (uint32_t)(sizeof(buffer) - 1U));
    if (read_len < 0) {
        serial_puts("[FAT32] self-test read /fat/DOCS/INFO.TXT failed\n");
        (void)vfs_close(fd);
        return;
    }

    buffer[(uint32_t)read_len] = '\0';
    serial_puts("[FAT32] self-test /fat/DOCS/INFO.TXT: ");
    serial_puts(buffer);
    if (read_len == 0 || buffer[(uint32_t)read_len - 1U] != '\n') {
        serial_puts("\n");
    }

    (void)vfs_close(fd);
}

static int32_t fat32_lookup(const struct vfs_node *dir, const char *name,
                            struct vfs_node *out_node)
{
    struct fat32_fs *fs;

    if (dir == 0 || name == 0 || out_node == 0) {
        return VFS_ERR_INVALID;
    }

    fs = (struct fat32_fs *)dir->fs_data;
    if (fs == 0 || fs->mounted == 0U) {
        return VFS_ERR_NOT_FOUND;
    }

    if (dir->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }

    if (str_len(name) == 0U) {
        return VFS_ERR_INVALID;
    }

    if (name[0] == '.' && name[1] == '\0') {
        *out_node = *dir;
        return VFS_OK;
    }

    return fat32_find_in_directory(fs, dir->inode, name, out_node);
}

static int32_t fat32_read(const struct vfs_node *node, uint32_t offset,
                          uint8_t *buffer, uint32_t size)
{
    struct fat32_fs *fs;
    uint8_t sector_buffer[FAT32_SECTOR_SIZE];
    uint32_t cluster;
    uint32_t cluster_size;
    uint32_t skip_clusters;
    uint32_t cluster_offset;
    uint32_t remaining;
    uint32_t copied = 0U;

    if (node == 0 || buffer == 0 || size == 0U) {
        return 0;
    }

    if (node->type != VFS_NODE_FILE) {
        return VFS_ERR_NOT_FILE;
    }

    fs = (struct fat32_fs *)node->fs_data;
    if (fs == 0 || fs->mounted == 0U) {
        return VFS_ERR_NOT_FOUND;
    }

    if (offset >= node->size) {
        return 0;
    }

    if (node->inode < 2U) {
        return VFS_ERR_NOT_FILE;
    }

    cluster = node->inode;
    cluster_size = fs->cluster_size_bytes;
    skip_clusters = offset / cluster_size;
    cluster_offset = offset % cluster_size;
    remaining = node->size - offset;
    if (remaining > size) {
        remaining = size;
    }

    while (skip_clusters > 0U) {
        if (fat32_read_fat_entry(fs, cluster, &cluster) != 0 ||
            fat32_is_eoc(cluster) != 0U ||
            cluster == FAT32_BAD_CLUSTER || cluster < 2U) {
            return (copied > 0U) ? (int32_t)copied : VFS_ERR_NOT_FOUND;
        }
        skip_clusters--;
    }

    while (remaining > 0U && fat32_is_eoc(cluster) == 0U) {
        uint32_t base_lba = fat32_cluster_to_lba(fs, cluster);
        uint32_t sec;

        if (base_lba == 0U) {
            break;
        }

        for (sec = 0U; sec < fs->sectors_per_cluster && remaining > 0U; sec++) {
            uint32_t start = 0U;
            uint32_t count;
            uint32_t i;

            if (cluster_offset >= FAT32_SECTOR_SIZE) {
                cluster_offset -= FAT32_SECTOR_SIZE;
                continue;
            }

            if (fat32_read_sector(fs, base_lba + sec, sector_buffer) != 0) {
                return (copied > 0U) ? (int32_t)copied : VFS_ERR_NOT_FOUND;
            }

            start = cluster_offset;
            count = FAT32_SECTOR_SIZE - start;
            if (count > remaining) {
                count = remaining;
            }

            for (i = 0U; i < count; i++) {
                buffer[copied + i] = sector_buffer[start + i];
            }

            copied += count;
            remaining -= count;
            cluster_offset = 0U;
        }

        if (remaining == 0U) {
            break;
        }

        if (fat32_read_fat_entry(fs, cluster, &cluster) != 0 ||
            cluster == FAT32_BAD_CLUSTER || cluster < 2U) {
            break;
        }
    }

    return (int32_t)copied;
}

static int32_t fat32_write(const struct vfs_node *node, uint32_t offset,
                           const uint8_t *buffer, uint32_t size)
{
    (void)node;
    (void)offset;
    (void)buffer;
    (void)size;
    return VFS_ERR_NOT_SUPPORTED;
}

int32_t fat32_init(void)
{
    struct vfs_node root_node;
    int32_t rc;
    uint32_t partition_lba = 0U;

    ata_init();

    if (ata_drive_present(ATA_DRIVE_SLAVE) != 0U) {
        fat32_state.ata_drive = ATA_DRIVE_SLAVE;
    } else if (ata_drive_present(ATA_DRIVE_MASTER) != 0U) {
        fat32_state.ata_drive = ATA_DRIVE_MASTER;
    } else {
        serial_puts("[FAT32] no ATA drive detected\n");
        return -1;
    }

    fat32_state.mounted = 0U;
    fat32_state.partition_lba = 0U;
    fat32_state.total_sectors = 0U;
    fat32_state.bytes_per_sector = 0U;
    fat32_state.sectors_per_cluster = 0U;
    fat32_state.reserved_sectors = 0U;
    fat32_state.fats = 0U;
    fat32_state.sectors_per_fat = 0U;
    fat32_state.root_cluster = 0U;
    fat32_state.fat_start_lba = 0U;
    fat32_state.data_start_lba = 0U;
    fat32_state.cluster_size_bytes = 0U;
    fat32_state.fat_cache_valid = 0U;
    fat32_state.fat_cache_lba = 0U;

    if (fat32_detect_partition(&fat32_state, &partition_lba) != 0) {
        serial_puts("[FAT32] FAT32 partition not found\n");
        return -1;
    }

    fat32_state.partition_lba = partition_lba;

    if (fat32_load_bpb(&fat32_state) != 0) {
        serial_puts("[FAT32] invalid BPB\n");
        return -1;
    }

    fat32_fill_node(&fat32_state, fat32_state.root_cluster, 0U, 1U, &root_node);
    rc = vfs_mount(FAT32_MOUNT_PATH, &root_node);
    if (rc != VFS_OK) {
        serial_puts("[FAT32] mount failed rc=");
        serial_put_u32((uint32_t)(-rc));
        serial_puts("\n");
        return -1;
    }

    fat32_state.mounted = 1U;
    serial_puts("[FAT32] mounted at /fat drive=");
    serial_puts((fat32_state.ata_drive == ATA_DRIVE_MASTER) ? "master" : "slave");
    serial_puts(" partition_lba=");
    serial_put_u32(fat32_state.partition_lba);
    serial_puts("\n");

    fat32_self_test();
    return 0;
}
