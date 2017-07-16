#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
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
int find_free_bitmap(unsigned char * bitmap, int size);
int insert_entry(struct ext2_inode *inode, unsigned char* disk, struct ext2_dir_entry_2 * insdir, unsigned char * bitmap, int bitmapsize, int* newblocks);
int insert_entry_block(unsigned char* disk, unsigned int block, struct ext2_dir_entry_2 * insdir);
int insert_entry_walk(int depth, int block, unsigned char* disk, struct ext2_dir_entry_2 * insdir, unsigned char * bitmap, int bitmapsize, int* newblocks);

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

  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2*EXT2_BLOCK_SIZE);
  struct ext2_inode *inode_table = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);
  unsigned char * block_bitmap = disk + (gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
  unsigned char * inode_bitmap = disk + (gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);
  
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
  
  int dirlen = sizeof(struct ext2_dir_entry_2);
  dirlen += strlen(token);
  short padding = 0;
  while(3 & dirlen){
    ++padding;
    ++dirlen;
  };
  // Create an inode for new dir
  int free_inode = find_free_bitmap(inode_bitmap, sb->s_inodes_count / 8) + 1;
  struct ext2_inode * next_inode = &(inode_table[free_inode - 1]);
  // Set type (i_mode), i_size, i_links_count, i_blocks, and i_block array
  next_inode->i_mode = EXT2_S_IFDIR;
  next_inode->i_size = EXT2_BLOCK_SIZE;
  next_inode->i_links_count = 2;
  next_inode->i_blocks = EXT2_BLOCK_SIZE/512;
  for(int i = 1; i < 15; i++){
    next_inode->i_block[i] = 0;
  }
  // Reserve a block for inode, insert . and ..
  int free_block = find_free_bitmap(block_bitmap, sb->s_blocks_count / 8) + 1;
  next_inode->i_block[0] = (unsigned int) free_block;
  next_inode->i_block[1] = 0;
  
  char * dirptr = (char *)(disk + (unsigned) (free_block) * EXT2_BLOCK_SIZE);
  struct ext2_dir_entry_2 * curr_dir = (struct ext2_dir_entry_2 *) dirptr;
  curr_dir->inode = free_inode;
  curr_dir->rec_len = 12;
  curr_dir->name_len = 1;
  curr_dir->file_type = EXT2_FT_DIR;
  curr_dir->name[0] = '.';
  dirptr += curr_dir->rec_len;
  curr_dir = (struct ext2_dir_entry_2 *) dirptr;
  curr_dir->inode = (int) (1 + ((char *)inode - (char *)inode_table)/sizeof(struct ext2_inode));
  curr_dir->rec_len = EXT2_BLOCK_SIZE - 12;
  curr_dir->name_len = 2;
  curr_dir->file_type = EXT2_FT_DIR;
  curr_dir->name[0] = '.';
  curr_dir->name[1] = '.';
  
  // Walk directories for first empty dir entry in inode, and insert new curr_dir
  struct ext2_dir_entry_2 * insdir = malloc(dirlen);
  insdir->inode = free_inode;
  insdir->rec_len = dirlen;
  insdir->name_len = strlen(token);
  insdir->file_type = EXT2_FT_DIR;
  for(int i = 0; i < insdir->name_len; i++){
    insdir->name[i] = token[i];
  }
  int newblocks = 0;
  int insertedto = insert_entry(inode, disk, insdir, block_bitmap, sb->s_blocks_count / 8, &newblocks);
  if(!insertedto){
    printf("Not enough space\n");
    return ENOENT;
  }
  // Update previous directory inode
  ++(inode->i_links_count);
  
  inode->i_blocks += 2 * newblocks;
  inode->i_size += EXT2_BLOCK_SIZE * newblocks;
  
  // Update superblock, One inode is used for new dir, add how many blocks have been used
  sb->s_free_blocks_count -= newblocks;
  --(sb->s_free_inodes_count);
  gd->bg_free_blocks_count -= newblocks;
  --(gd->bg_free_inodes_count);
  ++(gd->bg_used_dirs_count);
  
  free(insdir);
  return 0;
}


