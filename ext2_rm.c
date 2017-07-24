
#include "helper.h"

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
        if(flag){
            // Bonus case
        }else{
            show_error(ISADIRECTORY, EISDIR);
        }
    }else{
        remove_file(pinode, inode, token);
    }
}

