#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"

unsigned char *disk;


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
    unsigned int *blocks = &(root->i_blocks);
    for (int i=0; i < 15; i++) {
        if (blocks[i]) {
            printf(" %d", blocks[i]);
        }
    }
    printf("\n");


    
    return 0;
}
