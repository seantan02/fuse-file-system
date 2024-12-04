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
struct filesystem_context{
  char *disks[MAX_DISKS];
  int num_disks;
  char raid_mode[3];
  int disk_fds[MAX_DISKS];
  struct wfs_inode *curr_inode;
};

// functions declaration
void read_superblock(const char *disk_path, struct wfs_sb *sb);
void read_inode(const char *disk_path, int i_number, struct wfs_inode *inode);
int filter_args(int argc, char **argv, struct filesystem_context *context, int *fuse_argc, char **fuse_args);
void find_inode(char *path, struct wfs_inode *inode);







