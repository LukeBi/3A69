#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <libgen.h>
#include "ext2.h"

unsigned char *disk;
char file_type(unsigned mode);
void print_out(int i);


int main(int argc, char **argv) {
    printf("%s\n", basename(argv[1]));
    // int fd = open(argv[1], O_RDWR);

    // disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    // if(disk == MAP_FAILED) {
    //  perror("mmap");
    //  exit(1);
    // }
    // int size = strtol(argv[2], NULL, 10);
    // int size_printed = 0;
    // int to_print;
    // char *content; 
    // for (int i=3; i < argc; i++) {
    //     if (size - size_printed <= EXT2_BLOCK_SIZE) {
    //         to_print = size - size_printed;
    //     } else {
    //         to_print = EXT2_BLOCK_SIZE;
    //     }
    //     content = (char *) (disk + strtol(argv[i], NULL, 10) * EXT2_BLOCK_SIZE);
    //     for (int j=0; j < to_print; j++) {
    //         printf("%c", content[j]);
    //     }
    //     size_printed += to_print;
    //     if (size_printed >= size) {
    //         break;
    //     }
    // }
    // unsigned int *blocks = (unsigned int *) (disk + EXT2_BLOCK_SIZE * 23);
    // for (int i=0; i < 256; i++) {
    //     if (blocks[i]) {
    //         printf("%d ", blocks[i]);
    //     }
    // }
    // printf("\n");



    return 0;
}