/* disk.c: SimpleFS disk emulator */

#include "sfs/disk.h"
#include "sfs/logging.h"

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

/* Internal Prototyes */

bool    disk_sanity_check(Disk *disk, size_t blocknum, const char *data);

/* External Functions */

/**
 *
 * Opens disk at specified path with the specified number of blocks by doing
 * the following:
 *
 *  1. Allocate Disk structure and sets appropriate attributes.
 *
 *  2. Open file descriptor to specified path.
 *
 *  3. Truncate file to desired file size (blocks * BLOCK_SIZE).
 *
 * @param       path        Path to disk image to create.
 * @param       blocks      Number of blocks to allocate for disk image.
 *
 * @return      Pointer to newly allocated and configured Disk structure (NULL
 *              on failure).
 **/
Disk *	disk_open(const char *path, size_t blocks) {
    Disk* disk = (Disk*)malloc(sizeof(Disk));
    if (disk == NULL) {
        return NULL;
    }
    int file = open(path, O_RDWR);
    if (file == -1) {
        error("%d\n", errno);
        return NULL;
    }
    disk->fd = file;
    disk->blocks = blocks;
    disk->reads = 0;
    disk->writes = 0;
    return disk;
}

/**
 * Close disk structure by doing the following:
 *
 *  1. Close disk file descriptor.
 *
 *  2. Report number of disk reads and writes.
 *
 *  3. Release disk structure memory.
 *
 * @param       disk        Pointer to Disk structure.
 */
void	disk_close(Disk *disk) {
    assert(disk != NULL);
    close(disk->fd);
    info("Closing disk, reads: %zu, writes: %zu\n", disk->reads, disk->writes);
    free(disk);
}

/**
 * Read data from disk at specified block into data buffer by doing the
 * following:
 *
 *  1. Perform sanity check.
 *
 *  2. Seek to specified block.
 *
 *  3. Read from block to data buffer (must be BLOCK_SIZE).
 *
 * @param       disk        Pointer to Disk structure.
 * @param       block       Block number to perform operation on.
 * @param       data        Data buffer.
 *
 * @return      Number of bytes read.
 *              (BLOCK_SIZE on success, DISK_FAILURE on failure).
 **/
ssize_t disk_read(Disk *disk, size_t block, char *data) {
    if (!disk_sanity_check(disk, block, data)) {
        return DISK_FAILURE;
    }
    if (lseek(disk->fd, block * BLOCK_SIZE, SEEK_SET) == -1) {
        error("errno: %d\n", errno);
        return DISK_FAILURE;
    }

    return read(disk->fd, data, BLOCK_SIZE);
}

/**
 * Write data to disk at specified block from data buffer by doing the
 * following:
 *
 *  1. Perform sanity check.
 *
 *  2. Seek to specified block.
 *
 *  3. Write data buffer (must be BLOCK_SIZE) to disk block.
 *
 * @param       disk        Pointer to Disk structure.
 * @param       block       Block number to perform operation on.
 * @param       data        Data buffer.
 *
 * @return      Number of bytes written.
 *              (BLOCK_SIZE on success, DISK_FAILURE on failure).
 **/
ssize_t disk_write(Disk *disk, size_t block, char *data) {
    if (!disk_sanity_check(disk, block, data)) {
        return DISK_FAILURE;
    }
    if (lseek(disk->fd, block * BLOCK_SIZE, SEEK_SET) == -1) {
        error("errno: %d\n", errno);
        return DISK_FAILURE;
    }

    return write(disk->fd, data, BLOCK_SIZE);
}

/* Internal Functions */

/**
 * Perform sanity check before read or write operation by doing the following:
 *
 *  1. Check for valid disk.
 *
 *  2. Check for valid block.
 *
 *  3. Check for valid data.
 *
 * @param       disk        Pointer to Disk structure.
 * @param       block       Block number to perform operation on.
 * @param       data        Data buffer.
 *
 * @return      Whether or not it is safe to perform a read/write operation
 *              (true for safe, false for unsafe).
 **/
bool    disk_sanity_check(Disk *disk, size_t block, const char *data) {
    // invalid disk
    if (disk == NULL || disk->fd < 0) {
        return false;
    }

    // invalid block
    if (block >= disk->blocks) {
        return false;
    }

    // invalid data
    if (data == NULL) {
        return false;
    }

    return true;
}

/* vim: set expandtab sts=4 sw=4 ts=8 ft=c: */
