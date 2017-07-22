#include "helper.h"


void init(int fd){
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    sb = (struct ext2_super_block *)(disk + 1024);
    gd = (struct ext2_group_desc *)(disk + 2*EXT2_BLOCK_SIZE);
    inode_table = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);
    block_bitmap = disk + (gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
    inode_bitmap = disk + (gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);
}


/* 
 * Create a directory entry inside given directory inode, with given file inode number
 * and file name.
 */
void create_directory_entry(struct ext2_inode *dir_inode, unsigned int file_inode_number, char *file_name, char is_link) {
    struct ext2_dir_entry_2 *result = NULL;
    unsigned size_needed = get_size_dir_entry(strlen(file_name));
    int depth;
    unsigned int *block_num_addr;
    for (int i=0; i < 15; i++) {
        block_num_addr = &(dir_inode->i_block[i]);
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
    struct ext2_dir_entry_2 *result = NULL;
    unsigned ints_per_block = EXT2_BLOCK_SIZE / sizeof(unsigned int);
    if (depth) {
        if (!*block_num) {
            // if not allocated, allocate space first
            *block_num = allocate_data_block();
            memset((disk + *block_num * EXT2_BLOCK_SIZE), 0, EXT2_BLOCK_SIZE);
        }
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
        if (!*block_num) {
            *block_num = allocate_data_block();
            memset((disk + *block_num * EXT2_BLOCK_SIZE), 0, EXT2_BLOCK_SIZE);
        }
        struct ext2_dir_entry_2 *dir_entry = (struct ext2_dir_entry_2 *) (disk + *block_num * EXT2_BLOCK_SIZE);
        if (dir_entry->rec_len) {
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
            result = dir_entry;
            result->rec_len = EXT2_BLOCK_SIZE;
        }
        return result;
    }
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

unsigned int get_size_dir_entry(unsigned int path_length) {
    path_length += 8;
    if (path_length % 4) {
        path_length += (4 - path_length % 4);
    }
    return path_length;
}

int allocate_inode() {
    int i = EXT2_GOOD_OLD_FIRST_INO;
    // check available inodes
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
    // set bit map, update count and initialize the inode
    set_inode_bitmap(i);
    memset(&(inode_table[i]), 0, sizeof(struct ext2_inode));
    gd->bg_free_inodes_count--;
    // inode starts at 1
    return i+1;
}

int inode_is_taken(int index) {
    // char *bitmap = (char *) (disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);
    char sec = index / 8;
    char mask = 1 << (index % 8);
    return inode_bitmap[(unsigned int)sec] & mask;
}

void set_inode_bitmap(int index) {
    // char *bitmap = (char *) (disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);
    char sec = index / 8;
    char mask = 1 << (index % 8);
    inode_bitmap[(unsigned int) sec] |= mask;
}

int block_taken(int index) {
    char sec = index / 8;
    char mask = 1 << (index % 8);
    return block_bitmap[(unsigned int) sec] & mask;
}

int allocate_data_block() {
    // see if there are free blocks
    if (!gd->bg_free_blocks_count) {
        return -1;
    }
    // find free block
    int i=0;
    while (i < sb->s_blocks_count && block_taken(i)) {
        i++;
    }
    if (i == sb->s_blocks_count) {
        // should not be here
        return -1;
    }

    // set bit map and update count
    set_block_bitmap(i);
    gd->bg_free_blocks_count--;
    // block number starts at 1
    return i+1;
}

void set_block_bitmap(int index) {
    // char *bitmap = (char *) (disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
    char sec = index / 8;
    char mask = 1 << (index % 8);
    block_bitmap[(unsigned int) sec] |= mask;
}

struct ext2_inode *fetch_last(char* filepath, char * token, char get_last){
    // Fetch root, error if path does not include root
    int path_index = 0;
    path_index = get_next_token(token, filepath, path_index);
    if(path_index == -1){
        show_error(DOESNOTEXIST, ENOENT);
    }
    struct ext2_inode *inode = &(inode_table[EXT2_ROOT_INO-1]);
    struct ext2_inode *ret_inode = NULL;
    int size = strlen(filepath);
    while(path_index < size){
        path_index = get_next_token(token, filepath, path_index);
        if(path_index == -1){
            show_error(DOESNOTEXIST, ENOENT);
        }
        ret_inode = inode;
        inode = find_inode(token, strlen(token), inode);
        if(!inode){
            if(path_index + 1 >= size && (!get_last)){
                return ret_inode;
            }
            show_error(DOESNOTEXIST, ENOENT);
        }
        if(inode->i_mode & EXT2_S_IFDIR){
            path_index += 1;
        }
        // Test both symlink and directory in middle of path (works because symlink masks over dir)
        if(inode->i_mode & EXT2_S_IFLNK && path_index != size){
            show_error(DOESNOTEXIST, ENOENT);
        }
    }
    if(get_last){
        ret_inode = inode;
    }
    return ret_inode;
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

void show_error(int error, int exitcode){
    const char * string;
    switch(error) {
        case DOESNOTEXIST:
            string = NOFILE;
            break;
        case ALREADYEXIST:
            string = FILEEX;
            break;
        case ISADIRECTORY:
            string = ISADIR; 
            break;
        case EXT2LS:
            string = LSUSAGE;
            break;
        case EXT2MKDIR:
            string = MKDIRUSAGE;
            break;
        case EXT2RM:
            string = RMUSAGE;
            break;
        case EXT2LN:
            string = LNUSAGE;
            break;
        case EXT2CP:
            string = CPUSAGE;
            break;
        default:
            string = ERR;
    }
    fprintf(stderr, "%s\n", string);
    exit(exitcode);
}

struct ext2_inode * find_inode(char * name, int size, struct ext2_inode *inode){
    struct ext2_dir_entry_2 * dir = find_dir(name, size, inode);
    if(dir){
        return &(inode_table[(dir->inode) - 1]);
    }else{
        return NULL;
    }
}

struct ext2_inode * find_inode_block(char * name, int size, unsigned int block){
    struct ext2_dir_entry_2 * dir = find_dir_block(name, size, block);
    if(dir){
        return &(inode_table[(dir->inode) - 1]);
    }else{
        return NULL;
    }
}

struct ext2_inode * find_inode_walk(int depth, int block, char * name, int size){
    struct ext2_dir_entry_2 * dir = find_dir_walk(depth, block, name, size);
    if(dir){
        return &(inode_table[(dir->inode) - 1]);
    }else{
        return NULL;
    }
}

struct ext2_dir_entry_2 * find_dir(char * name, int size, struct ext2_inode *inode){
    struct ext2_dir_entry_2 * dir = NULL;
    unsigned int bptr;
    for(int i = 0; i < 15; i++){
        bptr = inode->i_block[i];
        if(bptr){
            if(i < 12){
                dir = find_dir_block(name, size, bptr);
                if(dir){
                    return dir;
                }
            }else{
                dir = find_dir_walk(i - 12, inode->i_block[i], name, size);
                if(dir){
                    return dir;
                }
            }
        }
    }
  return dir;
}

struct ext2_dir_entry_2 * find_dir_block(char * name, int size, unsigned int block){
    // Init dir entry vars in the block
    char * dirptr = (char *)(disk + block * EXT2_BLOCK_SIZE);
    char * next_block = dirptr + EXT2_BLOCK_SIZE;
    struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
  
    // Cycle through dir entries in the block
    while(dirptr != next_block){
        if(path_equal(name, size, dir)){
            return dir;
        }
        dirptr += dir->rec_len;
        dir = (struct ext2_dir_entry_2 *) dirptr;
    }
    return NULL;
}

struct ext2_dir_entry_2 * find_dir_walk(int depth, int block, char * name, int size){
    struct ext2_dir_entry_2 * dir = NULL;
    unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
    if(depth == 0){
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                dir = find_dir_block(name, size, inode[i]);
                if(dir){
                    return dir;
                }
            }
        }
    } else {
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                dir = find_dir_walk(depth - 1, inode[i], name, size);
                if(dir){
                    return dir;
                }
            }
        }
    }
    return dir; 
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

