#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
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

int main(int argc, char **argv) {

    if(argc != 2) {
        fprintf(stderr, "Usage: readimg <image file name>\n");
        exit(1);
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
    print_group(sb, gd);
    unsigned char * block_bitmap = disk + (gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
    unsigned char *  inode_bitmap = disk + (gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);
    printf("Block bitmap: ");
    print_bitmap(block_bitmap, sb->s_blocks_count / 8);
    printf("Inode bitmap: ");
    print_bitmap(inode_bitmap, sb->s_inodes_count / 8);
    printf("\n");
    print_inodes(inode_table, inode_bitmap , sb->s_inodes_count, disk);
    print_directory_blocks(inode_table, inode_bitmap, sb->s_inodes_count, disk);
    
    return 0;
}

void print_group(struct ext2_super_block *sb, struct ext2_group_desc *gd){
  printf("Inodes: %d\n", sb->s_inodes_count);
  printf("Blocks: %d\n", sb->s_blocks_count);
  printf("Block group:\n");
  printf("    block bitmap: %d\n", gd->bg_block_bitmap);
  printf("    inode bitmap: %d\n", gd->bg_inode_bitmap);
  printf("    inode table: %d\n", gd->bg_inode_table);
  printf("    free blocks: %d\n", sb->s_free_blocks_count);
  printf("    free inodes: %d\n", sb->s_free_inodes_count);
  printf("    used_dirs: %d\n", gd->bg_used_dirs_count);
}

void print_byte(char b){
  for(int i = 0; i < 8; i++){
    if(b & 1 << i){
      printf("1");
    }
    else{
      printf("0");
    }
  }
  return;
}
void print_directory_blocks(struct ext2_inode *inode_table, unsigned char* inode_bitmap, int size, unsigned char* disk){
  printf("Directory Blocks:\n");
  print_directory_block(inode_table, EXT2_ROOT_INO, disk);
  for(int i = EXT2_GOOD_OLD_FIRST_INO; i < size; i++){
    unsigned char shift = 7;
    unsigned int pos = ~7;
    if( (1 << (i & shift)) & (inode_bitmap[(i & pos) >> 3])){
      struct ext2_inode *inode = &(inode_table[i]);
        if (EXT2_S_IFDIR & inode->i_mode){
          // Add one since table index starts at 1
          print_directory_block(inode_table, i + 1, disk);
        }
    }
  }
}
void print_directory_block(struct ext2_inode *inode_table, int inum, unsigned char* disk){
  struct ext2_inode *inode = &(inode_table[inum-1]);
  unsigned int bptr = inode->i_block[0];
  printf("   DIR BLOCK NUM: %d (for inode %d)\n", bptr, inum);
  char * dirptr = (char *)(disk + bptr * EXT2_BLOCK_SIZE);
  char * next_block = dirptr + EXT2_BLOCK_SIZE;
  struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
  while(dirptr != next_block){
    int size = dir->name_len + 1;
    char name[size];
    for(int i = 0; i < size; i++){
      name[i] = dir->name[i];
    }
    name[size-1] = '\0';
    char filetype;
    switch(dir->file_type){
      case EXT2_FT_UNKNOWN:
        filetype = 'X';
        break;
      case EXT2_FT_REG_FILE:
        filetype = 'f';
        break;
      case EXT2_FT_DIR:
        filetype = 'd';
        break;
      case EXT2_FT_SYMLINK:
        filetype = 'l';
        break;
      default:
        filetype = 'X';
        break;
    }
    printf("Inode: %d rec_len: %d name_len: %d type= %c name=%s\n", dir->inode, dir->rec_len, dir->name_len, filetype, name);
    dirptr += dir->rec_len;
    dir = (struct ext2_dir_entry_2 *) dirptr;
  }
}

void print_bitmap(unsigned char * bits, int size){
  for(int i = 0; i < size - 1; i++){
    print_byte(bits[i]);
    printf(" ");
  }
  print_byte(bits[size-1]);
  printf("\n");
}

void print_inodes(struct ext2_inode *inode_table, unsigned char* inode_bitmap, int size, unsigned char* disk){
  printf("Inodes:\n");
  print_inode(inode_table, EXT2_ROOT_INO, disk);
  for(int i = EXT2_GOOD_OLD_FIRST_INO; i < size; i++){
    unsigned char shift = 7;
    unsigned int pos = ~7;
    if( (1 << (i & shift)) & (inode_bitmap[(i & pos) >> 3])){
      // Add one since table index starts at 1
      print_inode(inode_table, i + 1, disk);
    }
  }
  printf("\n");
}

void print_inode(struct ext2_inode *inode_table, int inum, unsigned char* disk){
  struct ext2_inode *inode = &(inode_table[inum-1]);
  printf("[%d] type: ", inum);
  if (EXT2_S_IFDIR & inode->i_mode){
    printf("d");
  }else if (EXT2_S_IFREG & inode->i_mode){
    printf("f");
  }else if(EXT2_S_IFLNK & inode->i_mode){
    printf("l");
  }
  printf(" size: %d links: %d blocks: %d\n", inode->i_size, inode->i_links_count, inode->i_blocks);
  
  
  printf("[%d] Blocks:  ", inum);
  for(int i = 0; i < 12; i++){
    if(inode->i_block[i]){
      printf("%d ", inode->i_block[i]);
    }
  }
  for(int i = 12; i < 16; i++){
    if(inode->i_block[i]){
      walk_inode(i - 12, inode->i_block[i], disk);
    }
  }
  
  printf("\n");
}



void walk_inode(int depth, int block, unsigned char* disk){
  unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
  if(depth == 0){
    for(int i = 0; i < 15; i++){
      if(inode[i]){
        printf("%d ", inode[i]);
      }
    }
  } else {
    for(int i = 0; i < 15; i++){
      if(inode[i]){
        walk_inode(depth - 1, inode[i], disk); 
      }
    }
  }
  
}



















