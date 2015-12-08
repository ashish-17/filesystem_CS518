/*
 * inode.c
 *
 *  Created on: Nov 27, 2015
 *      Author: ashish
 */

#include "inode.h"
#include "params.h"
#include "block.h"
#include <errno.h>

 // Local functions
void read_dentry_from_block(uint32_t block_id, sfs_dentry_t* dentries, int num_entries);

uint32_t path_2_ino_internal(const char *path, uint32_t ino_parent);

void free_ino(uint32_t ino);

uint32_t get_ino();

void free_block_no(uint32_t b_no);

uint32_t get_block_no();

void update_inode_bitmap(uint32_t ino, char ch);

void update_block_bitmap(uint32_t bno, char ch);

void update_inode_data(uint32_t ino, sfs_inode_t *inode);

void update_block_data(uint32_t bno, char* buffer);

void create_dentry(const char *name, sfs_inode_t *inode, uint32_t ino_parent);

void remove_dentry(sfs_inode_t *inode, uint32_t ino_parent);

// Function defs
uint32_t path_2_ino(const char *path) {
	if (strcmp(path, "/") == 0) {
		return (SFS_DATA->ino_root);
	} else if (*path == '/') {
		return path_2_ino_internal(path+1, SFS_DATA->ino_root);
	} else {
		log_msg("\npath_2_no invalid path");
	}

	return SFS_INVALID_INO;
}

