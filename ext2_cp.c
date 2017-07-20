#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"

unsigned char *disk;
struct ext2_group_desc *gd;
struct ext2_inode *inode_table;  
struct ext2_super_block *sb;


int get_next_token(char * token, char * path, int index);
struct ext2_inode * find_inode(char * name, int size, struct ext2_inode *inode, struct ext2_inode *inode_table, unsigned char * disk);
struct ext2_inode * find_inode_block(char * name, int size, struct ext2_inode *inode_table, unsigned char * disk, unsigned int block);
int path_equal(char * path, int size, struct ext2_dir_entry_2 * dir);
struct ext2_inode * find_inode_walk(int depth, int block, char * name, int size, struct ext2_inode *inode_table, unsigned char * disk);
int allocate_inode(void);
int allocate_data_block(void);
int block_taken(int index);
void set_block_bitmap(int index);
int inode_is_taken(int index);
void set_inode_bitmap(int index);
void create_file(int file_inode_number, char *local_file_path, struct ext2_inode *file_inode);
void create_directory_entry(struct ext2_inode *dir_inode, int file_inode_number, char *disk_path);
// struct ext2_dir_entry_2 * create_directory_entry_walk(struct ext2_inode *dir_inode, int index, int depth, char *file_name, int file_inode_number);
char *get_file_name(char *path);
unsigned int get_size_dir_entry(unsigned int path_length);
char *concat_system_path(char *dirpath, char *file_name);
struct ext2_dir_entry_2 *create_directory_entry_walk_2(unsigned int *block_num, unsigned int depth, unsigned int size_needed);



int main(int argc, char **argv) {
	if (argc != 4) {
        fprintf(stderr, "Usage: ext2_cp <image file name> <source file path> <target absolute path>\n");
        exit(1);	
    }


	// check if the disk image is valid
    int fd = open(argv[1], O_RDWR);
	disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	sb = (struct ext2_super_block *) (disk + EXT2_BLOCK_SIZE);
	gd = (struct ext2_group_desc *)(disk + 2*EXT2_BLOCK_SIZE);
	inode_table  = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);

	// check local file path is a valid file path
	// local file should exist and not be a directory
	char *local_file_path = argv[2];
	char base_name[EXT2_NAME_LEN];
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

		current_inode = find_inode(token, strlen(token), current_inode, inode_table, disk);
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

	// printf("Address of last inode: %d\n", last_inode);
	// create the inode for file, and do file copy first
	unsigned int file_inode_number = allocate_inode();
	create_file(file_inode_number, local_file_path, NULL);
	// then create directory entry in the directory
	create_directory_entry(last_inode, file_inode_number, disk_path);

	return 0;
}

char *concat_system_path(char *dirpath, char *file_name) {
	char has_slash = dirpath[strlen(dirpath)-1] == '/';
	char *concatenated_path = malloc(strlen(dirpath) + strlen(file_name) + 1 + 1 - has_slash);
	strcpy(concatenated_path, dirpath);
	if (!has_slash) {
		strcat(concatenated_path, "/");
	}
	strcat(concatenated_path, file_name);
	return concatenated_path;
}

char *get_file_name(char *path) {
	char *path_copy = malloc(strlen(path) + 1);
	strcpy(path_copy, path);
	char *base_name = basename(path_copy);
	char *to_return = malloc(strlen(base_name) + 1);
	strcpy(to_return, base_name);
	free(path_copy);
	return to_return;
}

void create_directory_entry(struct ext2_inode *dir_inode, int file_inode_number, char *disk_path) {
	struct ext2_dir_entry_2 *result = NULL;
	char *file_name = get_file_name(disk_path);
	unsigned size_needed = get_size_dir_entry(strlen(file_name));
	int depth;
	// for (int i=0; i < 15; i++) {
	// 	depth = (i >= 12) ? (i - 11):0;
	// 	if (create_directory_entry_walk(dir_inode, i, depth, file_name, file_inode_number)) {
	// 		return;
	// 	}
	// }
	unsigned int *block_num_addr;
	for (int i=0; i < 15; i++) {
		block_num_addr = &(dir_inode->i_block[i]);
		// printf("%p\n", block_num_addr);
		depth = (i >= 12) ? (i - 11):0;
		if ((result = create_directory_entry_walk_2(block_num_addr, depth, size_needed))) {
			// use result to set up
			result->inode = file_inode_number;
			result->name_len = strlen(file_name);
			result->file_type = EXT2_FT_REG_FILE;
			for (int j=0; j < result->name_len; j++) {
				result->name[j] = file_name[j];
			}
			return;
		}
	}
	printf("Should not be here!\n");
	return;
}

