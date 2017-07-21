#include "helper.h"

int main(int argc, char **argv) {
    char * filepath;
    char flag = 0;
    if(argc != 3 && argc != 4) {
        show_error(EXT2LS, 1);
    }
    if(argc == 4){
        if (strcmp(argv[2], "-a")){
            show_error(EXT2LS, 1);
        }
        filepath = argv[3];
        flag = 1;
    }else{
        filepath = argv[2];
    }
    int fd = open(argv[1], O_RDWR);
    init(fd);
  
    // Pass true for flag, always want to fetch the final item
    char token[EXT2_NAME_LEN];
    struct ext2_inode * inode = fetch_last(filepath, token, TRUE);
    if(inode->i_mode & EXT2_S_IFLNK){
        printf("%s\n", token);
    }else{
        print_directory_entries(inode, flag);
    }
    return 0;
}



