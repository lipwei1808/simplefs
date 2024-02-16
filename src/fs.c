/* fs.c: SimpleFS file system */

#include "sfs/fs.h"
#include "sfs/logging.h"
#include "sfs/utils.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

/* External Functions */

/**
 * Debug FileSystem by doing the following:
 *
 *  1. Read SuperBlock and report its information.
 *
 *  2. Read Inode Table and report information about each Inode.
 *
 * @param       disk        Pointer to Disk structure.
 **/
void    fs_debug(Disk *disk) {
    Block block;

    /* Read SuperBlock */
    if (disk_read(disk, 0, block.data) == DISK_FAILURE) {
        return;
    }

    printf("SuperBlock:\n");
    printf("    magic number is %s\n", block.super.magic_number == MAGIC_NUMBER ? "valid" : "invalid");
    printf("    %u blocks\n"         , block.super.blocks);
    printf("    %u inode blocks\n"   , block.super.inode_blocks);
    printf("    %u inodes\n"         , block.super.inodes);

    /* Read Inodes */
    for (int i = 1; i <= block.super.inode_blocks; i++) {
        Block blk;
        if (disk_read(disk, i , blk.data) == DISK_FAILURE) {
            error("Error reading disk on iteration %d\n", i);
            return;
        }
        
        // Iterate through all inodes in block
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            Inode inode = blk.inodes[j];
            if (inode.valid == 1) {
                uint32_t inode_number = (i - 1) * INODES_PER_BLOCK + j;
                int pointers = 0;
                for (int k = 0; k < POINTERS_PER_INODE; k++) {
                    if (inode.direct[k] == 0) continue;
                    pointers++;
                }
                printf("Inode %d\n", inode_number);
                printf("    size: %d bytes\n", inode.size);
                printf("    direct blocks: %d\n", pointers);
            }
        }
    }
}

/**
 * Format Disk by doing the following:
 *
 *  1. Write SuperBlock (with appropriate magic number, number of blocks,
 *  number of inode blocks, and number of inodes).
 *
 *  2. Clear all remaining blocks.
 *
 * Note: Do not format a mounted Disk!
 *
 * @param       fs      Pointer to FileSystem structure.
 * @param       disk    Pointer to Disk structure.
 * @return      Whether or not all disk operations were successful.
 **/
bool    fs_format(FileSystem *fs, Disk *disk) {
    assert(fs != NULL);
    assert(disk != NULL);
    uint32_t inodes = ceil(disk->blocks * 0.1);
    SuperBlock sp = {
        .magic_number = MAGIC_NUMBER,
        .blocks = disk->blocks,
        .inode_blocks = inodes,
        .inodes = inodes * INODES_PER_BLOCK,
    };
    Block blk;
    blk.super = sp;
    if (disk_write(disk, 0, blk.data) == DISK_FAILURE) {
        error("[fs_format]\n");
        return false;
    };

    // Create inode block
    Inode inode = {
        .valid = 0,
        .size = 0,
        .indirect = 0,
    };
    memset(inode.direct, 0, POINTERS_PER_INODE * sizeof(int));
    assert(sizeof(inode) == BLOCK_SIZE / INODES_PER_BLOCK);

    for (int i = 0; i < INODES_PER_BLOCK; i++) {
        blk.inodes[i] = inode;
    }
    assert(sizeof(blk) == BLOCK_SIZE);

    for (int i = 1; i <= inodes; i++) {
        if (disk_write(disk, i, blk.data) == DISK_FAILURE) {
            error("[fs_format]\n");
            return false;
        }
    }
    return false;
}

/**
 * Mount specified FileSystem to given Disk by doing the following:
 *
 *  1. Read and check SuperBlock (verify attributes).
 *
 *  2. Verify and record FileSystem disk attribute. 
 *
 *  3. Copy SuperBlock to FileSystem meta data attribute
 *
 *  4. Initialize FileSystem free blocks bitmap.
 *
 * Note: Do not mount a Disk that has already been mounted!
 *
 * @param       fs      Pointer to FileSystem structure.
 * @param       disk    Pointer to Disk structure.
 * @return      Whether or not the mount operation was successful.
 **/