struct ext2_dir_entry_2 *create_directory_entry_walk_2(unsigned int *block_num, unsigned int depth, unsigned int size_needed) {
	int degug = 0;
	// printf("DEBUG 0\n");
	struct ext2_dir_entry_2 *result = NULL;
	unsigned ints_per_block = EXT2_BLOCK_SIZE / sizeof(unsigned int);
	// printf("The dir address is %p\n", block_num);
	if (depth) {
		// printf("DEBUG 1\n");
		if (*block_num) {
			// the secondary directory entry blocks array is already initiated
			unsigned int *blocks = (unsigned int *) (disk + *block_num * EXT2_BLOCK_SIZE);
			int index = 0;
			while (index < ints_per_block) {
				result = create_directory_entry_walk_2(blocks + index, depth-1, size_needed);
				if (result) {
					return result;
				}
				index++;
			} 
			return NULL;
		} else {
			// initiate the secondary directory entry blocks array
			*block_num = allocate_data_block();
			memset((disk + *block_num * EXT2_BLOCK_SIZE), 0, EXT2_BLOCK_SIZE);
			return create_directory_entry_walk_2(
				(unsigned int *) (disk + *block_num * EXT2_BLOCK_SIZE), 
				depth-1, 
				size_needed);
		}
	} else {
		// printf("DEBUG 2\n");
		if (!*block_num) {
			// printf("DEBUG %d\n", degug++);
			*block_num = allocate_data_block();
			memset((disk + *block_num * EXT2_BLOCK_SIZE), 0, EXT2_BLOCK_SIZE);
		}
		struct ext2_dir_entry_2 *dir_entry = (struct ext2_dir_entry_2 *) (disk + *block_num * EXT2_BLOCK_SIZE);
		if (dir_entry->rec_len) {
			// printf("DEBUG 3\n");
			// the block has some entries
			// find the last entry
			unsigned int size_used = dir_entry->rec_len;
			while (size_used < EXT2_BLOCK_SIZE) {
				dir_entry = (struct ext2_dir_entry_2 *) ((char *) dir_entry + dir_entry->rec_len);
				size_used += dir_entry->rec_len;
			}
			unsigned int actual_last_entry_size = get_size_dir_entry(dir_entry->name_len);
			if (dir_entry->rec_len >= actual_last_entry_size + size_needed) {
				result = (struct ext2_dir_entry_2 *) ((char *) dir_entry + actual_last_entry_size);
				result->rec_len = dir_entry->rec_len - actual_last_entry_size;
				dir_entry->rec_len = actual_last_entry_size;
			}
		} else {
			// printf("DEBUG 4\n");
			result = dir_entry;
			result->rec_len = EXT2_BLOCK_SIZE;
		}
		return result;
	}
}


unsigned int get_size_dir_entry(unsigned int path_length) {
	path_length += 8;
	if (path_length % 4) {
		path_length += (4 - path_length % 4);
	}
	return path_length;
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
		sector_needed += sector_needed % 2;
	}

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
	unsigned int total_blocks_for_single_indirection = 12 + EXT2_BLOCK_SIZE / sizeof(unsigned int);
	if (block_count <= 12) {
		// no indirection
		(file_inode->i_block)[block_count] = 0;
	} else {
		// indirection, set 0 start from index 1
		single_indirect[block_count - 12] = 0;
	}

}

int allocate_data_block() {
	if (!gd->bg_free_blocks_count) {
		return -1;
	}

	int i=0;
	while (i < sb->s_blocks_count && block_taken(i)) {
		i++;
	}

	if (i == sb->s_blocks_count) {
		// should not be here
		return -1;
	}

	set_block_bitmap(i);
	// memset((disk + EXT2_BLOCK_SIZE * (i+1)), 0, EXT2_BLOCK_SIZE);
	gd->bg_free_blocks_count--;
	return i+1;
}

