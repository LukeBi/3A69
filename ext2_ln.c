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
#include "helper.h"


int create_hard_link(struct ext2_inode *dir_inode, char *link_file_path, char *source_file_path);
int create_soft_link(struct ext2_inode *dir_inode, char *link_file_path, char *source_file_path);
unsigned int create_softlink_file(char *source_file_path);
struct ext2_inode *get_inode_for_file_path(char *source_file_path);
void create_directory_entry(struct ext2_inode *dir_inode, unsigned int file_inode_number, char *file_name, char is_link);
struct ext2_dir_entry_2 *create_directory_entry_walk_2(unsigned int *block_num, unsigned int depth, unsigned int size_needed);
char *get_file_name(char *path);
unsigned int get_size_dir_entry(unsigned int path_length);
struct ext2_inode * find_inode_2(char * name, int size, struct ext2_inode *inode, struct ext2_inode *inode_table, unsigned char * disk);
struct ext2_inode * find_inode_block_2(char * name, int size, struct ext2_inode *inode_table, unsigned char * disk, unsigned int block);
struct ext2_inode * find_inode_walk_2(int depth, int block, char * name, int size, struct ext2_inode *inode_table, unsigned char * disk);
int allocate_data_block(void);
int block_taken(int index);
void set_block_bitmap(int index); 
int allocate_inode(void);
int inode_is_taken(int index); 
void set_inode_bitmap(int index);