bool    fs_mount(FileSystem *fs, Disk *disk) {
    assert(fs != NULL);
    assert(disk != NULL);
    Block blk;
    if (disk_read(disk, 0, blk.data) == DISK_FAILURE) {
        error("[fs_mount] error reading super block\n");
        return false;
    }

    if (blk.super.magic_number != MAGIC_NUMBER) {
        return false;
    }
    fs->disk = disk;
    memcpy(&fs->meta_data, &blk.super, sizeof(SuperBlock));

    fs_initialize_free_block_bitmap(fs);
    return false;
}

/**
 * Unmount FileSystem from internal Disk by doing the following:
 *
 *  1. Set FileSystem disk attribute.
 *
 *  2. Release free blocks bitmap.
 *
 * @param       fs      Pointer to FileSystem structure.
 **/
void    fs_unmount(FileSystem *fs) {
    assert(fs != NULL);
    assert(fs->free_blocks != NULL);
    free(fs->free_blocks);
    fs->disk = NULL;
}

/**
 * Allocate an Inode in the FileSystem Inode table by doing the following:
 *
 *  1. Search Inode table for free inode.
 *
 *  2. Reserve free inode in Inode table.
 *
 * Note: Be sure to record updates to Inode table to Disk.
 *
 * @param       fs      Pointer to FileSystem structure.
 * @return      Inode number of allocated Inode.
 **/
ssize_t fs_create(FileSystem *fs) {
    assert(fs != NULL);
    assert(fs->disk != NULL);
    int inode = -1;
    for (int i = 1; i <= fs->meta_data.inode_blocks; i++) {
        Block blk;
        if (disk_read(fs->disk, i, blk.data) != DISK_FAILURE) {
            error("[fs_create] read disk failure\n");
            return -1;
        }

        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            if (blk.inodes[j].valid == false) {
                blk.inodes[j].valid = true;
                inode = (i - 1) * INODES_PER_BLOCK + j;
                if (disk_write(fs->disk, i, blk.data) == DISK_FAILURE) {
                    return -1;
                }

                return inode;
            }
        }
    }

    return -1;
}

/**
 * Remove Inode and associated data from FileSystem by doing the following:
 *
 *  1. Load and check status of Inode.
 *
 *  2. Release any direct blocks.
 *
 *  3. Release any indirect blocks.
 *
 *  4. Mark Inode as free in Inode table.
 *
 * @param       fs              Pointer to FileSystem structure.
 * @param       inode_number    Inode to remove.
 * @return      Whether or not removing the specified Inode was successful.
 **/
bool    fs_remove(FileSystem *fs, size_t inode_number) {
    // Load Inode
    Inode* in = NULL;
    bool res = fs_load_inode(fs, inode_number, in);
    if (!res) {
        return false;
    }

    // Release direct blocks
    for (int i = 0; i < POINTERS_PER_INODE; i++) {
        int num = in->direct[i];
        if (num == 0) continue;

        fs->free_blocks[num] = false;
        in->direct[i] = 0;
    }

    // Release indirect blocks
    if (in->size > BLOCK_SIZE * POINTERS_PER_INODE) {
        assert(in->indirect > 0);
        Block indirect;
        if (disk_read(fs->disk, in->indirect, indirect.data) == DISK_FAILURE) {
            error("[fs_remove] error in reading indirect block\n");
            free(in);
            return false;
        }

        for (int i = 0; i < POINTERS_PER_BLOCK; i++) {
            int num = indirect.pointers[i];
            if (num == 0) continue;

            fs->free_blocks[num] = false;
        }
    }

    in->valid = false;
    in->indirect = 0;
    res = fs_save_inode(fs, inode_number, in);
    free(in);
    return res;
}

/**
 * Return size of specified Inode.
 *
 * @param       fs              Pointer to FileSystem structure.
 * @param       inode_number    Inode to remove.
 * @return      Size of specified Inode (-1 if does not exist).
 **/
ssize_t fs_stat(FileSystem *fs, size_t inode_number) {
    Inode* in;
    bool res = fs_load_inode(fs, inode_number, in);
    if (!res) {
        return -1;
    }

    int res = in->size;
    free(in);
    return res;
}

