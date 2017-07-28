#include "helper.h"

int main(int argc, char **argv) {
    char * filepath;
    if(argc != 3) {
        show_error(EXT2MKDIR, 1);
    }
    filepath = argv[2];
    int fd = open(argv[1], O_RDWR);
    init(fd);
     
    // Fetch root, error if path does not include root
    char token[EXT2_NAME_LEN];
    struct ext2_inode * inode = fetch_last(filepath, token, FALSE);
    // Only case for this, is when the next node is root, which must always exist
    if(!inode){
        show_error(ALREADYEXIST, EEXIST);    
    }
    if(find_inode(token, strlen(token), inode)){
        show_error(ALREADYEXIST, EEXIST);    
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
     
    char * dirptr = (char *)(disk + (unsigned) (free_block) * EXT2_BLOCK_SIZE);
    struct ext2_dir_entry_2 * curr_dir = (struct ext2_dir_entry_2 *) dirptr;
    init_dirent(curr_dir, free_inode, 12, 1, EXT2_FT_DIR, ".");
    dirptr += curr_dir->rec_len;
    curr_dir = (struct ext2_dir_entry_2 *) dirptr;
    init_dirent(curr_dir, inode_number(inode), EXT2_BLOCK_SIZE - 12, 2, EXT2_FT_DIR, "..");
     
    // Walk directories for first empty dir entry in inode, and insert new curr_dir
    struct ext2_dir_entry_2 * insdir = malloc(dirlen);
    init_dirent(insdir, free_inode, dirlen, strlen(token), EXT2_FT_DIR, token);
    int newblocks = 0;
    int insertedto = insert_entry(inode, insdir, sb->s_blocks_count / 8, &newblocks);
    if(!insertedto){
        printf("Not enough space\n");
        return ENOENT;
    }
    // Update previous directory inode
    ++(inode->i_links_count);
     
    inode->i_blocks += 2 * newblocks;
    inode->i_size += EXT2_BLOCK_SIZE * newblocks;
     
    // Update superblock, One inode is used for new dir, add how many blocks have been used, 1 block for . and ..
    sb->s_free_blocks_count -= newblocks + 1;
    --(sb->s_free_inodes_count);
    gd->bg_free_blocks_count -= newblocks + 1;
    --(gd->bg_free_inodes_count);
    ++(gd->bg_used_dirs_count);
     
    free(insdir);
    return 0;
}

