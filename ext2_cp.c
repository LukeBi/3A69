#include "helper.h"

void create_file(int file_inode_number, char *local_file_path, struct ext2_inode *file_inode);

int main(int argc, char **argv) {
	if (argc != 4) {
		show_error(EXT2CP, 1);	
    }
	// check if the disk image is valid
    int fd = open(argv[1], O_RDWR);
	init(fd);
	// check local file path is a valid file path
	// local file should exist and not be a directory
	char *local_file_path = argv[2];
	char *disk_path = argv[3];
	struct stat st;
	if (lstat(local_file_path, &st)) {
		show_error(DOESNOTEXIST, ENOENT);
	} else if (st.st_mode & S_IFDIR) {
		show_error(ISADIRECTORY, EISDIR);
	}

	char token[EXT2_NAME_LEN];
	struct ext2_inode *disk_inode = fetch_last(disk_path, token, FALSE);
	int file_inode_number = 0;
	struct ext2_inode* parent_dir_inode = disk_inode;
	char *result_file_name_source = local_file_path;
	struct ext2_inode *argument_inode = NULL;
	if (disk_inode) {
		// path is only root
		struct ext2_inode *file_inode = find_inode(token, strlen(token), disk_inode);
		if (file_inode) {
			if (file_inode->i_mode & EXT2_S_IFDIR) {
				// valid directory path on image
				parent_dir_inode = file_inode;
			} else if (disk_path[strlen(disk_path) != '/']) {
				// valid file path
				file_inode_number = inode_number(file_inode);
				argument_inode = file_inode;
				result_file_name_source = disk_path;
			} else {
				// invalid path (has '/' at the end)
				show_error(DOESNOTEXIST, ENOENT);
			}
		} else {
			if (disk_path[strlen(disk_path) != '/']) {
				// valid new file path
				result_file_name_source = disk_path;
			} else {
				// invalid path
				show_error(DOESNOTEXIST, ENOENT);
			}
		}
	} else {
		// path is root
		parent_dir_inode = &(inode_table[EXT2_ROOT_INO-1]);
	}
	if (!file_inode_number) {
		file_inode_number = allocate_inode();
	}
	create_file(file_inode_number, local_file_path, argument_inode);
	create_directory_entry(parent_dir_inode, file_inode_number, get_file_name(result_file_name_source), FALSE);
	return 0;
}


void create_file(int file_inode_number, char *local_file_path, struct ext2_inode *file_inode) {
	if (!file_inode) {
		// get the file inode`
		file_inode = &(inode_table[file_inode_number - 1]);
		file_inode->i_links_count = 1;
	}
	// get size
	struct stat st;
	lstat(local_file_path, &st);
	// copy file contents
	FILE *fp;
	if (!(fp = fopen(local_file_path, "rb"))) {
		perror("fopen");
		exit(1);
	} 
	// read one block at a time
 	char content_block[EXT2_BLOCK_SIZE];
	unsigned int sector_needed = sector_needed_from_size(st.st_size);
	// get indirect inode if needed	
	unsigned int *single_indirect;
	char clear = 0;
	if (sector_needed > 24) {
		if (!file_inode->i_block[12]) {
			file_inode->i_block[12] = allocate_data_block();
			clear = 1;
		}
		single_indirect = (unsigned int *) (disk + file_inode->i_block[12] * EXT2_BLOCK_SIZE);
		if (clear) {
			memset(single_indirect, 0, EXT2_BLOCK_SIZE);
		}
		// may should not to initialize all to 0
	}
	init_inode(file_inode, EXT2_S_IFREG, (unsigned int) st.st_size, file_inode->i_links_count, sector_needed);
	// start copying file contents
	int block, num;
	int block_count = 0;
	char create_new_ones = FALSE;
	while (sector_needed > 0) {
		if (create_new_ones || !file_inode->i_block[block_count]) {
			create_new_ones = TRUE;
			block = allocate_data_block();
			if (block_count < 12) {
				(file_inode->i_block)[block_count] = block;
			} else {
				single_indirect[block_count-12] = block;
			}
		} else {
			if (block_count < 12) {
				block = (file_inode->i_block)[block_count];
			} else {
				block = single_indirect[block_count-12];
			}
		}
		block_count++;
		num = fread(content_block, 1, EXT2_BLOCK_SIZE, fp);
		copy_content(content_block, (char *) disk + block * EXT2_BLOCK_SIZE, num);
		sector_needed -= 2;
	}
	zero_terminate_block_array(block_count, file_inode, single_indirect);
}