/**
 * Read from the specified Inode into the data buffer exactly length bytes
 * beginning from the specified offset by doing the following:
 *
 *  1. Load Inode information.
 *
 *  2. Continuously read blocks and copy data to buffer.
 *
 *  Note: Data is read from direct blocks first, and then from indirect blocks.
 *
 * @param       fs              Pointer to FileSystem structure.
 * @param       inode_number    Inode to read data from.
 * @param       data            Buffer to copy data to.
 * @param       length          Number of bytes to read.
 * @param       offset          Byte offset from which to begin reading.
 * @return      Number of bytes read (-1 on error).
 **/
ssize_t fs_read(FileSystem *fs, size_t inode_number, char *data, size_t length, size_t offset) {
    Inode* in;
    bool res = fs_load_inode(fs, inode_number, in);
    if (!res) {
        return -1;
    }

    for (int i = 0; i < POINTERS_PER_INODE; i++) {
        if (i)
    }
    return -1;
}

/**
 * Write to the specified Inode from the data buffer exactly length bytes
 * beginning from the specified offset by doing the following:
 *
 *  1. Load Inode information.
 *
 *  2. Continuously copy data from buffer to blocks.
 *
 *  Note: Data is read from direct blocks first, and then from indirect blocks.
 *
 * @param       fs              Pointer to FileSystem structure.
 * @param       inode_number    Inode to write data to.
 * @param       data            Buffer with data to copy
 * @param       length          Number of bytes to write.
 * @param       offset          Byte offset from which to begin writing.
 * @return      Number of bytes read (-1 on error).
 **/
ssize_t fs_write(FileSystem *fs, size_t inode_number, char *data, size_t length, size_t offset) {
    return -1;
}

bool fs_load_inode(FileSystem *fs, size_t inode_number, Inode *node) {
    assert(fs != NULL);
    assert(fs->disk != NULL);
    assert(node == NULL);

    size_t blk_number = inode_number / INODES_PER_BLOCK;
    size_t offset = inode_number - (INODES_PER_BLOCK * blk_number);
    Block blk;
    if (disk_read(fs->disk, blk_number, blk.data) == DISK_FAILURE) {
        error("[fs_load_inode] error reading block from disk\n");
        return false;
    }

    Inode in = blk.inodes[offset];
    if (!in.valid) {
        return false;
    }
    node = malloc(sizeof(Inode));
    memcpy(node, &in, sizeof(Inode));
    return true;
}

bool fs_save_inode(FileSystem *fs, size_t inode_number, Inode *node) {
    assert(fs != NULL);
    assert(fs->disk != NULL);
    assert(node != NULL);
    size_t blk_number = inode_number / INODES_PER_BLOCK;
    size_t offset = inode_number - (INODES_PER_BLOCK * blk_number);

    Block blk;
    if (disk_read(fs->disk, blk_number, blk.data) == DISK_FAILURE) {
        error("[fs_save_inode] error reading block from disk\n");
        return false;
    }

    blk.inodes[offset] = *node;
    if (disk_write(fs->disk, blk_number, blk.data) == DISK_FAILURE) {
        error("[fs_save_inode] error writing block to disk\n");
        return false;
    }

    return true;
}

void fs_initialize_free_block_bitmap(FileSystem *fs) {
    assert(fs != NULL);
    assert(fs->disk != NULL);

    uint32_t data_blocks = fs->meta_data.blocks - fs->meta_data.inode_blocks;
    bool* free_blocks = (bool*)malloc(sizeof(bool) * data_blocks);
    memset(free_blocks, 0, sizeof(bool) * data_blocks);
    for (int i = 1; i <= fs->meta_data.inode_blocks; i++) {
        Block blk;
        if (disk_read(fs->disk, i, blk.data) == DISK_FAILURE) {
            error("[fs_initialize_free_block_bitmap] error reading inode blocks\n");
            return;
        }
    
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            Inode in = blk.inodes[j];
            if (!in.valid) continue;

            for (int ptr = 0; ptr < POINTERS_PER_INODE; ptr++) {
                if (in.direct[ptr] != 0) {
                    int idx = in.direct[ptr] - 1 - fs->meta_data.inode_blocks;
                    free_blocks[idx] = true;
                }
            }
        }
    }

    fs->free_blocks = free_blocks;
}

/* vim: set expandtab sts=4 sw=4 ts=8 ft=c: */