void print_directory_entries(struct ext2_inode *inode, char flag){
    unsigned int bptr;
    for(int i = 0; i < 15; i++){
        bptr = inode->i_block[i];
        if(bptr){
            if(i < 12){
                print_directory_block_entries(flag, bptr);
            }else{
                walk_directory_entries(i - 12, inode->i_block[i], flag);
            }
        }
    }
}

void walk_directory_entries(int depth, int block, char flag){
    unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
    if(depth == 0){
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                print_directory_block_entries(flag, inode[i]);
            }
        }
    } else {
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                walk_directory_entries(depth - 1, inode[i], flag); 
            }
        }
    }
}


void print_directory_block_entries(char flag, unsigned int block){
    char * dirptr = (char *)(disk + block * EXT2_BLOCK_SIZE);
    char * next_block = dirptr + EXT2_BLOCK_SIZE;
    struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
    while(dirptr != next_block){
        // Print . and ..
        if(dir->inode){
            if(flag){
                printf("%.*s\n", dir->name_len, dir->name);
            }else{
                if(strcmp(dir->name, ".") && strcmp(dir->name, "..")){
                    printf("%.*s\n", dir->name_len, dir->name);
                }
            }
        }
        dirptr += dir->rec_len;
        dir = (struct ext2_dir_entry_2 *) dirptr;
    }
}