uint32_t path_2_ino_internal(const char *path, uint32_t ino_parent) {

	uint32_t ino_path = SFS_INVALID_INO;
	char buffer[SFS_DENTRY_SIZE * SFS_NINODES];
	memset(buffer, 0, sizeof(buffer));

	sfs_inode_t inode;
	get_inode(ino_parent, &inode);

	int num_dentries = (inode.size / SFS_DENTRY_SIZE);
	if (num_dentries > 0) {
		log_msg("\npath_2_ino_internal num_dentries=%d", num_dentries);

		sfs_dentry_t* dentries = malloc(sizeof(sfs_dentry_t) * num_dentries);
	    read_dentries(&inode, dentries);

	    int i = 0;
	    for (i = 0; i < num_dentries; ++i) {
			log_msg("\npath_2_ino_internal Entry%d = %s, path=%s", i, dentries[i].name,path);
	    	if (strcmp(dentries[i].name, path) == 0) {
	    		ino_path = dentries[i].inode_number;
	    		log_msg("\npath_2_ino: Dentry found ino = %d", ino_path);

	    		break;
	    	}
	    }
	    free(dentries);
	}

	log_msg("\npath_2_ino: test outer");
	return ino_path;
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

uint32_t create_inode(const char *path, mode_t mode) {
	uint32_t ino_path = path_2_ino(path);
	if (ino_path == SFS_INVALID_INO) {
		ino_path = get_ino();
		uint32_t block_no = get_block_no();

		if ((ino_path != SFS_INVALID_INO) && (block_no != SFS_INVALID_BLOCK_NO)) {
			// Step 1: Update the inode bitmap to reflect availability
			update_inode_bitmap(ino_path, '0');

			// Step 2: Update Data n=bitmap
			update_block_bitmap(block_no, '0');

			// Step 3: Create Inode
			sfs_inode_t inode;
			memset(&inode, 0, sizeof(inode));
			inode.atime = inode.ctime = inode.mtime = time(NULL);
			inode.nblocks = 1;
			inode.ino = ino_path;
			inode.blocks[0] = block_no;
			inode.size = 0;
			inode.nlink = 0;
			inode.mode = mode;

			// Step 4: Write inode to disk
			update_inode_data(ino_path, &inode);

			// Step 5: Create a directory entry
			create_dentry(path+1, &inode, path_2_ino("/"));

			return inode.ino;
		}
	} else {
		log_msg("\nError path already exists!");
	}

	return SFS_INVALID_INO;
}

int remove_inode(const char *path) {
	uint32_t ino_path = path_2_ino(path);
	if (ino_path != SFS_INVALID_INO) {
		sfs_inode_t inode_data;
		get_inode(ino_path, &inode_data);

		int num_blocks = inode_data.nblocks;
		while (num_blocks > 0) {

			free_block_no(inode_data.blocks[num_blocks - 1]);
			update_block_bitmap(inode_data.blocks[num_blocks - 1], '1');

			--num_blocks;
		}

		free_ino(inode_data.ino);
		update_inode_bitmap(inode_data.ino, '1');

		log_msg("inode removed..now proceeding to remove dentry");
		remove_dentry(&inode_data, SFS_DATA->ino_root);

		return 0;
	} else {
		log_msg("\nError no such path exists!");
	}

	return -ENOENT;
}

int write_inode(sfs_inode_t *inode_data, const char* buffer, int size, int offset) {

	int capacity = (SFS_NDIR_BLOCKS - (offset / BLOCK_SIZE))*BLOCK_SIZE - (offset % BLOCK_SIZE);
	if (size > capacity) {
		log_msg("Can't write a file of this size");
		return 0;
	}

	int i = 0;
	int orig_offset = offset;
	char tmp_buf[BLOCK_SIZE];
	int bytes_written = 0;
	int num_new_blocks = 0;
	int start_block_idx = (offset / BLOCK_SIZE);
	offset = offset % BLOCK_SIZE;

	for (i = start_block_idx; (bytes_written < size) && (i < SFS_NDIR_BLOCKS);++i) {
		if (i >= inode_data->nblocks) {
			inode_data->blocks[i] = get_block_no();
			++num_new_blocks;
			log_msg("\nAllocated a new block for file");
		}

		if (offset != 0) {
			int bytes_to_write = (BLOCK_SIZE - offset) > size ? size : (BLOCK_SIZE - offset);
			block_read(SFS_BLOCK_DATA + inode_data->blocks[i], tmp_buf);
			memcpy(tmp_buf + offset, buffer, bytes_to_write);
			update_block_data(inode_data->blocks[i], tmp_buf);

			bytes_written += bytes_to_write;

			log_msg("\norig offset = %d Offset = %d written %d bytes", orig_offset, offset, bytes_written);

			offset = 0;
		} else {
			int bytes_to_write = (size - bytes_written) > BLOCK_SIZE ? BLOCK_SIZE : (size - bytes_written);
			memcpy(tmp_buf, buffer + bytes_written, bytes_to_write);
			update_block_data(inode_data->blocks[i], tmp_buf);

			log_msg("\nUpdated block %d offset = %d num bytes written = %d",inode_data->blocks[i], offset, bytes_to_write);

			bytes_written += bytes_to_write;
		}
	}

	inode_data->nblocks += num_new_blocks;
	inode_data->size = orig_offset + size;

	update_inode_data(inode_data->ino, inode_data);

	return bytes_written;
}

int read_inode(sfs_inode_t *inode_data, char* buffer, int size, int offset) {

	log_msg("\nread_inode");
	int i = 0;
	int orig_offset = offset;
	char tmp_buf[BLOCK_SIZE];
	int bytes_read = 0;
	int num_new_blocks = 0;
	int start_block_idx = (offset / BLOCK_SIZE);
	offset = offset % BLOCK_SIZE;

	for (i = start_block_idx; (bytes_read < inode_data->size) && (i < SFS_NDIR_BLOCKS);++i) {

		if (offset != 0) {
			int bytes_to_read = (BLOCK_SIZE - offset) > inode_data->size ? inode_data->size : (BLOCK_SIZE - inode_data->size);
			block_read(SFS_BLOCK_DATA + inode_data->blocks[i], tmp_buf);
			memcpy(buffer, tmp_buf + offset, bytes_to_read);

			bytes_read += bytes_to_read;

			log_msg("Offset = %d read %d bytes", offset, bytes_read);

			offset = 0;
		} else {
			int bytes_to_read = (inode_data->size - bytes_read) > BLOCK_SIZE ? BLOCK_SIZE : (inode_data->size - bytes_to_read);
			block_read(SFS_BLOCK_DATA + inode_data->blocks[i], tmp_buf);
			log_msg("\ntmp_buf = %s", tmp_buf);
			memcpy(buffer + bytes_read, tmp_buf, bytes_to_read);

			//log_msg("\nRead block %d offset = %d num bytes read = %d",inode_data->blocks[i], offset, bytes_to_read);

			log_msg("\nRead Buffer = %s", buffer);
			log_msg("\ntmp_buf1 = %s", tmp_buf);
			bytes_read += bytes_to_read;
		}
	}

	return bytes_read;
}

void fill_stat_from_ino(const sfs_inode_t* inode, struct stat *statbuf) {
	statbuf->st_dev = 0;
	statbuf->st_ino = inode->ino;
	statbuf->st_mode = inode->mode;
	statbuf->st_nlink = inode->nlink;
	statbuf->st_uid = getuid();
	statbuf->st_gid = getgid();
	statbuf->st_rdev = 0;
	statbuf->st_size = inode->size;
	statbuf->st_blksize = BLOCK_SIZE;
	statbuf->st_blocks = inode->nblocks;
	statbuf->st_atime = inode->atime;
	statbuf->st_mtime = inode->mtime;
	statbuf->st_ctime = inode->ctime;
}

void read_dentries(sfs_inode_t *inode_data, sfs_dentry_t* dentries) {
	if (S_ISDIR(inode_data->mode)) {
		int num_blocks_read = 0;
		int num_bytes_read = 0;
		int num_entries = 0;
		int entry_offset = 0;

		while ((num_blocks_read < inode_data->nblocks) && (num_bytes_read < inode_data->size)) {
			if (inode_data->size - num_bytes_read < BLOCK_SIZE) {
				num_entries = ((inode_data->size - num_bytes_read) / SFS_DENTRY_SIZE);
			} else {
				num_entries = (BLOCK_SIZE / SFS_DENTRY_SIZE);
			}

			log_msg("\n read_dentries num_entries=%d", num_entries);

			if (num_blocks_read < SFS_NDIR_BLOCKS) {
				read_dentry_from_block(inode_data->blocks[num_blocks_read], dentries + entry_offset, num_entries);
			}

			++num_blocks_read;
			num_bytes_read += (num_entries * SFS_DENTRY_SIZE);
			entry_offset += num_entries;
		}
	} else {
	    log_msg("\n Invalid inode number %d, not a directory", inode_data->ino);
	}
}

void read_dentry_from_block(uint32_t block_id, sfs_dentry_t* dentries, int num_entries) {

	char buffer[BLOCK_SIZE];
	block_read(SFS_BLOCK_DATA + block_id, buffer);

	int entries_read = 0;
	int bytes_read = 0;
	while ((bytes_read < BLOCK_SIZE) && (entries_read < num_entries)) {
		log_msg("\nread_dentry_from_block Entries read = %d", entries_read);
		memcpy(dentries + entries_read, buffer + bytes_read, sizeof(sfs_dentry_t));
	    ++entries_read;
	    bytes_read += SFS_DENTRY_SIZE;
	}
}

void free_ino(uint32_t ino) {
	if (ino < SFS_NINODES) {
		if ((SFS_DATA->state_inodes[ino].node.next
				== SFS_DATA->state_inodes[ino].node.prev)
				&& (SFS_DATA->free_inodes != &(SFS_DATA->state_inodes[ino].node))) {

			list_add_tail(&(SFS_DATA->state_inodes[ino].node), SFS_DATA->free_inodes);
			log_msg("\nSuccess: Inode added to the free list");

		} else {
			log_msg("\nError: Inode already in the free list");
		}
	}
}

uint32_t get_ino() {
	if (list_empty(SFS_DATA->free_inodes))  {
		log_msg("\nError: Inode limit reached!!!");
	} else {
		list_t *ino_node = SFS_DATA->free_inodes;
		list_del(SFS_DATA->free_inodes);

		SFS_DATA->free_inodes = ino_node->next;

		INIT_LIST_HEAD(ino_node);

		sfs_free_list *ptr = list_entry(ino_node, sfs_free_list, node);
		if (ptr != NULL) {
			log_msg("\nSuccess: Free ino found = %d", ptr->id);
			return ptr->id;
		} else {
			log_msg("\nError: Free ino couldn't be retrieved");
		}
	}

	return SFS_INVALID_INO;
}

void free_block_no(uint32_t b_no) {
	if (b_no < SFS_NBLOCKS_DATA) {
		if ((SFS_DATA->state_data_blocks[b_no].node.next
				== SFS_DATA->state_data_blocks[b_no].node.prev)
				&& (SFS_DATA->free_data_blocks != &(SFS_DATA->state_data_blocks[b_no].node))) {

			list_add_tail(&(SFS_DATA->state_data_blocks[b_no].node), SFS_DATA->free_data_blocks);
			log_msg("\nSuccess: Data block added to the free list");
		} else {
			log_msg("\nError: Data block already in the free list");
		}
	}
}

uint32_t get_block_no() {
	if (list_empty(SFS_DATA->free_data_blocks))  {
		log_msg("\nError: Data blocks limit reached!!!");
	} else {
		list_t *data_block_node = SFS_DATA->free_data_blocks;
		list_del(SFS_DATA->free_data_blocks);

		SFS_DATA->free_data_blocks = data_block_node->next;

		INIT_LIST_HEAD(data_block_node);

		sfs_free_list *ptr = list_entry(data_block_node, sfs_free_list, node);
		if (ptr != NULL) {
			log_msg("\nSuccess: Free data block found = %d", ptr->id);
			return ptr->id;
		} else {
			log_msg("\nError: Free data block couldn't be retrieved");
		}
	}

	return SFS_INVALID_BLOCK_NO;
}

void update_inode_bitmap(uint32_t ino, char ch) {
	int i = 0;
	char buffer[BLOCK_SIZE];
	block_read(SFS_BLOCK_INODE_BITMAP + ino / BLOCK_SIZE, buffer);
	buffer[ino] = ch;
	block_write(SFS_BLOCK_INODE_BITMAP + ino / BLOCK_SIZE, buffer);

	log_msg("\nupdate_inode_bitmap Successful update");
}

void update_block_bitmap(uint32_t bno, char ch) {
	char buffer[BLOCK_SIZE];
	block_read(SFS_BLOCK_DATA_BITMAP + bno / BLOCK_SIZE, buffer);
	buffer[bno] = ch;
	block_write(SFS_BLOCK_DATA_BITMAP + bno / BLOCK_SIZE, buffer);

	log_msg("\nupdate_block_bitmap Successful update");
}

void update_inode_data(uint32_t ino, sfs_inode_t *inode) {
	char buffer[BLOCK_SIZE];
	inode->mtime = time(NULL);

	block_read(SFS_BLOCK_INODES + ino / (BLOCK_SIZE / SFS_INODE_SIZE), buffer);
	memcpy(buffer + ((ino % (BLOCK_SIZE / SFS_INODE_SIZE)) * SFS_INODE_SIZE), inode, sizeof(sfs_inode_t));
	block_write(SFS_BLOCK_INODES + ino / (BLOCK_SIZE / SFS_INODE_SIZE), buffer);

	log_msg("\nupdate_inode_data Successful update");
}

void update_block_data(uint32_t bno, char* buffer) {
	block_write(SFS_BLOCK_DATA + bno, buffer);

	log_msg("\nupdate_block_data Successful update");
}

void create_dentry(const char *name, sfs_inode_t *inode, uint32_t ino_parent) {
	log_msg("\ncreate_dentry path=%s ino = %d ino_parent=%d", name, inode->ino, ino_parent);
	sfs_inode_t inode_parent;
	get_inode(ino_parent, &inode_parent);

	sfs_dentry_t dentry;
	dentry.inode_number = inode->ino;
	strcpy(dentry.name, name);

	char buffer[BLOCK_SIZE];

	int num_dentries = (inode_parent.size / SFS_DENTRY_SIZE);
	int idx = num_dentries / (BLOCK_SIZE / SFS_DENTRY_SIZE);
	int int_idx = num_dentries % (BLOCK_SIZE / SFS_DENTRY_SIZE);

	if ((int_idx == 0) && (num_dentries != 0)) {
		inode_parent.blocks[idx] = get_block_no();
		update_block_bitmap(inode_parent.blocks[idx], '0');
		inode_parent.nblocks += 1;
	}

	block_read(SFS_BLOCK_DATA + inode_parent.blocks[idx], buffer);
	memcpy(buffer + (int_idx * SFS_DENTRY_SIZE), &dentry, sizeof(sfs_dentry_t));
	block_write(SFS_BLOCK_DATA + inode_parent.blocks[idx], buffer);

	inode_parent.size += SFS_DENTRY_SIZE;
	update_inode_data(inode_parent.ino, &inode_parent);
}

void remove_dentry(sfs_inode_t *inode, uint32_t ino_parent) {
	sfs_inode_t inode_parent;
	get_inode(ino_parent, &inode_parent);
	if (S_ISDIR(inode_parent.mode)) {
		int num_blocks_read = 0;
		int num_bytes_read = 0;
		int num_entries = 0;
		int entry_offset = 0;

		while ((num_blocks_read < inode_parent.nblocks)
				&& (num_bytes_read < inode_parent.size)) {
			if (inode_parent.size - num_bytes_read < BLOCK_SIZE) {
				num_entries = ((inode_parent.size - num_bytes_read) / SFS_DENTRY_SIZE);
			} else {
				num_entries = (BLOCK_SIZE / SFS_DENTRY_SIZE);
			}

			log_msg("\n read_dentries num_entries=%d", num_entries);

			if (num_blocks_read < SFS_NDIR_BLOCKS) {
				char buffer[BLOCK_SIZE];
				block_read(SFS_BLOCK_DATA + inode_parent.blocks[num_blocks_read], buffer);

				int entries_read = 0;
				int bytes_read = 0;
				while ((bytes_read < BLOCK_SIZE) && (entries_read < num_entries)) {
					log_msg("\nread_dentry_from_block Entries read = %d", entries_read);
					sfs_dentry_t dentry;
					memcpy(&dentry, buffer + bytes_read, sizeof(sfs_dentry_t));
					if (dentry.inode_number == inode->ino) {
						log_msg("\nEntry to be deleted found");
						// Now i am going to overwrite it with the last dentry

						int total_entries = (inode_parent.size / SFS_DENTRY_SIZE);
						if (total_entries > 1) {
							int idx = (total_entries - 1) / (BLOCK_SIZE / SFS_DENTRY_SIZE);
							int int_idx = (total_entries - 1) % (BLOCK_SIZE / SFS_DENTRY_SIZE);

							char buffer_last[BLOCK_SIZE];
							block_read(SFS_BLOCK_DATA + inode_parent.blocks[idx], buffer_last);
							sfs_dentry_t dentry_last;
							memcpy(&dentry_last, buffer_last + SFS_DENTRY_SIZE * int_idx, sizeof(sfs_dentry_t));

							if (int_idx == 0) {
								inode_parent.nblocks--;
								free_block_no(inode_parent.blocks[idx]);
								update_block_bitmap(inode_parent.blocks[idx], '1');
							}

							memcpy(buffer + bytes_read, &dentry_last, sizeof(sfs_dentry_t));
							update_block_data(inode_parent.blocks[num_blocks_read], buffer);
							inode_parent.size -= SFS_DENTRY_SIZE;
						} else {
							inode_parent.size -= SFS_DENTRY_SIZE;
						}

						update_inode_data(inode_parent.ino, &inode_parent);
						log_msg("\n Item deleted successfully");
						return;
					}
					++entries_read;
					bytes_read += SFS_DENTRY_SIZE;
				}
			}

			++num_blocks_read;
			num_bytes_read += (num_entries * SFS_DENTRY_SIZE);
			entry_offset += num_entries;
		}
	} else {
		log_msg("\n Invalid inode number %d, not a directory", inode_parent.ino);
	}
}
