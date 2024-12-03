#include <unistd.h>
#include <sys/types.h>
#include "wfs.h" // include the structs: superblock, inode, data block
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_DISKS 10
#define DEBUG 0

/**
 Implementation steps as guide:
 1. First, include necessary headers:
 2. Use a data structure or global variables to store:
 - RAID mode
 - Disk image files
 - Number of inodes
 - Number of data blocks
 3. a main function that will:
 - parse command line arguments
 - validate arguments (RAID MODE, # disk, more)
 - calculate bitmap sizes
     - block size: BLOCK_SIZE (512 by default)
 - initialize the superblock
 - create the root inode
 - write these structures to all disk images
**/

// global variable
unsigned char disk_count = 0;
unsigned char total_disks = 0;

struct mkfs_info {
  char RAID_mode[3];
  char *disks[MAX_DISKS];
  uint num_inodes;
  uint num_data_b;
};

int validate_args(int argc, char **argv){
  if(argc < 9) return -1; // cannot be less than 9
  // loop through the string and check if '-r, -d, -i, -b' all exist
  unsigned char found_r, found_d, found_i, found_b;
  found_r = 0;
  found_d = 0;
  found_i = 0;
  found_b = 0;

  char *tmp;
  char toCmp[] = "0";
  char arg_r[] = "-r";
  char arg_d[] = "-d";
  char arg_i[] = "-i";
  char arg_b[] = "-b";


  for(int i=0; i < argc; i++){
	tmp = *(argv +i);
	if(tmp == NULL) continue;
	if(strcmp(tmp, arg_r) == 0){
	  if(i+1 >= argc) return -1; // prevent index out of bound
	  if(strcmp(*(argv+i+1), "0") != 0 && strcmp(*(argv+i+1), "1") != 0 && strcmp(*(argv+i+1), "1v") != 0)
		return -1;
	  found_r = 1;
	}

	if(strcmp(tmp, arg_d) == 0){
	  found_d = 1;
	}

	if(strcmp(tmp, arg_i) == 0){
	  if(i+1 >= argc) return -1; // prevent index out of bound
	  int num_inode = atoi(*(argv+i+1));
	  if(num_inode == 0 && strcmp(*(argv+i+1), toCmp) != 0) return -1; // if invalid -i
	  if(num_inode <= 0) return -1;
	  found_i = 1;
	}

	if(strcmp(tmp, arg_b) == 0){
	  if(i+1 >= argc) return -1; // prevent index out of bound
	  int b = atoi(*(argv+i+1));
	  if(b == 0 && strcmp(*(argv+i+1), toCmp) != 0) return -1; // if invalid -b
	  if(b < 0) return -1;
	  found_b = 1;
	}
  }

  if(!found_r || !found_d || !found_i || !found_b){
	if(DEBUG) printf("One of the argument not found! r, d, i, b: %i %i %i %i\n",
		found_r, found_d, found_i, found_b);
	return -1;
  }
  return 0;
}

int parse_args(int argc, char **argv, struct mkfs_info *mkfs_information){
  if(validate_args(argc, argv) != 0) return -1;
  
  // we go through the string using token and find each parameters
  char *tmp;
  int i = 0;
  int num_inodes, b;
  unsigned char d_count = 0;

  char arg_r[] = "-r";
  char arg_d[] = "-d";
  char arg_i[] = "-i";
  char arg_b[] = "-b";

  while(i < argc){
	tmp = *(argv+i);
	if(tmp == NULL){
	  i++;
	  continue;
	}
	// if this is one of the arg then we take next one parse it and i++
	if(strcmp(tmp, arg_r) == 0){
	  strcpy(mkfs_information->RAID_mode, *(argv+i+1));
	}else if(strcmp(tmp, arg_d) == 0){
	  (mkfs_information->disks)[d_count++] = *(argv+i+1);

	}else if(strcmp(tmp, arg_i) == 0){
	  num_inodes = atoi(*(argv+i+1));
	  // make it multiple of 32
	  if(num_inodes & 31) num_inodes += 32;
	  num_inodes &= ~31; 
	  mkfs_information->num_inodes = num_inodes;

	}else if(strcmp(tmp, arg_b) == 0){
	  b = atoi(*(argv+i+1));
	  // make it multiple of 32
	  if(b & 31) b += 32;
	  b &= ~31;
	  mkfs_information->num_data_b = b;
	}
	i++;
  }

  // update d_count in global
  total_disks = d_count;

  return 0;
}

void sb_init(uint num_inode, uint num_data_b, struct mkfs_info *mkfs_information,  struct wfs_sb *superblock){
  // How many block do we need for bitmap?
  // inode bitmap = # inode ceil_div by (BLOCK_SIZE * 8)
  // data_bitmap = # data blocks ceil_div by (BLOCK_SIZE * 8)
  int i_bm, db_bm;
  // how many bytes inodes need
  i_bm = num_inode / 8; // guaranteed to be divisible by 8
  if(DEBUG) printf("# block allocate for inode bitmap is :%d\n", i_bm);
  // how many bytes does db need
  db_bm = (num_data_b & ~7) / 8;
  if(num_data_b & 7) db_bm += 1;
  if(DEBUG) printf("# block allocate for data block bitmap is :%d\n",db_bm);

  superblock->num_inodes = num_inode;
  superblock->num_data_blocks = num_data_b;
  superblock->i_bitmap_ptr = sizeof(struct wfs_sb);
  superblock->d_bitmap_ptr = superblock->i_bitmap_ptr + i_bm; // offset from inode bitmap
  // inodes should be block aligned
  superblock->i_blocks_ptr = ((superblock->d_bitmap_ptr + db_bm) & ~(BLOCK_SIZE-1)); // offset from db_bitmap
  if((superblock->d_bitmap_ptr + db_bm) & (BLOCK_SIZE-1)) superblock->i_blocks_ptr += BLOCK_SIZE;
  // data block should be block aligned too
  superblock->d_blocks_ptr = superblock->i_blocks_ptr + (mkfs_information->num_inodes * BLOCK_SIZE); 
  // raid
  strcpy(superblock->raid, mkfs_information->RAID_mode);
  superblock->disk_num = disk_count++; // increment after use
  superblock->total_disks = total_disks;

  return;
}

