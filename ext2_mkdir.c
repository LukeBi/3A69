#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"


unsigned char *disk;

void walk_inode(int depth, int block, unsigned char* disk);
int get_next_token(char * token, char * path, int index);
struct ext2_inode * find_inode(char * name, int size, struct ext2_inode *inode, struct ext2_inode *inode_table, unsigned char * disk);
struct ext2_inode * find_inode_block(char * name, int size, struct ext2_inode *inode_table, unsigned char * disk, unsigned int block);
int path_equal(char * path, int size, struct ext2_dir_entry_2 * dir);
struct ext2_inode * find_inode_walk(int depth, int block, char * name, int size, struct ext2_inode *inode_table, unsigned char * disk);

int main(int argc, char **argv) {
  char * filepath;
  if(argc != 3) {
    fprintf(stderr, "Usage: ext2_mkdir <image file name> <absolute file path>\n");
    exit(1);
  }

  filepath = argv[2];

  int fd = open(argv[1], O_RDWR);

  disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(disk == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }

  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2*EXT2_BLOCK_SIZE);
  struct ext2_inode *inode_table = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);
  
  // Fetch root, error if path does not include root
  int path_index = 0;
  char token[EXT2_NAME_LEN];
  path_index = get_next_token(token, filepath, path_index);
  if(path_index == -1){
    printf("No such file or directory\n");
    return ENOENT;
  }
  
  struct ext2_inode *inode = &(inode_table[EXT2_ROOT_INO-1]);
  int size = strlen(filepath);
  if(path_index + 1 >= size){
    printf("File exists\n");
    return EEXIST;
  }
  path_index = get_next_token(token, filepath, path_index);
  if(path_index + 1 >= size){
    path_index += 1;
  }
  while(path_index < size){
    if(path_index == -1){
      printf("No such file or directory\n");
      return ENOENT;
    }
    inode = find_inode(token, strlen(token), inode, inode_table, disk);
    if(!inode){
      printf("No such file or directory\n");
      return ENOENT;
    }
    if(inode->i_mode & EXT2_S_IFDIR){
      path_index += 1;
    }
    // Test both symlink and directory in middle of path (works because symlink masks over dir)
    if(inode->i_mode & EXT2_S_IFLNK && path_index != size){
      printf("No such file or directory\n");
      return ENOENT;
    }
    if(path_index < size){
      path_index = get_next_token(token, filepath, path_index);
    }
    if(path_index + 1 >= size){
      path_index += 1;
    }
  }
  if(find_inode(token, strlen(token), inode, inode_table, disk)){
    printf("File exists\n");
    return EEXIST;
  }
  
  int dirlen = 8;
  dirlen += strlen(token);
  short padding = 0;
  while(3 & dirlen){
    ++padding;
    ++dirlen;
  }
  printf("%d\n", dirlen);
  // Create an inode for new dir
  
  // Walk directories for first empty dir entry and insert
  
  // Update bitmaps
  
  // Update superblock
  return 0;
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
        inode_ptr = find_inode_walk(i - 12, inode->i_block[i + 12], name, size, inode_table, disk);
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