int block_taken(int index) {
	char *bitmap = (char *) (disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
	char sec = index / 8;
	char mask = 1 << (index % 8);
	return bitmap[sec] & mask;
}

void set_block_bitmap(int index) {
	char *bitmap = (char *) (disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
	char sec = index / 8;
	char mask = 1 << (index % 8);
	bitmap[sec] |= mask;	
}

int allocate_inode() {
	int i = EXT2_GOOD_OLD_FIRST_INO;

	if (!gd->bg_free_inodes_count) {
		return -1;
	}

	// find empty slot from bit map
	while (i < sb->s_inodes_count && inode_is_taken(i)) {
		i++;
	}

	if (i == sb->s_inodes_count) {
		// should not reach here
		return -1;
	}

	set_inode_bitmap(i);
	struct ext2_inode *inode_table = (struct ext2_inode *) (disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);
	memset(&(inode_table[i]), 0, sizeof(struct ext2_inode));
	gd->bg_free_inodes_count--;

	// return the inode number which is one larger than index
	return i+1;
}

int inode_is_taken(int index) {
	char *bitmap = (char *) (disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);
	char sec = index / 8;
	char mask = 1 << (index % 8);
	return bitmap[sec] & mask;
}

void set_inode_bitmap(int index) {
	char *bitmap = (char *) (disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);
	char sec = index / 8;
	char mask = 1 << (index % 8);
	bitmap[sec] |= mask;
}

int get_next_token(char * token, char * path, int index){
  if(index == 0){
    if(path[index] == '/'){
      token[0] = '/';
      token[1] = '\0';
      return 1;
    }else{
      return -1;
    }
  }
  int t_i = 0;
  while(path[index] != '\0' && path[index] != '/'){
    if(t_i == EXT2_NAME_LEN){
      return -1;
    }
    token[t_i] = path[index];
    ++t_i;
    ++index;
  }
  token[t_i] = '\0';
  return index;
}


struct ext2_inode * find_inode(char * name, int size, struct ext2_inode *inode, struct ext2_inode *inode_table, unsigned char * disk){
  struct ext2_inode * inode_ptr = NULL;
  unsigned int bptr;
  for(int i = 0; i < 15; i++){
    bptr = inode->i_block[i];
    if(bptr){
      if(i < 12){
        inode_ptr = find_inode_block(name, size, inode_table, disk, bptr);
        if(inode_ptr){
          return inode_ptr;
        }
      }else{
        inode_ptr = find_inode_walk(i - 12, inode->i_block[i], name, size, inode_table, disk);
        if(inode_ptr){
          return inode_ptr;
        }
      }
    }
  }
  return inode_ptr;
}

struct ext2_inode * find_inode_block(char * name, int size, struct ext2_inode *inode_table, unsigned char * disk, unsigned int block){
  // Init dir entry vars in the block
  char * dirptr = (char *)(disk + block * EXT2_BLOCK_SIZE);
  char * next_block = dirptr + EXT2_BLOCK_SIZE;
  struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
  
  // Cycle through dir entries in the block
  while(dirptr != next_block){
//  	printf("Stuck here with rec_len %d\n", dir->rec_len);
    if(path_equal(name, size, dir)){
      return &(inode_table[dir->inode - 1]);
    }
    dirptr += dir->rec_len;
    dir = (struct ext2_dir_entry_2 *) dirptr;
  }
  return NULL;
}

int path_equal(char * path, int size, struct ext2_dir_entry_2 * dir){
  int true = size == dir->name_len;
  int index = 0;
  while(index < size && true){
    true = true && (path[index] == dir->name[index]);
    ++index;
  }
  return true;
}

struct ext2_inode * find_inode_walk(int depth, int block, char * name, int size, struct ext2_inode *inode_table, unsigned char * disk){
  struct ext2_inode * inode_ptr = NULL;
  unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
  if(depth == 0){
    for(int i = 0; i < 15; i++){
      if(inode[i]){
        inode_ptr = find_inode_block(name, size, inode_table, disk, inode[i]);
        if(inode_ptr){
          return inode_ptr;
        }
      }
    }
  } else {
    for(int i = 0; i < 15; i++){
      if(inode[i]){
        inode_ptr = find_inode_walk(depth - 1, inode[i], name, size, inode_table, disk);
        if(inode_ptr){
          return inode_ptr;
        }
         
      }
    }
  }
 return inode_ptr; 
}

