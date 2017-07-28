
#include "helper.h"
void remove_item(struct ext2_inode *inode, struct ext2_inode *pionde, char flag, char* token);
void remove_dir(struct ext2_inode * pinode, struct ext2_inode * inode, char* token);
void remove_block_entries(struct ext2_inode * inode, unsigned int block);
void remove_block_walk(struct ext2_inode * inode, int depth, int block);
struct ext2_dir_entry_2 * find_dir_winode(int inodenum, struct ext2_inode *inode);
struct ext2_dir_entry_2 * find_dir_block_winode(int inodenum, unsigned int block);
struct ext2_dir_entry_2 * find_dir_walk_winode(int depth, int block, int inodenum);

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
    if(inode->i_mode & EXT2_S_IFDIR){
        if(inode_number(inode) == EXT2_ROOT_INO){
            pinode = NULL;
            strcpy(token, "/");
        }else{
            pinode = find_inode("..", 2, inode);
            // Fetch correct token
            // WALK THROUGH FETCH CORRECT INODE NUMBER
            struct ext2_dir_entry_2 * dir = find_dir_winode(inode_number(inode), pinode);
            strncpy(token, dir->name, dir->name_len);
            token[dir->name_len] = '\0';
        }
    }
    remove_item(inode, pinode, flag, token);
}

void remove_item(struct ext2_inode *inode, struct ext2_inode *pinode, char flag, char* token){
    if(inode->i_mode & EXT2_S_IFDIR){
        if(flag){
            // Bonus case
            remove_dir(pinode, inode, token);
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
                remove_block_walk(inode, i - 12, inode->i_block[i]);
            }
        }
    }
    delete_inode(inode);
    if(pinode){
        unsigned int block = remove_direntry(pinode, token);
        --(pinode->i_links_count);
        if(block){
            delete_block_from(pinode, block);
        }
    }
    
}

void remove_block_entries(struct ext2_inode * inode, unsigned int block){
    char * dirptr = (char *)(disk + block * EXT2_BLOCK_SIZE);
    struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
    
    unsigned char shift = 7;
    unsigned int pos = ~7;
    --block;
    while((1 << (block & shift)) & (block_bitmap[(block & pos) >> 3]))
    {
        if(dir->inode){
            char storetoken[dir->name_len + 1];
            strncpy(storetoken, dir->name, dir->name_len);
            storetoken[dir->name_len] = '\0';
            if(strncmp(dir->name, ".", dir->name_len) && strncmp(dir->name, "..", dir->name_len)){
                remove_item(&(inode_table[(dir->inode) - 1]), inode, TRUE, storetoken);
            }else{
                int blk = remove_direntry(inode, storetoken);
                if(blk){
                    delete_block_from(inode, blk);
                }
            }
        }else if (dir->rec_len == EXT2_BLOCK_SIZE){
            flip_bit(block_bitmap, block + 1);
            //++(sb->s_free_blocks_count);
            //++(gd->bg_free_blocks_count);
        }else{
            char storetoken[dir->name_len + 1];
            strncpy(storetoken, dir->name, dir->name_len);
            storetoken[dir->name_len] = '\0';
            remove_direntry(inode, storetoken);
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
        flip_bit(block_bitmap, block);
        //++(sb->s_free_blocks_count);
        //++(gd->bg_free_blocks_count);
    }
}
struct ext2_dir_entry_2 * find_dir_winode(int inodenum, struct ext2_inode *inode){
    struct ext2_dir_entry_2 * dir = NULL;
    unsigned int bptr;
    for(int i = 0; i < 15; i++){
        bptr = inode->i_block[i];
        if(bptr){
            if(i < 12){
                dir = find_dir_block_winode(inodenum, bptr);
                if(dir){
                    return dir;
                }
            }else{
                dir = find_dir_walk_winode(i - 12, inode->i_block[i], inodenum);
                if(dir){
                    return dir;
                }
            }
        }
    }
  return dir;
}

struct ext2_dir_entry_2 * find_dir_block_winode(int inodenum, unsigned int block){
    // Init dir entry vars in the block
    char * dirptr = (char *)(disk + block * EXT2_BLOCK_SIZE);
    char * next_block = dirptr + EXT2_BLOCK_SIZE;
    struct ext2_dir_entry_2 * dir = (struct ext2_dir_entry_2 *) dirptr;
  
    // Cycle through dir entries in the block
    while(dirptr != next_block){
        if(inodenum == dir->inode){
            return dir;
        }
        dirptr += dir->rec_len;
        dir = (struct ext2_dir_entry_2 *) dirptr;
    }
    return NULL;
}

struct ext2_dir_entry_2 * find_dir_walk_winode(int depth, int block, int inodenum){
    struct ext2_dir_entry_2 * dir = NULL;
    unsigned int *inode = (unsigned int *)(disk + (block) * EXT2_BLOCK_SIZE);
    if(depth == 0){
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                dir = find_dir_block_winode(inodenum, inode[i]);
                if(dir){
                    return dir;
                }
            }
        }
    } else {
        for(int i = 0; i < 15; i++){
            if(inode[i]){
                dir = find_dir_walk_winode(depth - 1, inode[i], inodenum);
                if(dir){
                    return dir;
                }
            }
        }
    }
    return dir; 
}


