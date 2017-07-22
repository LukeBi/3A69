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
#include "ext2.h"

#define TRUE 1
#define FALSE 0

#define DOESNOTEXIST 0
#define ALREADYEXIST 1
#define ISADIRECTORY 2

#define EXT2LS 10 
#define EXT2MKDIR 11
#define EXT2RM 12

unsigned char * disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;
struct ext2_inode *inode_table;
unsigned char * block_bitmap;
unsigned char * inode_bitmap;
static const char NOFILE[] = "No such file or directory";
static const char FILEEX[] = "File exists";
static const char ISADIR[] = "Is a directory";
static const char LSUSAGE[] = "Usage: ext2_ls <image file name> [-a] <absolute file path>";
static const char MKDIRUSAGE[] = "Usage: ext2_mkdir <image file name> <absolute file path>";
static const char RMUSAGE[] = "Usage: ext2_rm <image file name> [-r] <absolute file path>";
static const char ERR[] = "ERROR";
void init(int fd);
struct ext2_inode *fetch_last(char* filepath, char * token, char get_last);
int get_next_token(char * token, char * path, int index);
void show_error(int error, int exitcode);
struct ext2_inode * find_inode(char * name, int size, struct ext2_inode *inode);
struct ext2_inode * find_inode_block(char * name, int size, unsigned int block);
struct ext2_inode * find_inode_walk(int depth, int block, char * name, int size);
struct ext2_dir_entry_2 * find_dir_block(char * name, int size, unsigned int block);
struct ext2_dir_entry_2 * find_dir_walk(int depth, int block, char * name, int size);
struct ext2_dir_entry_2 * find_dir(char * name, int size, struct ext2_inode *inode);
int path_equal(char * path, int size, struct ext2_dir_entry_2 * dir);
void print_directory_entries(struct ext2_inode *inode, char flag);
void print_directory_block_entries(char flag, unsigned int block);
void walk_directory_entries(int depth, int block, char flag);
int find_free_bitmap(unsigned char * bitmap, int size);
int insert_entry(struct ext2_inode *inode, struct ext2_dir_entry_2 * insdir, int bitmapsize, int* newblocks);
int insert_entry_block(unsigned int block, struct ext2_dir_entry_2 * insdir);
int insert_entry_walk(int depth, int block, struct ext2_dir_entry_2 * insdir, int bitmapsize, int* newblocks);
void init_dirent(struct ext2_dir_entry_2 * dir, unsigned int inode, unsigned short rec_len, unsigned char name_len, unsigned char file_type, char * name);
void copy_dirent(struct ext2_dir_entry_2 *dir, struct ext2_dir_entry_2 *source, unsigned short rec_len);
int inode_number(struct ext2_inode * inode);