/*
  Simple File System

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.

*/

#include "params.h"
#include "block.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include "inode.h"

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"

#define SFS_MAGIC_NUM 1707

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint32_t num_data_blocks; // Total number of data blocks on disk.
	uint32_t num_free_blocks; // Total number of free blocks.
	uint32_t num_inodes; // Total number of inodes on disk.
	uint32_t bitmap_inode_blocks;
	uint32_t bitmap_data_blocks;
	uint32_t inode_root;  // Root directory.
} sfs_superblock;

/*
 * Use the root directory to get the full path for the input relative path
 */
static void sfs_fullpath(char buffer[PATH_MAX], const char *path)
{
    strcpy(buffer, SFS_DATA->diskfile);
    strncat(buffer, path, PATH_MAX);
}


///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *sfs_init(struct fuse_conn_info *conn)
{
    fprintf(stderr, "in bb-init\n");
    log_msg("\nsfs_init()\n");
    
    log_conn(conn);
    log_fuse_context(fuse_get_context());

    disk_open(SFS_DATA->diskfile);
    struct stat *statbuf = (struct stat*)malloc(sizeof(struct stat));
    lstat(SFS_DATA->diskfile, statbuf);

    // Check for first time initialization.
    if (statbuf->st_size == 0) {

    	// Step 1: Write super block to disk file
    	sfs_superblock sb = {
    			.magic = SFS_MAGIC_NUM,
    			.num_data_blocks = SFS_NBLOCKS_DATA,
				.num_free_blocks = SFS_NBLOCKS_DATA,
				.num_inodes = SFS_NINODES,
				.bitmap_inode_blocks = SFS_BLOCK_INODE_BITMAP,
				.bitmap_data_blocks = SFS_BLOCK_DATA_BITMAP,
				.inode_root = 0
    	};

    	block_write_padded(SFS_BLOCK_SUPERBLOCK, &sb, sizeof(sfs_superblock));

    	//Step 2: Write inode bitmap
    	int i = 0;
    	char bitmap_inodes[BLOCK_SIZE];
    	memset(bitmap_inodes, '1', sizeof(bitmap_inodes));
    	for (i = 0; i < SFS_NBLOCKS_INODE_BITMAP; ++i) {
        	block_write((SFS_BLOCK_INODE_BITMAP + i), bitmap_inodes);
    	}

    	//Step 3: Write data bitmap
    	char bitmap_data[BLOCK_SIZE];
    	memset(bitmap_data, '1', sizeof(bitmap_data));
    	for (i = 0; i < SFS_NBLOCKS_DATA_BITMAP; ++i) {
        	block_write((SFS_BLOCK_DATA_BITMAP + i), bitmap_data);
    	}

    	//Step 4: Write inode blocks
    	char buffer_inode[BLOCK_SIZE];
    	memset(buffer_inode, '0', sizeof(buffer_inode));
    	for (i = 0; i < SFS_NBLOCKS_INODE; ++i) {
        	block_write((SFS_BLOCK_INODES + i), buffer_inode);
    	}

    	//Step 5: Write data blocks
    	char buffer_data[BLOCK_SIZE];
    	memset(buffer_data, '0', sizeof(buffer_data));
    	for (i = 0; i < SFS_NBLOCKS_DATA; ++i) {
        	block_write((SFS_BLOCK_DATA + i), buffer_data);
    	}

    	//Step 6: Initialize the root inode
    	if (block_read(SFS_BLOCK_INODE_BITMAP, bitmap_inodes) > 0) {
    		bitmap_inodes[0] = '0';
    		block_write(SFS_BLOCK_INODE_BITMAP, bitmap_inodes);
    	}

		if (block_read(SFS_BLOCK_DATA_BITMAP, bitmap_data) > 0) {
			bitmap_data[0] = '0';
			block_write(SFS_BLOCK_DATA_BITMAP, bitmap_data);
		}

		sfs_inode_t inode;
		memset(&inode, 0, sizeof(inode));
		inode.atime = inode.ctime = inode.mtime = time(NULL);
		inode.nblocks = 1;
		inode.ino = 0;
		inode.blocks[0] = SFS_BLOCK_DATA;
		inode.size = 0;
		inode.nlink = 0;
		inode.mode = S_IFDIR;

		block_write_padded(SFS_BLOCK_INODES, &inode, sizeof(sfs_inode_t));
    }

    // Here we start the init process

    // Step 1: Cache the state of inodes availability in fuse context

    SFS_DATA->state_inodes = (sfs_free_list*)malloc(SFS_NINODES * sizeof(sfs_free_list));
    memset(SFS_DATA->state_inodes, 0, SFS_NINODES * sizeof(sfs_free_list));

    int i = 0, inodes_cached = 0;
	char bitmap_inodes[BLOCK_SIZE];
	int num_used_inodes = 0;
	for (i = 0; i < SFS_NBLOCKS_INODE_BITMAP; ++i) {
		block_read((SFS_BLOCK_INODE_BITMAP + i), bitmap_inodes);

		int block_ptr = 0;
		while((block_ptr < BLOCK_SIZE) && (inodes_cached < SFS_NINODES)) {
			sfs_free_list *node = SFS_DATA->state_inodes + inodes_cached;
			node->id = inodes_cached;
			INIT_LIST_HEAD(&(node->node));
			if (bitmap_inodes[block_ptr] == '1') {
				if (SFS_DATA->free_inodes == NULL) {
					SFS_DATA->free_inodes = &(node->node);
				} else {
					list_add_tail(&(node->node), SFS_DATA->free_inodes);
				}
			} else {
				num_used_inodes++;
			}

			++inodes_cached;
			++block_ptr;
		}
	}

    log_msg("\nsfs_init() num_used_inodes = %d", num_used_inodes);

    // Step 2: Cache the state of data block's availability in fuse context

    SFS_DATA->state_data_blocks = (sfs_free_list*)malloc(SFS_NBLOCKS_DATA * sizeof(sfs_free_list));
    memset(SFS_DATA->state_data_blocks, 0, SFS_NBLOCKS_DATA * sizeof(sfs_free_list));

	int data_blocks_cached = 0;
	char bitmap_data[BLOCK_SIZE];
	int num_used_data_blocks = 0;
	for (i = 0; i < SFS_NBLOCKS_DATA_BITMAP; ++i) {
		block_read((SFS_BLOCK_DATA_BITMAP + i), bitmap_data);

		int block_ptr = 0;
		while ((block_ptr < BLOCK_SIZE) && (data_blocks_cached < SFS_NBLOCKS_DATA)) {
			sfs_free_list *node = SFS_DATA->state_data_blocks + data_blocks_cached;
			node->id = data_blocks_cached;
			INIT_LIST_HEAD(&(node->node));
			if (bitmap_data[block_ptr] == '1') {
				if (SFS_DATA->free_data_blocks == NULL) {
				    log_msg("\nsfs_init() here it is null");
					SFS_DATA->free_data_blocks = &(node->node);
				} else {
					list_add_tail(&(node->node), SFS_DATA->free_data_blocks);
				}
			} else {
				++num_used_data_blocks;
			}

			++data_blocks_cached;
			++block_ptr;
		}
	}

    log_msg("\nsfs_init() num_used_data_blocks = %d", num_used_data_blocks);

    // Step 3: Cache root's inode number
    char buffer_super_block[BLOCK_SIZE];
	block_read(SFS_BLOCK_SUPERBLOCK, buffer_super_block);
	sfs_superblock sb;
	memcpy(&sb, buffer_super_block, sizeof(sb));

	SFS_DATA->ino_root = sb.inode_root;
    log_msg("\nsfs_init() ino_root = %d", SFS_DATA->ino_root);

    return SFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void sfs_destroy(void *userdata)
{
    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);
    disk_close();

    free(SFS_DATA->state_inodes);
    SFS_DATA->state_inodes = NULL;

    free(SFS_DATA->state_data_blocks);
    SFS_DATA->state_data_blocks = NULL;

    SFS_DATA->free_inodes = NULL;
    SFS_DATA->free_data_blocks = NULL;
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n",
	  path, statbuf);
    
    uint32_t ino = path_2_ino(path);
    if (ino != SFS_INVALID_INO) {
    	log_msg("\nsfs_getattr path found");
    	sfs_inode_t inode;
    	get_inode(ino, &inode);

    	fill_stat_from_ino(&inode, statbuf);
    } else {
    	log_msg("\nsfs_getattr path not found");
    	retstat = -ENOENT;
    }

    return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
	    path, mode, fi);
    
    uint32_t ino = create_inode(path, mode);
    log_msg("\nFile creation success inode = %d", ino);

    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);
    retstat = remove_inode(path);
    
    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int sfs_open(const char *path, struct fuse_file_info *fi)
{
    int retstat = -ENOENT;
    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n",
	    path, fi);

	uint32_t ino = path_2_ino(path);
	if (ino != SFS_INVALID_INO) {
		sfs_inode_t inode;
		get_inode(ino, &inode);
		if (S_ISREG(inode.mode)) {
			retstat = 0;
		}
	} else {
		log_msg("\nNot a valid file");
	}
    
    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
    
    // No saved data related to any file which needs to be freed.

    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);

	uint32_t ino = path_2_ino(path);
	if (ino != SFS_INVALID_INO) {
		log_msg("\nsfs_read path found");
		sfs_inode_t inode;
		get_inode(ino, &inode);

		log_msg("\nsfs_read got the inode");

		retstat = read_inode(&inode, buf, size, offset);
		log_msg("\nData read = %s", buf);
	} else {
		log_msg("\nsfs_read path not found");
		retstat = -ENOENT;
	}

    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);

	uint32_t ino = path_2_ino(path);
	if (ino != SFS_INVALID_INO) {
		log_msg("\nsfs_write path found");
		sfs_inode_t inode;
		get_inode(ino, &inode);

		retstat = write_inode(&inode, buf, size, offset);
	} else {
		log_msg("\nsfs_write path not found");
		retstat = -ENOENT;
	}
    
    return retstat;
}


