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
		perror("lstat");
		return ENOENT;
	} else if (st.st_mode & S_IFDIR) {
		printf("%s is a directory\n", local_file_path);
		return EISDIR;
	}

	// check if disk_path is valid
		// should either be a valid direcotry path
		// or a valid directory path followed by a file name that does not exist yet
	int path_index = 0;
	char token[EXT2_NAME_LEN];

  	// flag for whether the file name in local path is appended into disk path
  	char appended = 0;
	// special case when copy to root of image
	if (strlen(disk_path) == 1 && disk_path[0] == '/') {
		char *file_name = get_file_name(local_file_path);
		disk_path = concat_system_path(disk_path, file_name);
		free(file_name);
		appended = 1;
	}

 	// get root directory for absolute path
	path_index = get_next_token(token, disk_path, path_index);
	if(path_index == -1){
		printf("No such file or directory\n");
		return ENOENT;
	}

	struct ext2_inode *current_inode = &(inode_table[EXT2_ROOT_INO-1]);
	// printf("%d\n", current_inode);
	// record the last inode, since the path may point to a new path 
	// (new file name)
	struct ext2_inode *last_inode = current_inode;
  	int size = strlen(disk_path);
	while (path_index < size) {
		last_inode = current_inode;
		path_index = get_next_token(token, disk_path, path_index);
		if(path_index == -1){
			// path invalid
			printf("No such file or directory\n");
			return ENOENT;
		}

		current_inode = find_inode_2(token, strlen(token), current_inode, inode_table, disk);
		if (!current_inode) {
			// file does not exist
			if (path_index != size) {
		  		printf("No such file or directory\n");
		  		return ENOENT;
			} else {
				// case: the disk path has a new file name
				printf("case: copy to new file path %s\n", disk_path);
				break;
			}
		}

		if(current_inode->i_mode & EXT2_S_IFDIR){
			if (appended) {
				printf("Cannot overwrite a directory\n");
				exit(1);
			}
			// the directory path may not end up with '/'
			// check whether the disk path is a directory path
			if (path_index == size || path_index+1 == size) {
				// form the new file path for the file to be copied in
				char *file_name = get_file_name(local_file_path);
				disk_path = concat_system_path(disk_path, file_name);
				free(file_name);
				appended = 1;
				size = strlen(disk_path);
			}
			// normal directory entry during file path
			path_index += 1;
		}

		// Test both symlink and directory in middle of path (works because symlink masks over dir)
		if(current_inode->i_mode & EXT2_S_IFLNK) {
			if (path_index != size) {
				// file occured in the middle of a path --> invalid path
				printf("No such file or directory\n");
      			return ENOENT;
			} else {
				// case: override the existing file
				// no need to edit path
				printf("case: overwritting file at path %s\n", disk_path);
				create_file(-1, local_file_path, current_inode);
				// no need to modify directory entry or allocate new inode
				return 0;
			}
		}
	}
	// create the inode for file, and do file copy first
	unsigned int file_inode_number = allocate_inode();
	create_file(file_inode_number, local_file_path, NULL);
	// then create directory entry in the directory
	create_directory_entry(last_inode, file_inode_number, disk_path, 0);

	return 0;
}


void create_file(int file_inode_number, char *local_file_path, struct ext2_inode *file_inode) {
	if (!file_inode) {
		// get the file inode`
		file_inode = &(inode_table[file_inode_number - 1]);
		file_inode->i_links_count = 1;
	}
	// set type
	file_inode->i_mode = EXT2_S_IFREG;

	// get size
	struct stat st;
	lstat(local_file_path, &st);
	file_inode->i_size = (unsigned int) st.st_size;

	// copy file contents
	FILE *fp;
	if (!(fp = fopen(local_file_path, "rb"))) {
		perror("fopen");
		exit(1);
	} 

	// read one block at a time
 	char content_block[EXT2_BLOCK_SIZE];
 	// sectors(512 bytes) needed 
	unsigned int sector_needed = file_inode->i_size / 512;

	if (file_inode->i_size % 512) {
		sector_needed++;
		// should be even number
	}
	sector_needed += sector_needed % 2;


	file_inode->i_blocks = sector_needed;
	// printf("Inode %d with blocks(sectors) %d\n", file_inode_number, sector_needed);
	// newly created file will have one link

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

	// start copying file contents
	int block, num;
	int block_count = 0;
	char *block_pointer;
	char create_new_ones = 0;
	while (sector_needed > 0) {
		if (create_new_ones || !file_inode->i_block[block_count]) {
			create_new_ones = 1;
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
		block_pointer = (char *) (disk + (block)* EXT2_BLOCK_SIZE);
		// can't use strcpy because '\0'
		for (int i=0; i < num; i++) {
			block_pointer[i] = content_block[i];
		}
		printf("Num copied %d\n", num);
		sector_needed -= 2;
	}

	// set the next data block bit to be zero incase previous file uses more than blocks
	if (block_count <= 12) {
		// no indirection
		(file_inode->i_block)[block_count] = 0;
	} else {
		// indirection, set 0 start from index 1
		single_indirect[block_count - 12] = 0;
	}

}