int find_free_bitmap(unsigned char * bitmap, int size){
    // Size is in bytes, convert to bits
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

/**
 * Return the block in which the entry was inserted.
 **/
int insert_entry(struct ext2_inode *inode, struct ext2_dir_entry_2 * insdir, int bitmapsize, int* newblocks){
    unsigned int bptr;
    int newblock = 0;
    for(int i = 0; i < 15; i++){
        bptr = inode->i_block[i];
        if(i < 12){
            if(bptr){
                newblock = insert_entry_block(bptr, insdir);
                if(newblock){
                    return newblock;
                }
            }else{
                // Init block
                inode->i_block[i] = find_free_bitmap(block_bitmap, bitmapsize);
                *newblocks += 1;
                char * dirptr = (char *)(disk + inode->i_block[i] * EXT2_BLOCK_SIZE);
                struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
                copy_dirent(dir, insdir, EXT2_BLOCK_SIZE);
                return inode->i_block[i];
            }
        }else{
            if(!bptr){
                // Init inode depth
                inode->i_block[i] = find_free_bitmap(block_bitmap, bitmapsize);
                *newblocks += 1;
                unsigned int *nextinode = (unsigned int *)(disk + (inode->i_block[i] - 1) * EXT2_BLOCK_SIZE);
                for(int j = 0; j < 15; j++){
                    nextinode[j] = 0;
                }
            }
            newblock = insert_entry_walk(i - 12, inode->i_block[i], insdir, bitmapsize, newblocks);
            if(newblock){
                return newblock;
            }
        }
    }
    return newblock;
}

int insert_entry_block(unsigned int block, struct ext2_dir_entry_2 * insdir){
  
    char * dirptr = (char *)(disk + block * EXT2_BLOCK_SIZE);
    char * next_block = dirptr + EXT2_BLOCK_SIZE;
    struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
     
    if(dir->inode == 0){
        copy_dirent(dir, insdir, EXT2_BLOCK_SIZE);
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
        copy_dirent(dir, insdir, EXT2_BLOCK_SIZE - total);
        return block;
    }
    return 0;
}

int insert_entry_walk(int depth, int block, struct ext2_dir_entry_2 * insdir, int bitmapsize, int* newblocks){
    unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
    int newblock = 0;
    if(depth == 0){
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                newblock = insert_entry_block(inode[i], insdir);
                if(newblock){
                    return newblock;
                }
            }else{
                newblock = find_free_bitmap(block_bitmap, bitmapsize);
                *newblocks += 1;
                char * dirptr = (char *)(disk + newblock * EXT2_BLOCK_SIZE);
                struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
                copy_dirent(dir, insdir, EXT2_BLOCK_SIZE);
                return newblock;
            }
        }
    } else {
        for(int i = 0; i < 15; i++){
            if(!inode[i]){
                // If multilevel node not initialized, initialize it
                inode[i] = find_free_bitmap(block_bitmap, bitmapsize);
                *newblocks += 1;
                unsigned int *nextinode = (unsigned int *)(disk + (inode[i] - 1) * EXT2_BLOCK_SIZE);
                for(int j = 0; j < 15; j++){
                    nextinode[j] = 0;
                }
            }
            // Test for insertion
            newblock = insert_entry_walk(depth - 1, inode[i], insdir, bitmapsize, newblocks); 
            if(newblock){
                return newblock;
            }
        }
    }
  return newblock;
}

void init_dirent(struct ext2_dir_entry_2 * dir, unsigned int inode, unsigned short rec_len, unsigned char name_len, unsigned char file_type, char * name){
    dir->inode = inode;
    dir->rec_len = rec_len;
    dir->name_len = name_len;
    dir->file_type = file_type;
    for(int i = 0; i < name_len; i++){
        dir->name[i] = name[i];
    }
}

void copy_dirent(struct ext2_dir_entry_2 *dir, struct ext2_dir_entry_2 *source, unsigned short rec_len){
    init_dirent(dir, source->inode, rec_len, source->name_len, source->file_type, source->name);
}

int inode_number(struct ext2_inode * inode){
    return (int) (1 + ((char *)inode - (char *)inode_table)/sizeof(struct ext2_inode));
}