void init_root_inode(struct wfs_sb *superblock, struct wfs_inode *root_inode){
  root_inode->num = 0;
  root_inode->mode = S_IFDIR |  S_IRWXU; // directory and absolute permission for owner
  root_inode->uid = getuid();
  root_inode->gid = getgid();
  root_inode->size = 0;
  root_inode->nlinks = 1;
  time_t now = time(NULL);
  root_inode->atim = now;
  root_inode->mtim = now;
  root_inode->ctim = now;
  memset(root_inode->blocks, 0, sizeof(off_t) * N_BLOCKS);
}

void set_up_disk(char *disk, struct wfs_sb *superblock, struct wfs_inode *root_inode){
  /**
  This function writes the superblock and root_inode and create a data block according to the inode into a disk
  **/  
  int fd = open(disk, O_RDWR);
  if (fd == -1) {
    if(DEBUG) printf("Failed to open disk image");
    exit(-1);
  }
  // check if the disk is big enough
  struct stat disk_stat;
  if (fstat(fd, &disk_stat) < 0) {
    perror("Failed to get disk image size");
    exit(-1);
  }
  off_t required_size = superblock->d_blocks_ptr + (superblock->num_data_blocks * BLOCK_SIZE);
  if (required_size > disk_stat.st_size) {
    if(DEBUG) fprintf(stderr, "Disk image is too small. Required: %lld, Actual: %lld\n", 
                (long long)required_size, (long long)disk_stat.st_size);
    exit(-1);
  }

  if (write(fd, superblock, sizeof(struct wfs_sb)) != sizeof(struct wfs_sb)) {
    if(DEBUG) printf("Failed to write superblock");
    exit(-1);
  }

  uint inode_bitmap_size = (superblock->d_bitmap_ptr - superblock->i_bitmap_ptr);
  uint db_bitmap_size = (superblock->i_blocks_ptr - superblock->d_bitmap_ptr);

  unsigned char *i_bitmap = calloc(1, inode_bitmap_size);
  unsigned char *d_bitmap = calloc(1, db_bitmap_size);
  i_bitmap[0] += (1 << 0); // set first bit to 1 
  if (write(fd, i_bitmap, inode_bitmap_size) != inode_bitmap_size) {
    if(DEBUG) printf("Failed to write inode bitmap");
    exit(-1);
  }
  free(i_bitmap);

  if (write(fd, d_bitmap, db_bitmap_size) != db_bitmap_size) {
    if(DEBUG) printf("Failed to write data block bitmap");
    exit(-1);
  }
  free(d_bitmap);

  if (write(fd, root_inode, sizeof(struct wfs_inode)) != sizeof(struct wfs_inode)) {
    if(DEBUG) printf("Failed to write root inode");
    exit(-1);
  }
  close(fd);
}

int main(int argc, char **argv){
  struct mkfs_info *mkfs_information = calloc(1, sizeof(struct mkfs_info));
  if(parse_args(argc, argv, mkfs_information) != 0) return 1;
  if(DEBUG){
	printf("Struct of mkfs after parsing args: r: %s, i: %i, d: %d\n", mkfs_information->RAID_mode, 
			mkfs_information->num_inodes, mkfs_information->num_data_b);
	for(int i=0; i<MAX_DISKS; i++){
	  printf("Disk %i: %s\n", i, mkfs_information->disks[i]);
	}
  }

  struct wfs_sb *superblock = calloc(1, sizeof(struct wfs_sb));
  sb_init(mkfs_information->num_inodes, mkfs_information->num_data_b, mkfs_information, superblock);
  if(DEBUG){
	printf("Superblock num_inodes: %ld, num_data_blocks: %ld, i_bitmap_ptr: %ld, d_bitmap_ptr: %ld, i_blocks_ptr: %ld,  d_blocks_ptr: %ld\n", superblock->num_inodes, superblock->num_data_blocks, superblock->i_bitmap_ptr, superblock->d_bitmap_ptr, superblock->i_blocks_ptr, superblock->d_blocks_ptr);
  }

  struct wfs_inode *root_inode = calloc(1, sizeof(struct wfs_inode));
  init_root_inode(superblock, root_inode);
  if(DEBUG){
	printf("Root inode: num: %d, mode: %o, uid: %d, gid: %d, size: %lld bytes, nlinks: %d, atim: %s, mtim: %s, ctim: %s, block 0: %lld\n", root_inode->num, root_inode->mode, root_inode->uid, root_inode->gid, (long long) root_inode->size, root_inode->nlinks, ctime(&root_inode->atim), ctime(&root_inode->mtim), ctime(&root_inode->ctim), (long long) root_inode->blocks[0]);
  }

  char *disk;
  // now write to each disks
  for(int i=0; i < MAX_DISKS; i++){
	if(mkfs_information->disks[i] == NULL) continue; // skip if it's nothing
	disk = mkfs_information->disks[i];
	set_up_disk(disk, superblock, root_inode);
  }

  // free superblock and inode after
  free(mkfs_information);
  free(superblock);
  free(root_inode);

  return 0;
}




















