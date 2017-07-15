#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"

unsigned char *disk;
char file_type(unsigned mode);
void print_out(int i);


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
    printf("Inodes: %d\n", sb->s_inodes_count);
    printf("Blocks: %d\n", sb->s_blocks_count);

    printf("Block group:\n");
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2048);
    printf("\tblock bitmap: %d\n", gd->bg_block_bitmap);
    printf("\tinode bitmap: %d\n", gd->bg_inode_bitmap);
    printf("\tinode table: %d\n", gd->bg_inode_table);
    printf("\tfree blocks: %d\n", gd->bg_free_blocks_count);
    printf("\tfree inodes: %d\n", gd->bg_free_inodes_count);
    printf("\tused_dirs: %d\n", gd->bg_used_dirs_count);

    unsigned int  *block_bitmap = (unsigned int *) (disk + gd->bg_block_bitmap * 1024);
    unsigned int mask = 1;
    int shift = 0;
    printf("Block bitmap: ");
    for (int i=0; i < sb->s_blocks_count; i++) {
        printf("%d", (mask & *block_bitmap) >> shift);
        mask = mask << 1;
        shift++;
        if (!((i+1) % 8)) {
            printf(" ");
        }
        if (!((i+1) % 32)) {
            block_bitmap += 1;
            mask = 1;
            shift = 0;
        }
    }
    printf("\n");
    unsigned int *inode_bitmap = (unsigned int *) (disk + gd->bg_inode_bitmap * 1024);
    mask = 1;
    shift = 0;
    printf("Inode bitmap: ");
    for (int i=0; i < sb->s_inodes_count; i++) {
        printf("%d", (mask & *inode_bitmap) >> shift);
        mask = mask << 1;
        shift++;
        if (!((i+1) % 8)) {
            printf(" ");
        }
        if (!((i+1) % 32)) {
            inode_bitmap += 1;
            mask = 1;
            shift = 0;
        }
    }
    printf("\n\nInodes:\n");

    struct ext2_inode *inode_table = (struct ext2_inode *) (disk + gd->bg_inode_table*1024);
    struct ext2_inode *root = &(inode_table[EXT2_ROOT_INO - 1]);

    char filetype;
    if (root->i_mode & EXT2_S_IFDIR) {
        filetype = 'd';
    } else if (root->i_mode & EXT2_S_IFREG) {
        filetype = 'f';
    } else {
        filetype = 'l';
    }

    printf("[2] type: %c size: %d links: %d blocks: %d\n", 
        filetype, root->i_size, root->i_links_count, root->i_blocks);
    printf("[2] Blocks: ");
    unsigned int *block = root->i_block;
    for (int i=0; i < 15; i++) {
        if (block[i]) {
            printf(" %d", block[i]);
        }
    }
    printf("\n");

    inode_bitmap = (unsigned int *) (disk + gd->bg_inode_bitmap * 1024);
    struct ext2_inode *current_inode;
    for (int i=EXT2_GOOD_OLD_FIRST_INO+1; i <= 32; i++) {
        mask = 1 << (i-1);
        if (mask & *inode_bitmap) {
            current_inode = &(inode_table[i-1]);
            printf("[%d] type: %c size: %d links: %d blocks: %d\n", i,  
                file_type(current_inode->i_mode), current_inode->i_size, 
                current_inode->i_links_count, current_inode->i_blocks);
            printf("[%d] Blocks: ", i);
            unsigned int *block = current_inode->i_block;
            for (int j=0; j < 15; j++) {
                if (block[j]) {
                    printf(" %d", block[j]);
                }
            }
            printf("\n");
        }
    }

    printf("\nDirectory Blocks:\n");
    int current_block_size_used;
    struct ext2_dir_entry_2 *current_dir_entry;
    block = root->i_block;
    for (int i=0; i < 15; i++) {
        if (block[i]) {
            printf("   DIR BLOCK NUM: %d (for inode %d) \n", block[i], 2);
            current_block_size_used = 0;
            current_dir_entry = (struct ext2_dir_entry_2 *) (disk + block[i] * 1024);
            while (current_block_size_used < 1024) {
                current_block_size_used += current_dir_entry->rec_len;
                printf("Inode: %d rec_len: %d name_len: %d name=%.*s\n", 
                    current_dir_entry->inode, current_dir_entry->rec_len, 
                    current_dir_entry->name_len, current_dir_entry->name_len,
                     current_dir_entry->name);
                current_dir_entry = (struct ext2_dir_entry_2 *) 
                    (disk + block[i] * 1024 + current_block_size_used);
            }
        }
    }

    for (int i=EXT2_GOOD_OLD_FIRST_INO+1; i <= 32; i++) {
        mask = 1 << (i-1);
        if (mask & *inode_bitmap) {
            current_inode = &(inode_table[i-1]);
            if (file_type(current_inode->i_mode) == 'd') {
                block = current_inode->i_block;
                for (int j=0; j < 15; j++) {
                    if (block[j]) {
                        printf("   DIR BLOCK NUM: %d (for inode %d) \n", block[j], i);
                        current_block_size_used = 0;
                        current_dir_entry = (struct ext2_dir_entry_2 *) (disk + block[j] * 1024);
                        while (current_block_size_used < 1024) {
                            current_block_size_used += current_dir_entry->rec_len;
                            printf("Inode: %d rec_len: %d name_len: %d name=%.*s\n", 
                                current_dir_entry->inode, current_dir_entry->rec_len, 
                                current_dir_entry->name_len, current_dir_entry->name_len,
                                 current_dir_entry->name);
                            current_dir_entry = (struct ext2_dir_entry_2 *) 
                                (disk + block[j] * 1024 + current_block_size_used);
                        }
                    }
                }
            }
        }
    }

    unsigned int *part = (unsigned int *) (disk + 36*1024);

    return 0;
}

void print_out(int i) {
    char *text = (char *) (disk + i*1024);
    printf("%.1024s\n\n\n", text);
    return;
}

char file_type(unsigned mode) {
    char filetype;
    if (mode & EXT2_S_IFDIR) {
        filetype = 'd';
    } else if (mode & EXT2_S_IFREG) {
        filetype = 'f';
    } else {
        filetype = 'l';
    }
    return filetype;
}