int find_free_bitmap(unsigned char * bitmap, int size){
  size*=8;
  for(int i = EXT2_GOOD_OLD_FIRST_INO; i < size; i++){
    unsigned char shift = 7;
    unsigned int pos = ~7;
    if(!( (1 << (i & shift)) & (bitmap[(i & pos) >> 3]))){
      // Flag the bitmap, as it is taken now
      (bitmap[(i & pos) >> 3]) |= (1 << (i & shift)); 
      return i;
    }
  }
  return -1;
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






/**
 * Return the block in which the entry was inserted.
 **/
int insert_entry(struct ext2_inode *inode, unsigned char* disk, struct ext2_dir_entry_2 * insdir, unsigned char * bitmap, int bitmapsize, int* newblocks){
  unsigned int bptr;
  int newblock = 0;
  for(int i = 0; i < 15; i++){
    bptr = inode->i_block[i];
    if(i < 12){
      if(bptr){
        newblock = insert_entry_block(disk, bptr, insdir);
        if(newblock){
          return newblock;
        }
      }else{
        // Init block
        inode->i_block[i] = find_free_bitmap(bitmap, bitmapsize);
        *newblocks += 1;
        char * dirptr = (char *)(disk + inode->i_block[i] * EXT2_BLOCK_SIZE);
        struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
        dir->inode = insdir->inode;
        dir->rec_len = EXT2_BLOCK_SIZE;
        dir->name_len = insdir->name_len;
        dir->file_type = insdir->file_type;
        for(int i = 0; i < dir->name_len; i++){
          dir->name[i] = insdir->name[i];
        }
        return inode->i_block[i];
      }
    }else{
      if(!bptr){
        // Init inode depth
        inode->i_block[i] = find_free_bitmap(bitmap, bitmapsize);
        *newblocks += 1;
        unsigned int *nextinode = (unsigned int *)(disk + (inode->i_block[i] - 1) * EXT2_BLOCK_SIZE);
        for(int j = 0; j < 15; j++){
          nextinode[j] = 0;
        }
      }
      newblock = insert_entry_walk(i - 12, inode->i_block[i + 12], disk, insdir, bitmap, bitmapsize, newblocks);
      if(newblock){
        return newblock;
      }
    }
  }
  return newblock;
}

int insert_entry_block(unsigned char* disk, unsigned int block, struct ext2_dir_entry_2 * insdir){
  
  char * dirptr = (char *)(disk + block * EXT2_BLOCK_SIZE);
  char * next_block = dirptr + EXT2_BLOCK_SIZE;
  struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
  
  if(dir->inode == 0){
    dir->inode = insdir->inode;
    dir->rec_len = EXT2_BLOCK_SIZE;
    dir->name_len = insdir->name_len;
    dir->file_type = insdir->file_type;
    for(int i = 0; i < dir->name_len; i++){
      dir->name[i] = insdir->name[i];
    }
    return block;
  }
  int total = 0;
  while(dirptr != next_block){
    dir = (struct ext2_dir_entry_2 *) dirptr;
    total += dir->rec_len;
    dirptr += dir->rec_len;
  }
  total -= dir->rec_len;
  
  int size = dir->name_len + 8;
  // Pad for multiple of 4.
  while(3 & size){
    ++size;
  }
  total += size;
  int diff = dir->rec_len - size;
  if(insdir->rec_len <= diff){
    dirptr -= dir->rec_len;
    dirptr += size;
    dir->rec_len = size;
    dir = (struct ext2_dir_entry_2 *) dirptr;
    dir->inode = insdir->inode;
    dir->rec_len = EXT2_BLOCK_SIZE - total;
    dir->name_len = insdir->name_len;
    dir->file_type = insdir->file_type;
    for(int i = 0; i < dir->name_len; i++){
      dir->name[i] = insdir->name[i];
    }
    return block;
  }else{
    return 0;
  }
  return 0;
}





int insert_entry_walk(int depth, int block, unsigned char* disk, struct ext2_dir_entry_2 * insdir, unsigned char * bitmap, int bitmapsize, int* newblocks){
  unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
  int newblock = 0;
  if(depth == 0){
    for(int i = 0; i < 15; i++){
      if(inode[i]){
        newblock = insert_entry_block(disk, inode[i], insdir);
        if(newblock){
          return newblock;
        }
      }else{
        newblock = find_free_bitmap(bitmap, bitmapsize);
        *newblocks += 1;
        char * dirptr = (char *)(disk + newblock * EXT2_BLOCK_SIZE);
        struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
        dir->inode = insdir->inode;
        dir->rec_len = EXT2_BLOCK_SIZE;
        dir->name_len = insdir->name_len;
        dir->file_type = insdir->file_type;
        for(int i = 0; i < dir->name_len; i++){
          dir->name[i] = insdir->name[i];
        }
        return newblock;
      }
    }
  } else {
    for(int i = 0; i < 15; i++){
      if(!inode[i]){
        // If multilevel node not initialized, initialize it
        inode[i] = find_free_bitmap(bitmap, bitmapsize);
        *newblocks += 1;
        unsigned int *nextinode = (unsigned int *)(disk + (inode[i] - 1) * EXT2_BLOCK_SIZE);
        for(int j = 0; j < 15; j++){
          nextinode[j] = 0;
        }
      }
      
      // Test for insertion
      newblock = insert_entry_walk(depth - 1, inode[i], disk, insdir, bitmap, bitmapsize, newblocks); 
      if(newblock){
        return newblock;
      }
    }
  }
  return newblock;
}