/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
	    path, mode);

    uint32_t ino = create_inode(path, mode);
    log_msg("\nFile creation success inode = %d", ino);
    
    return retstat;
}


/** Remove a directory */
int sfs_rmdir(const char *path)
{
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
	    path);
    
    
    return retstat;
}


/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n",
	  path, fi);

	uint32_t ino = path_2_ino(path);
	if (ino != SFS_INVALID_INO) {
		sfs_inode_t inode;
		get_inode(ino, &inode);
		if (S_ISDIR(inode.mode)) {
			retstat = 0;
		}
	} else {
		log_msg("\nNot a valid file");
	}
    
    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
	       struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nsfs_readdir(path=\"%s\")\n", path);

    filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	uint32_t ino = path_2_ino(path);
	if (ino != SFS_INVALID_INO) {
		log_msg("\nsfs_readdir path found");
		sfs_inode_t inode;
		get_inode(ino, &inode);

		int num_dentries = (inode.size / SFS_DENTRY_SIZE);
		sfs_dentry_t* dentries = malloc(sizeof(sfs_dentry_t) * num_dentries);
	    read_dentries(&inode, dentries);

	    int i = 0;
	    for (i = 0; i < num_dentries; ++i) {
	    	filler(buf, dentries[i].name, NULL, 0);
	    }

	    free(dentries);
	} else {
		log_msg("\nsfs_readdir path not found");
	}

    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;

    
    return retstat;
}

struct fuse_operations sfs_oper = {
  .init = sfs_init,
  .destroy = sfs_destroy,

  .getattr = sfs_getattr,
  .create = sfs_create,
  .unlink = sfs_unlink,
  .open = sfs_open,
  .release = sfs_release,
  .read = sfs_read,
  .write = sfs_write,

  .rmdir = sfs_rmdir,
  .mkdir = sfs_mkdir,

  .opendir = sfs_opendir,
  .readdir = sfs_readdir,
  .releasedir = sfs_releasedir
};

void sfs_usage()
{
    fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct sfs_state *sfs_data;
    
    // sanity checking on the command line
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
	sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL) {
	perror("main calloc");
	abort();
    }

    // Pull the diskfile and save it in internal data
    sfs_data->diskfile = realpath(argv[argc-2], NULL); // Save the absolute path of the diskfile
    printf("%s", sfs_data->diskfile);
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    
    sfs_data->logfile = log_open();
    sfs_data->free_data_blocks = NULL;
    sfs_data->free_inodes = NULL;
    
    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}