int main(int argc, char **argv) {
	char *link_file_path;
	char *source_file_path;
	// flag for -s command
	char soft_link = 0;
	// check input argument
	if (argc == 4) {
		// create hard link
		link_file_path = argv[2];
		source_file_path = argv[3];
    } else if (argc == 5 && !strcmp("-s", argv[2])) {
    	// create soft link
    	link_file_path = argv[3];
    	source_file_path = argv[4];
    	soft_link = 1;
    } else {
        fprintf(stderr, "Usage: ext2_ls <image file name> [-s] <link file path> <target absolute path>\n");
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

	int path_index = 0;
	char token[EXT2_NAME_LEN];

	// first check if link file path exists and if it's valid
	path_index = get_next_token(token, link_file_path, path_index);
	if (path_index == -1) {
		fprintf(stderr, "ln: %s: No such file or directory\n", link_file_path);
		return ENOENT;
	}
	if (strlen(link_file_path) == 1 && link_file_path[0] == '/') {
		fprintf(stderr, "ln: %s: Directory exists\n", link_file_path);
		return ENOENT;
	}

	struct ext2_inode *current_inode = &(inode_table[EXT2_ROOT_INO - 1]);
	struct ext2_inode *last_inode;
	int size = strlen(link_file_path);
	while (path_index < size) {
		last_inode = current_inode;
		path_index = get_next_token(token, link_file_path, path_index);

		if (path_index == -1) {
			// path too long
			fprintf(stderr, "ln: %s: No such file or directory\n", link_file_path);
			return ENOENT;
		}

		current_inode = find_inode_2(token, strlen(token), current_inode, inode_table, disk);
		if (!current_inode) {
			// reach not existing path component
			if (path_index != size) {
				// the component is in the middle of the path
				printf("Here\n");
				fprintf(stderr, "ln: %s: No such file or directory\n", link_file_path);
				return ENOENT;
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
				printf("HERE\n");
				fprintf(stderr, "ln: %s: Directory exists\n", link_file_path);
				return EISDIR;
			} else {
				// read slash
				path_index++;
			}
		}

		if (current_inode->i_mode & EXT2_S_IFLNK) {
			// reach a file
			if (path_index != size) {
				// file in the middle of path -> invalid path
				fprintf(stderr, "ln: %s: No such file or directory\n", link_file_path);
				return ENOENT;
			} else {	
				// file at the end of path, link file path already exists
				fprintf(stderr, "ln: %s: File exists\n", link_file_path);
				return EEXIST;
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
	printf("create_hard_link\n");
	// get source_file_inode at given path
	struct ext2_inode *source_file_inode = get_inode_for_file_path(source_file_path);
	if (source_file_inode) {
		// source path is valid
		if (source_file_inode->i_mode & EXT2_S_IFDIR) {
			// source is directory
			fprintf(stderr, "ln: %s: Directory exists\n", source_file_path);
			return EISDIR;
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
		fprintf(stderr, "ln: %s: No such file or directory\n", link_file_path);
		return ENOENT;
	}
	return 0;
}

/*
 * Create the soft link file in side given directory inode with content as the given
 * source file path.
 */
int create_soft_link(struct ext2_inode *dir_inode, char *link_file_path, char *source_file_path) {
	printf("create_soft\n");
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
	printf("create_softlink_file\n");
	// allocate inode
	unsigned int source_file_inode_number = allocate_inode();
	printf("Inode number is %d\n", source_file_inode_number);
	struct ext2_inode *source_inode = &(inode_table[source_file_inode_number - 1]);

	// set inode info
	source_inode->i_mode = EXT2_S_IFLNK;
	source_inode->i_size = strlen(source_file_path);
	source_inode->i_links_count = 1;

	unsigned int sector_needed = source_inode->i_size / 512;
	if (source_inode->i_size % 512) {
		sector_needed++;
	}
	sector_needed += sector_needed % 2;
	source_inode->i_blocks = sector_needed;
	
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
	char *block_pointer;
	while (sector_needed > 0) {
		// allocate content block
		block = allocate_data_block();
		if (block_count < 12) {
			source_inode->i_block[block_count] = block;
		} else {
			single_indirect[block_count - 12] = block;
		}
		block_count++;

		block_pointer = (char *) (disk + block * EXT2_BLOCK_SIZE);
		// determine the size to write
		num = source_inode->i_size - index;
		if (num > EXT2_BLOCK_SIZE) {
			num = EXT2_BLOCK_SIZE;
		}
		// copy content
		for (int i=0; i < num; i++) {
			block_pointer[i] = source_file_path[index++];
		}

		sector_needed -= 2;
	}

	// set 0 after the last used node
	if (block_count <= 12) {
		source_inode->i_block[block_count] = 0;
	} else {
		single_indirect[block_count - block_count] = 0;
	}

	return source_file_inode_number;
}

struct ext2_inode *get_inode_for_file_path(char *source_file_path) {
	printf("get_inode_for_file_path\n");
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
		if (!current_inode) {
			return NULL;
		}

		if (current_inode->i_mode & EXT2_S_IFDIR) {
			path_index++;
		}

		if (current_inode->i_mode & EXT2_S_IFLNK && path_index != size) {
			return NULL;
		}
	}

	return current_inode;
}


/* 
 * Create a directory entry inside given directory inode, with given file inode number
 * and file name.
 */
void create_directory_entry(struct ext2_inode *dir_inode, unsigned int file_inode_number, char *file_name, char is_link) {
	printf("create_directory_entry\n");
	struct ext2_dir_entry_2 *result = NULL;
	unsigned size_needed = get_size_dir_entry(strlen(file_name));
	int depth;
	unsigned int *block_num_addr;
	for (int i=0; i < 15; i++) {
		block_num_addr = &(dir_inode->i_block[i]);
		// printf("%p\n", block_num_addr);
		depth = (i >= 12) ? (i - 11):0;
		// get pointer for result directory entry
		if ((result = create_directory_entry_walk_2(block_num_addr, depth, size_needed))) {
			// use result to set up
			result->inode = file_inode_number;
			result->name_len = strlen(file_name);
			if (is_link) {
				result->file_type = EXT2_FT_SYMLINK;
			} else {
				result->file_type = EXT2_FT_REG_FILE;
			}
			for (int j=0; j < result->name_len; j++) {
				result->name[j] = file_name[j];
			}
			return;
		}
	}
	// no space available
	printf("Should not be here!\n");
	return;
}

struct ext2_dir_entry_2 *create_directory_entry_walk_2(unsigned int *block_num, unsigned int depth, unsigned int size_needed) {
	printf("create_directory_entry_walk_2\n");
	//	int degug = 0;
	// printf("DEBUG 0\n");
	struct ext2_dir_entry_2 *result = NULL;
	// entries per block
	unsigned ints_per_block = EXT2_BLOCK_SIZE / sizeof(unsigned int);
	// printf("The dir address is %p\n", block_num);
	// if need indirection
	if (depth) {
		if (!*block_num) {
			// if not allocated, allocate space first
			*block_num = allocate_data_block();
			memset((disk + *block_num * EXT2_BLOCK_SIZE), 0, EXT2_BLOCK_SIZE);
		}
		// printf("DEBUG 1\n");
		// the secondary directory entry blocks array is already initiated
		unsigned int *blocks = (unsigned int *) (disk + *block_num * EXT2_BLOCK_SIZE);
		int index = 0;
		// go through the block list
		while (index < ints_per_block) {
			result = create_directory_entry_walk_2(blocks + index, depth-1, size_needed);
			if (result) {
				return result;
			}
			index++;
		} 
		// no space available at the current directory entry
		return NULL;
	} else {
		// printf("DEBUG 2\n");
		if (!*block_num) {
			// printf("DEBUG %d\n", degug++);
			*block_num = allocate_data_block();
			memset((disk + *block_num * EXT2_BLOCK_SIZE), 0, EXT2_BLOCK_SIZE);
		}
		struct ext2_dir_entry_2 *dir_entry = (struct ext2_dir_entry_2 *) (disk + *block_num * EXT2_BLOCK_SIZE);
		if (dir_entry->rec_len) {
			// the block has some entries
			// find the last entry
			// printf("DEBUG 3\n");
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
			// the block entry is newly created
			// printf("DEBUG 4\n");
			result = dir_entry;
			result->rec_len = EXT2_BLOCK_SIZE;
		}
		return result;
	}
}

char *get_file_name(char *path) {
	printf("get_file_name\n");
	char *path_copy = malloc(strlen(path) + 1);
	strcpy(path_copy, path);
	char *base_name = basename(path_copy);
	char *to_return = malloc(strlen(base_name) + 1);
	strcpy(to_return, base_name);
	free(path_copy);
	return to_return;
}

unsigned int get_size_dir_entry(unsigned int path_length) {
	printf("get_size_dir_entry\n");
	path_length += 8;
	if (path_length % 4) {
		path_length += (4 - path_length % 4);
	}
	return path_length;
}


struct ext2_inode * find_inode_2(char * name, int size, struct ext2_inode *inode, struct ext2_inode *inode_table, unsigned char * disk){
  struct ext2_inode * inode_ptr = NULL;
  unsigned int bptr;
  for(int i = 0; i < 15; i++){
    bptr = inode->i_block[i];
    if(bptr){
      if(i < 12){
        inode_ptr = find_inode_block_2(name, size, inode_table, disk, bptr);
        if(inode_ptr){
          return inode_ptr;
        }
      }else{
        inode_ptr = find_inode_walk_2(i - 12, inode->i_block[i], name, size, inode_table, disk);
        if(inode_ptr){
          return inode_ptr;
        }
      }
    }
  }
  return inode_ptr;
}

struct ext2_inode * find_inode_block_2(char * name, int size, struct ext2_inode *inode_table, unsigned char * disk, unsigned int block){
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

struct ext2_inode * find_inode_walk_2(int depth, int block, char * name, int size, struct ext2_inode *inode_table, unsigned char * disk){
  struct ext2_inode * inode_ptr = NULL;
  unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
  if(depth == 0){
    for(int i = 0; i < 15; i++){
      if(inode[i]){
        inode_ptr = find_inode_block_2(name, size, inode_table, disk, inode[i]);
        if(inode_ptr){
          return inode_ptr;
        }
      }
    }
  } else {
    for(int i = 0; i < 15; i++){
      if(inode[i]){
        inode_ptr = find_inode_walk_2(depth - 1, inode[i], name, size, inode_table, disk);
        if(inode_ptr){
          return inode_ptr;
        }
         
      }
    }
  }
 return inode_ptr; 
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
	return bitmap[(unsigned int) sec] & mask;
}

void set_block_bitmap(int index) {
	char *bitmap = (char *) (disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
	char sec = index / 8;
	char mask = 1 << (index % 8);
	bitmap[(unsigned int) sec] |= mask;	
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
	return bitmap[(unsigned int) sec] & mask;
}

void set_inode_bitmap(int index) {
	char *bitmap = (char *) (disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);
	char sec = index / 8;
	char mask = 1 << (index % 8);
	printf("%u\n", (unsigned int) sec);
	bitmap[(unsigned int) sec] |= mask;
}
