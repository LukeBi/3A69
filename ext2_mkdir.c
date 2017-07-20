#include "helper.h"

int main(int argc, char **argv) {
    char * filepath;
    if(argc != 3) {
        fprintf(stderr, "Usage: ext2_mkdir <image file name> <absolute file path>\n");
        exit(1);
    }
    
    filepath = argv[2];
    int fd = open(argv[1], O_RDWR);
    init(fd);
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
    init_dirent(curr_dir, free_inode, 12, 1, EXT2_FT_DIR, ".");
    dirptr += curr_dir->rec_len;
    curr_dir = (struct ext2_dir_entry_2 *) dirptr;
    init_dirent(curr_dir, , EXT2_BLOCK_SIZE - 12, 2, EXT2_FT_DIR, "..");
    curr_dir->inode = inode_number(inode);
     
    // Walk directories for first empty dir entry in inode, and insert new curr_dir
    struct ext2_dir_entry_2 * insdir = malloc(dirlen);
    init_dirent(insdir, free_inode, dirlen, strlen(token), EXT2_FT_DIR, token);
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

