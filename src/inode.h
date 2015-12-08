/*
 * inode.h
 *
 *  Created on: Nov 27, 2015
 *      Author: ashish
 */

#ifndef SRC_INODE_H_
#define SRC_INODE_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fuse.h>

#define SFS_NDIR_BLOCKS		12 						// Number of direct blocks
#define SFS_IND_BLOCK		SFS_NDIR_BLOCKS 		// Index of indirect block
#define SFS_DIND_BLOCK		(SFS_IND_BLOCK + 1) 	// Index of double indirect block
#define SFS_TIND_BLOCK		(SFS_DIND_BLOCK + 1) 	// Index of Triple indirect block
#define SFS_N_BLOCKS		(SFS_TIND_BLOCK + 1) 	// Total number of blocks

#define SFS_NIND_BLOCKS		(BLOCK_SIZE / 4) 					// 128 Blocks = 64KB
#define SFS_NDIND_BLOCKS 	((BLOCK_SIZE / 4) * SFS_NIND_BLOCKS) // 16384 Blocks = 8MB
#define SFS_NTIND_BLOCKS 	((BLOCK_SIZE / 4) * SFS_NDIND_BLOCKS) // 2097152 blocks = 1GB

#define SFS_NINODES 256 // Max number of inodes/files
#define SFS_INODE_SIZE 128 // Size in bytes of inode struct, below mentioned struct should be < 128bytes
#define SFS_NBLOCKS_INODE (SFS_NINODES / (BLOCK_SIZE / SFS_INODE_SIZE)) // Number of blocks for inodes = 64
#define SFS_NBLOCKS_DATA (SFS_NINODES * SFS_NDIND_BLOCKS) // Enough blocks to at least accommodate double indirection

#define SFS_NBLOCKS_INODE_BITMAP 1 // Can store 512*8 inodes (More than enough for now)
#define SFS_NBLOCKS_DATA_BITMAP (SFS_NBLOCKS_DATA / (BLOCK_SIZE * 8)) // 1024 blocks for this bitmap

#define SFS_BLOCK_SUPERBLOCK 0 // 0
#define SFS_BLOCK_INODE_BITMAP (SFS_BLOCK_SUPERBLOCK + 1) // Only 1 super block. = 1
#define SFS_BLOCK_DATA_BITMAP (SFS_BLOCK_INODE_BITMAP + SFS_NBLOCKS_INODE_BITMAP) // = 2
#define SFS_BLOCK_INODES (SFS_BLOCK_DATA_BITMAP + SFS_NBLOCKS_DATA_BITMAP) // 2 + 1024 = 1026
#define SFS_BLOCK_DATA (SFS_BLOCK_INODES + SFS_NBLOCKS_INODE) // 1026 + 64

#define SFS_MAX_LENGTH_FILE_NAME 32
#define SFS_DENTRY_SIZE 64

#define SFS_INVALID_INO (SFS_NINODES)
#define SFS_INVALID_BLOCK_NO (SFS_NBLOCKS_DATA)

typedef struct __attribute__((packed)) {
	uint32_t   	ino;     /* inode number */
	uint32_t	mode;	/* Flags related to file mode (Dir/file/link)*/
    uint32_t   	nlink;   /* number of hard links */
    uint32_t    size;    /* total size, in bytes */
    uint32_t  	nblocks;  /* number of 512B blocks allocated */
    uint32_t    atime;   /* time of last access */
    uint32_t   	mtime;   /* time of last modification */
    uint32_t    ctime;   /* time of last status change */
	uint32_t 	blocks[SFS_N_BLOCKS]; 	/* Size  = 4 * 15 = 60 bytes */
} sfs_inode_t;

typedef struct __attribute__((packed)) {
	uint32_t inode_number;
	char name[SFS_MAX_LENGTH_FILE_NAME]; /* File name */
} sfs_dentry_t;

uint32_t path_2_ino(const char* path);

void get_inode(uint32_t ino, sfs_inode_t *inode_data);

uint32_t create_inode(const char *path, mode_t mode);

int remove_inode(const char *path);

int write_inode(sfs_inode_t *inode_data, const char* buffer, int size, int offset);

int read_inode(sfs_inode_t *inode_data, char* buffer, int size, int offset);

void fill_stat_from_ino(const sfs_inode_t* inode, struct stat *statbuf);

void read_dentries(sfs_inode_t *inode_data, sfs_dentry_t* dentries);

#endif /* SRC_INODE_H_ */
