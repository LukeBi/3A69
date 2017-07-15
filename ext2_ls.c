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

void print_byte(char b);
void print_bitmap(unsigned char* bits, int size);
void print_inode(struct ext2_inode *inode_table, int inum, unsigned char* disk);
void print_inodes(struct ext2_inode *inode_table, unsigned char* inode_bitmap, int size, unsigned char* disk);
void print_directory_block(struct ext2_inode *inode_table, int inum, unsigned char* disk);
void print_directory_blocks(struct ext2_inode *inode_table, unsigned char* inode_bitmap, int size, unsigned char* disk);
void print_group(struct ext2_super_block *sb, struct ext2_group_desc *gd);
void walk_inode(int depth, int block, unsigned char* disk);
void print_directory_entries(struct ext2_inode *inode, unsigned char* disk, char flag);
void print_directory_block_entries(unsigned char* disk, char flag, unsigned int block);
int get_next_token(char * token, char * path, int index);
void walk_directory_entries(int depth, int block, unsigned char* disk, char flag);

int main(int argc, char **argv) {
  char * filepath;
  char flag = 0;
  if(argc != 3 && argc != 4) {
    fprintf(stderr, "Usage: ext2_ls <image file name> [-a] <absolute file path>\n");
    exit(1);
  }
  if(argc == 4){
    if (strcmp(argv[2], "-a")){
      fprintf(stderr, "Usage: ext2_ls <image file name> [-a] <absolute file path>\n");
      exit(1);
    }
    filepath = argv[3];
    flag = 1;
  }else{
    filepath = argv[2];
  }
  int fd = open(argv[1], O_RDWR);

  disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(disk == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }

  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2*EXT2_BLOCK_SIZE);
  struct ext2_inode *inode_table = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);
  unsigned char * block_bitmap = disk + (gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
  unsigned char *  inode_bitmap = disk + (gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);
  
  int path_index = 0;
  char token[EXT2_NAME_LEN];
  path_index = get_next_token(token, filepath, path_index);
  if(path_index == -1){
    printf("No such file or directory\n");
    return ENOENT;
  }
  
  struct ext2_inode *inode = &(inode_table[EXT2_ROOT_INO-1]);
  print_directory_entries(inode, disk, flag);
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
  while(path[index] != '\0' || path[index] != '/'){
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

void print_directory_entries(struct ext2_inode *inode, unsigned char* disk, char flag){
  unsigned int bptr;
  for(int i = 0; i < 15; i++){
    bptr = inode->i_block[i];
    if(bptr){
      if(i < 12){
        print_directory_block_entries(disk, flag, bptr);
      }else{
        walk_directory_entries(i - 12, inode->i_block[i + 12], disk, flag);
      }
    }
  }
  /**
   * IMPLEMENT NODE WALK HERE!
   **/
}
void walk_directory_entries(int depth, int block, unsigned char* disk, char flag){
  unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
  if(depth == 0){
    for(int i = 0; i < 15; i++){
      if(inode[i]){
        print_directory_block_entries(disk, flag, inode[i]);
      }
    }
  } else {
    for(int i = 0; i < 15; i++){
      if(inode[i]){
        walk_directory_entries(depth - 1, inode[i], disk, flag); 
      }
    }
  }
  
}


void print_directory_block_entries(unsigned char* disk, char flag, unsigned int block){
  
  char * dirptr = (char *)(disk + block * EXT2_BLOCK_SIZE);
  char * next_block = dirptr + EXT2_BLOCK_SIZE;
  struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
  
  while(dirptr != next_block){
    // Print . and ..
    if(flag){
      printf("%.*s\n", dir->name_len, dir->name);
    }else{
      if(strcmp(dir->name, ".") && strcmp(dir->name, "..")){
        printf("%.*s\n", dir->name_len, dir->name);
      }
    }
    dirptr += dir->rec_len;
    dir = (struct ext2_dir_entry_2 *) dirptr;
  }
}








