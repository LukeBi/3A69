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

struct ext2_inode *fetch_last(char* filepath, char * token, char get_last){
    int path_index = 0;
    path_index = get_next_token(token, filepath, path_index);
    if(path_index == -1){
        show_error(DOESNOTEXIST, ENOENT);
    }
    struct ext2_inode *inode = &(inode_table[EXT2_ROOT_INO-1]);
    int size = strlen(filepath);
    if(path_index + 1 >= size){
        show_error(ALREADYEXIST, EEXIST);
    }
    path_index = get_next_token(token, filepath, path_index);
    if(path_index + 1 >= size){
        path_index += 1;
    }
    while(path_index < size){
        if(path_index == -1){
            show_error(DOESNOTEXIST, ENOENT);
        }
        inode = find_inode(token, strlen(token), inode);
        if(!inode){
            show_error(DOESNOTEXIST, ENOENT);
        }
        if(inode->i_mode & EXT2_S_IFDIR){
            path_index += 1;
        }
        // Test both symlink and directory in middle of path (works because symlink masks over dir)
        if(inode->i_mode & EXT2_S_IFLNK && path_index != size){
            show_error(DOESNOTEXIST, ENOENT);
        }
        if(path_index < size){
            path_index = get_next_token(token, filepath, path_index);
        }
        if(path_index + 1 >= size){
            path_index += 1;
        }
    }
    if(get_last){
      inode = find_inode(token, strlen(token), inode);
        if(!inode){
            show_error(DOESNOTEXIST, ENOENT);
        }
    }else{
      if(find_inode(token, strlen(token), inode)){
        show_error(ALREADYEXIST, EEXIST);
      }
    }
    return inode;
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
        case EXT2LS:
            string = LSUSAGE;
            break;
        case EXT2MKDIR:
            string = MKDIRUSAGE;
            break;
        case EXT2RM:
            string = RMUSAGE;
            break;
        default:
            string = ERR;
    }
    fprintf(stderr, "%s\n", string);
    exit(exitcode);
}

struct ext2_inode * find_inode(char * name, int size, struct ext2_inode *inode){
    struct ext2_inode * inode_ptr = NULL;
    unsigned int bptr;
    for(int i = 0; i < 15; i++){
        bptr = inode->i_block[i];
        if(bptr){
            if(i < 12){
                inode_ptr = find_inode_block(name, size, bptr);
                if(inode_ptr){
                    return inode_ptr;
                }
            }else{
                inode_ptr = find_inode_walk(i - 12, inode->i_block[i], name, size);
                if(inode_ptr){
                    return inode_ptr;
                }
            }
        }
    }
  return inode_ptr;
}

struct ext2_inode * find_inode_block(char * name, int size, unsigned int block){
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

struct ext2_inode * find_inode_walk(int depth, int block, char * name, int size){
    struct ext2_inode * inode_ptr = NULL;
    unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
    if(depth == 0){
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                inode_ptr = find_inode_block(name, size, inode[i]);
                if(inode_ptr){
                    return inode_ptr;
                }
            }
        }
    } else {
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                inode_ptr = find_inode_walk(depth - 1, inode[i], name, size);
                if(inode_ptr){
                    return inode_ptr;
                }
            }
        }
    }
    return inode_ptr; 
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