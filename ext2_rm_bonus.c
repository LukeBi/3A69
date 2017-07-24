
#include "helper.h"
void remove_file(struct ext2_inode * pinode, struct ext2_inode * inode, char* token);
void delete_inode(struct ext2_inode * inode);
void delete_inode_blocks(struct ext2_inode *inode);
void delete_inode_block_indir(int depth, int block);
void flip_bit(unsigned char * bitmap, int index);
unsigned int remove_direntry(struct ext2_inode * pinode, char * token);
void delete_block_from(struct ext2_inode *inode, unsigned int block);
void delete_block_from_indir(int depth, int block, unsigned int delblock);
void remove_item(struct ext2_inode *inode, struct ext2_inode *pionde, char flag);

int main(int argc, char **argv) {
    char * filepath;
    char flag = 0;
    if(argc != 3 && argc != 4) {
        show_error(EXT2RM, 1);
    }
    if(argc == 4){
        if (strcmp(argv[2], "-r")){
            show_error(EXT2RM, 1);
        }
        filepath = argv[3];
        flag = 1;
    }else if(argc == 3){
        filepath = argv[2];
    }
    int fd = open(argv[1], O_RDWR);
    init(fd);
     
    // Fetch root, error if path does not include root
    char token[EXT2_NAME_LEN];
    struct ext2_inode * pinode = fetch_last(filepath, token, FALSE);
    struct ext2_inode * inode = fetch_last(filepath, token, TRUE);
    
    remove_item(inode, pinode, flag);
}

void remove_item(struct ext2_inode *inode, struct ext2_inode *pionde, char flag){
    if(inode->i_mode & EXT2_S_IFDIR){
        if(flag){
            // Bonus case
            struct ext2_inode * pdirinode = find_inode("..", 2, inode);
            remove_dir(pdirinode, inode);
        }else{
            show_error(ISADIRECTORY, EISDIR);
        }
    }else{
        remove_file(pinode, inode, token);
    }
}

void remove_dir(struct ext2_inode * pinode, struct ext2_inode * inode, char* token){
    
    unsigned int bptr;
    for(int i = 0; i < 15; i++){
        bptr = inode->i_block[i];
        if(bptr){
            if(i < 12){
                remove_block_entries(inode, bptr);
            }else{
                walk_directory_entries(i - 12, inode->i_block[i], flag);
            }
        }
    }
    
    delete_inode(inode);
    unsigned int block = remove_direntry(pinode, token);
    if(block){
        delete_block_from(inode, block);
    }
    
}

void remove_block_entries(struct ext2_inode * inode, unsigned int block){
    char * dirptr = (char *)(disk + block * EXT2_BLOCK_SIZE);
    char * next_block = dirptr + EXT2_BLOCK_SIZE;
    struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
    
    unsigned char shift = 7;
    unsigned int pos = ~7;
    while((1 << (block & shift)) & (block_bitmap[(block & pos) >> 3]))
    {
        if(dir->inode){
            if(strcmp(dir->name, ".") && strcmp(dir->name, "..")){
                remove_item(&(inode_table[(dir->inode) - 1]), inode, TRUE);
            }else{
                remove_direntry(inode, dir->name);
            }
        }else{
            remove_direntry(inode, dir->name);
        }
    }
}

void remove_block_walk(struct ext2_inode * inode, int depth, int block){
    unsigned int *inodeptr = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
    if(depth == 0){
        for(int i = 0; i < 15; i++){
            if(inodeptr[i]){
                remove_block_entries(inode, block);
            }
        }
    } else {
        for(int i = 0; i < 15; i++){
            if(inodeptr[i]){
                remove_block_walk(inode, depth - 1, inodeptr[i]);
            }
        }
        flip_bit(block_bitmap, block){
    }
}


void remove_file(struct ext2_inode * pinode, struct ext2_inode * inode, char* token){
    if(!(--(inode->i_links_count))){
        delete_inode(inode);
    }
    
    unsigned int block = remove_direntry(pinode, token);
    if(block){
        delete_block_from(inode, block);
    }
}

void delete_inode(struct ext2_inode * inode){
    inode->i_dtime = time(NULL);
    flip_bit(inode_bitmap, inode_number(inode));
    delete_inode_blocks(inode);
}

unsigned int remove_direntry(struct ext2_inode * pinode, char * token){
    struct ext2_dir_entry_2 * direntry = find_dir(token, strlen(token), pinode);
    unsigned int block = ((unsigned long)direntry - (unsigned long)disk)>> 10;
    
    char * dirptr = (char *)(disk + block * EXT2_BLOCK_SIZE);
    struct ext2_dir_entry_2 * dir;
  
    // Cycle through dir entries in the block until get to before direntry
    while(dirptr != (char *) direntry){
        dir = (struct ext2_dir_entry_2 *) dirptr;
        dirptr += dir->rec_len;
    }
    // Case 1: direntry is the first element
    if(!dir){
        // Case 1a: direntry is the only element, remove the entire block
        if(direntry->rec_len == EXT2_BLOCK_SIZE){
            flip_bit(block_bitmap, block);
            return block;
        }
        // Case 1b: direntry is not the only element, swap with the next element
        else{
            dir = direntry + direntry->rec_len;
            copy_dirent(direntry, dir, dir->rec_len + direntry->rec_len);
        }
    }
    // Case 2: direntry is a later element, extend the first element
    else{
        dir->rec_len += direntry->rec_len;
    }
    return 0;
}

void delete_block_from(struct ext2_inode *inode, unsigned int block){
    unsigned int bptr;
    for(int i = 0; i < 15; i++){
        bptr = inode->i_block[i];
        if(bptr){
            if((i < 12) && (block == bptr)){
                inode->i_block[i] = 0;
            }else if (i >= 12){
                delete_block_from_indir(i - 12, inode->i_block[i], block);
            }
        }
    }
}

void delete_block_from_indir(int depth, int block, unsigned int delblock){
    unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
    if(depth == 0){
        for(int i = 0; i < 15; i++){
            if(inode[i] == delblock){
                inode[i] = 0;
            }
        }
    } else {
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                delete_block_from_indir(depth - 1, inode[i], delblock); 
            }
        }
    }
}


void delete_inode_blocks(struct ext2_inode *inode){
    unsigned int bptr;
    for(int i = 0; i < 15; i++){
        bptr = inode->i_block[i];
        if(bptr){
            if(i < 12){
                flip_bit(block_bitmap, bptr);
            }else{
                delete_inode_block_indir(i - 12, inode->i_block[i]);
            }
        }
    }
}

void delete_inode_block_indir(int depth, int block){
    unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
    if(depth == 0){
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                flip_bit(block_bitmap, block);
            }
        }
    } else {
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                delete_inode_block_indir(depth - 1, inode[i]); 
            }
        }
    }
    flip_bit(block_bitmap, block);
}

void flip_bit(unsigned char * bitmap, int index){
    unsigned char shift = 7;
    unsigned int pos = ~7;
    bitmap[(index & pos) >> 3] &= ~(1 << (index & shift));
}