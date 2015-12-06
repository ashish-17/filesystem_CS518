/*
 * inode.c
 *
 *  Created on: Nov 27, 2015
 *      Author: ashish
 */

#include "inode.h"
#include "params.h"
#include "block.h"
#include <fuse.h>

uint32_t path_2_ino(const char *path) {
	if (strcmp(path, "/") == 0) {
		return (SFS_DATA->ino_root);
	}

	return SFS_INVALID_INO;
}

void get_inode(uint32_t ino, sfs_inode_t *inode_data) {
	if (ino < SFS_NINODES) {
		if ((SFS_DATA->state_inodes[ino].node.next == SFS_DATA->state_inodes[ino].node.prev) &&
			(SFS_DATA->free_inodes != &(SFS_DATA->state_inodes[ino].node))) {
			int block_offset = ino / (BLOCK_SIZE / SFS_INODE_SIZE);
			int inside_block_offset = ino % (BLOCK_SIZE / SFS_INODE_SIZE);

			char buffer[BLOCK_SIZE];
	    	block_read(SFS_BLOCK_INODES + block_offset, buffer);

		    log_msg("\n here block_offset=%d inside_block_offset=%d", block_offset, inside_block_offset);
	    	memcpy(inode_data, buffer + inside_block_offset*SFS_INODE_SIZE, sizeof(sfs_inode_t));
		    log_msg("\n inode number %d successfully found", inode_data->ino);
		} else {
		    log_msg("\n inode number %d not in use", ino);
		}
	} else {
	    log_msg("\n Invalid inode number %d", ino);
	}
}
