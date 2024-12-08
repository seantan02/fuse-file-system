#include <time.h>
#include <sys/stat.h>

#define BLOCK_SIZE (512)
#define MAX_NAME   (28)

#define D_BLOCK    (6)
#define IND_BLOCK  (D_BLOCK+1)
#define N_BLOCKS   (IND_BLOCK+1)

/*
  The fields in the superblock should reflect the structure of the filesystem.
  `mkfs` writes the superblock to offset 0 of the disk image. 
  The disk image will have this format:

          d_bitmap_ptr       d_blocks_ptr
               v                  v
+----+---------+---------+--------+--------------------------+
| SB | IBITMAP | DBITMAP | INODES |       DATA BLOCKS        |
+----+---------+---------+--------+--------------------------+
0    ^                   ^
i_bitmap_ptr        i_blocks_ptr

*/

// Superblock
struct wfs_sb {
    size_t num_inodes;
    size_t num_data_blocks;
    off_t i_bitmap_ptr;
    off_t d_bitmap_ptr;
    off_t i_blocks_ptr;
    off_t d_blocks_ptr;
    // Extend after this line
	char raid[3];	
	unsigned char disk_num;
	unsigned char total_disks;
};

// Inode
struct wfs_inode {
    int     num;      /* Inode number */
    mode_t  mode;     /* File type and mode */
    uid_t   uid;      /* User ID of owner */
    gid_t   gid;      /* Group ID of owner */
    off_t   size;     /* Total size, in bytes */
    int     nlinks;   /* Number of links */

    time_t atim;      /* Time of last access */
    time_t mtim;      /* Time of last modification */
    time_t ctim;      /* Time of last status change */

    off_t blocks[N_BLOCKS]; /* Block 0 - 6 is direct ptr, Block 7 is indirect ptr */
};

// Directory entry
struct wfs_dentry {
    char name[MAX_NAME];
    int num;
};

//Added
// definition
#define MAX_DISKS 10
// Struct
struct shared_state{
  char *disks[MAX_DISKS];
  int num_disks;
  char raid_mode[3];
  int disk_fds[MAX_DISKS];
};

// functions declaration
int power_of_2(int num);
//int find_bit_zero(uint32_t bits, uint32_t *z);
int filter_args(int argc, char **argv, int *fuse_argc, char **fuse_args);
void read_superblock_by_path(const char *disk_path, struct wfs_sb *sb);
void read_superblock(int disk_num, struct wfs_sb *sb);
void read_inode_by_path(const char *disk_path, int i_number, struct wfs_inode *inode);
void read_inode(int disk_num, int i_number, struct wfs_inode *inode);
void read_bitmap(int disk_num, int for_inode, char *dest);
int write_to_disk(int disk_num, off_t offset, size_t size, char *src);
int write_db_to_disk(char *raid_mode, int db_num, off_t offset, size_t size, char *src);
void read_db(char *raid_mode, int db_block_index, off_t block_offset, char * dest);
int traverse_path(char *path, struct wfs_inode *inode);
void set_up_context_fds();
int allocate_inode(struct wfs_inode *inode);
int allocate_db(char *raid_mode, int num_blocks_in_use, char *dest, off_t *ptr);
int create_node(const char *path, mode_t mode, int is_file);
int add_dentry(struct wfs_inode *inode, struct wfs_dentry *dentry);
