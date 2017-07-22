#include "helper.h"


int create_hard_link(struct ext2_inode *dir_inode, char *link_file_path, char *source_file_path);
int create_soft_link(struct ext2_inode *dir_inode, char *link_file_path, char *source_file_path);
unsigned int create_softlink_file(char *source_file_path);
struct ext2_inode *get_inode_for_file_path(char *source_file_path);


int main(int argc, char **argv) {
	char *link_file_path = NULL;
	char *source_file_path = NULL;
	// flag for -s command
	char soft_link = FALSE;
	// check input argument
	if (argc == 4) {
		// create hard link
		link_file_path = argv[2];
		source_file_path = argv[3];
    } else if (argc == 5 && !strcmp("-s", argv[2])) {
    	// create soft link
    	link_file_path = argv[3];
    	source_file_path = argv[4];
    	soft_link = TRUE;
    } else {
    	show_error(EXT2LN, 1);
    }

	// check if the disk image is valid
    int fd = open(argv[1], O_RDWR);
    init(fd);

	int path_index = 0;
	char token[EXT2_NAME_LEN];
	// first check if link file path exists and if it's valid
	path_index = get_next_token(token, link_file_path, path_index);
	if (path_index == -1) {
		show_error(DOESNOTEXIST, ENOENT);
	}
	if (strlen(link_file_path) == 1 && link_file_path[0] == '/') {
		show_error(ISADIRECTORY, EISDIR);
	}

	struct ext2_inode *current_inode = &(inode_table[EXT2_ROOT_INO - 1]);
	struct ext2_inode *last_inode;
	int size = strlen(link_file_path);
	while (path_index < size) {
		last_inode = current_inode;
		path_index = get_next_token(token, link_file_path, path_index);

		if (path_index == -1) {
			// path too long
			show_error(DOESNOTEXIST, ENOENT);
		}

		current_inode = find_inode_2(token, strlen(token), current_inode, inode_table, disk);
		if (!current_inode) {
			// reach not existing path component
			if (path_index != size) {
				// the component is in the middle of the path
				show_error(DOESNOTEXIST, ENOENT);
			} else {
				// the component is at the end of the path
				// the file path does not exist yet, but valid
				if (soft_link) {
					return create_soft_link(last_inode, link_file_path, source_file_path);
				} else {
					// hard link will check for source file path, may return error value
					return create_hard_link(last_inode, link_file_path, source_file_path);
				}
			}
		}

		if (current_inode->i_mode & EXT2_S_IFDIR) {
			// directory component in the path
			if (path_index == size || path_index + 1 == size) {
				// the component is at the end of path
				// directory exists at link path
				show_error(ISADIRECTORY, EISDIR);
			} else {
				// read slash
				path_index++;
			}
		}

		if (current_inode->i_mode & EXT2_S_IFLNK) {
			// reach a file
			if (path_index != size) {
				// file in the middle of path -> invalid path
				show_error(DOESNOTEXIST, ENOENT);
			} else {	
				// file at the end of path, link file path already exists
				show_error(ALREADYEXIST, EEXIST);
			}
		}
	}
	return 0;
}

/*
 * Create a hard link in the given directory inode, with the source file path given. 
 * 
 */
int create_hard_link(struct ext2_inode *dir_inode, char *link_file_path, char *source_file_path) {
	// get source_file_inode at given path
	struct ext2_inode *source_file_inode = get_inode_for_file_path(source_file_path);
	if (source_file_inode) {
		// source path is valid
		if (source_file_inode->i_mode & EXT2_S_IFDIR) {
			// source is directory
				show_error(ISADIRECTORY, EISDIR);
		} else {
			// source is file
			unsigned int source_inode_number = inode_number(source_file_inode);
			char *file_name = get_file_name(link_file_path);
			// create the directory entry
			create_directory_entry(dir_inode, source_inode_number, file_name, 0);
			// increment link count for the file inode
			source_file_inode->i_links_count++;
		}
	} else {
		// file path invalid
		show_error(DOESNOTEXIST, ENOENT);
	}
	return 0;
}

/*
 * Create the soft link file in side given directory inode with content as the given
 * source file path.
 */
int create_soft_link(struct ext2_inode *dir_inode, char *link_file_path, char *source_file_path) {
	unsigned link_file_inode_number = create_softlink_file(source_file_path);
	char *link_file_name = get_file_name(link_file_path);
	create_directory_entry(dir_inode, link_file_inode_number, link_file_name, 1);	
	return 0;
}


/* 
 * Create a link file with the content as given source path.
 * Return the inode number of the created file.
 */
unsigned int create_softlink_file(char *source_file_path) {
	// allocate inode
	unsigned int source_file_inode_number = allocate_inode();
	struct ext2_inode *source_inode = &(inode_table[source_file_inode_number - 1]);
	unsigned int size = strlen(source_file_path);
	unsigned int sector_needed = sector_needed_from_size(size);
	init_inode(source_inode, EXT2_S_IFLNK, size, 1, sector_needed);
	
	// initiate indirect table if needed
	unsigned int *single_indirect = NULL;
	if (sector_needed > 24) {
		source_inode->i_block[12] = allocate_data_block();
		single_indirect = (unsigned int *) (disk + source_inode->i_block[12] * EXT2_BLOCK_SIZE);
	}

	// writing path into content blocks
	unsigned int block_count = 0;
	unsigned int index = 0;
	unsigned int block, num;
	while (sector_needed > 0) {
		// allocate content block
		block = allocate_data_block();
		if (block_count < 12) {
			source_inode->i_block[block_count] = block;
		} else {
			single_indirect[block_count - 12] = block;
		}
		block_count++;
		// determine the size to write
		num = source_inode->i_size - index;
		if (num > EXT2_BLOCK_SIZE) {
			num = EXT2_BLOCK_SIZE;
		}
		// copy content
		copy_content(source_file_path + index, (char *) (disk + block * EXT2_BLOCK_SIZE), num);
		index += num;
		sector_needed -= 2;
	}
	zero_terminate_block_array(block_count, source_inode, single_indirect);
	return source_file_inode_number;
}

struct ext2_inode *get_inode_for_file_path(char *source_file_path) {
	int path_index = 0;
	char token[EXT2_NAME_LEN];

	// first check if first file path exists and if it's valid
	path_index = get_next_token(token, source_file_path, path_index);
	if (path_index == -1) {
		return NULL;
	}

	struct ext2_inode *current_inode = &(inode_table[EXT2_ROOT_INO - 1]);
	struct ext2_inode *last_inode;
	int size = strlen(source_file_path);
	while (path_index < size) {
		last_inode = current_inode;
		path_index = get_next_token(token, source_file_path, path_index);
		if (path_index == -1) {
			// path too long
			return NULL;
		}
		current_inode = find_inode_2(token, strlen(token), current_inode, inode_table, disk);
		// reach not existing
		if (!current_inode || ((current_inode->i_mode & EXT2_S_IFLNK) && path_index != size)) {
			return NULL;
		}
		if (current_inode->i_mode & EXT2_S_IFDIR) {
			path_index++;
		}
	}

	return current_inode;
}

