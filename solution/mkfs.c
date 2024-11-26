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

#include "wfs.h" // include the structs: superblock, inode, data block
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_DISKS 10
#define DEBUG 1
/**
27 struct wfs_sb {
 28     size_t num_inodes;
 29     size_t num_data_blocks;
 30     off_t i_bitmap_ptr;
 31     off_t d_bitmap_ptr;
 32     off_t i_blocks_ptr;
 33     off_t d_blocks_ptr;
 34     // Extend after this line
 35 };
 36 
 37 // Inode
 38 struct wfs_inode {
 39     int     num;       Inode number 
 40     mode_t  mode;      File type and mode
 41     uid_t   uid;       User ID of owner 
 42     gid_t   gid;       Group ID of owner 
 43     off_t   size;      Total size, in bytes 
 44     int     nlinks;    Number of links 
 45 
 46     time_t atim;       Time of last access 
 47     time_t mtim;       Time of last modification 
 48     time_t ctim;       Time of last status change 
 49 
 50     off_t blocks[N_BLOCKS];
 51 };
 52 
 53 // Directory entry
 54 struct wfs_dentry {
 55     char name[MAX_NAME];
 56     int num;
  };
**/

struct mkfs_info {
  unsigned char RAID_mode;
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

  char *tmp1, *tmp2;
  char toCmp[] = "0";
  for(int i=0; i < argc; i++){
	tmp1 = *(argv +i);
	tmp2 = strchr(tmp1, '-');
	if(tmp2 == NULL) continue;
	if(*(tmp2+1) == 'r'){
	  if(i+1 >= argc) return -1; // prevent index out of bound
	  int r = atoi(*(argv+i+1));
	  if(r == 0 && strcmp(*(argv+i+1), toCmp) != 0) return -1; // if invalid -r
	  if(r < 0) return -1;
	  found_r = 1;
	}

	if(*(tmp2+1) == 'd'){
	  found_d = 1;
	}

	if(*(tmp2+1) == 'i'){
	  if(i+1 >= argc) return -1; // prevent index out of bound
	  int num_inode = atoi(*(argv+i+1));
	  if(num_inode == 0 && strcmp(*(argv+i+1), toCmp) != 0) return -1; // if invalid -i
	  if(num_inode <= 0) return -1;
	  found_i = 1;
	}

	if(*(tmp2+1) == 'b'){
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
  char *tmp1, *tmp2;
  int i = 0;
  int r, num_inodes, b, d_count;
  d_count = 0;

  while(i < argc){
	tmp1 = *(argv+i);
	tmp2 = strchr(tmp1, '-');
	if(tmp2 == NULL){
	  i++;
	  continue;
	}
	// if this is one of the arg then we take next one parse it and i++
	if(*(tmp2+1) == 'r'){
	  r = atoi(*(argv+i+1));
	  mkfs_information->RAID_mode = r;	
	}else if(*(tmp2+1) == 'd'){
	  (mkfs_information->disks)[d_count++] = *(argv+i+1);
	}else if(*(tmp2+1) == 'i'){
	  num_inodes = atoi(*(argv+i+1));
	  // make it multiple of 32
	  if(num_inodes & 31) num_inodes += 32;
	  num_inodes &= ~31; 
	  mkfs_information->num_inodes = num_inodes;
	}else if(*(tmp2+1) == 'b'){
	  b = atoi(*(argv+i+1));
	  mkfs_information->num_data_b = b;
	}
	i++;
  }

  return 0;
}

void sb_init(uint num_inode, uint num_data_b){
  // How many block do we need for bitmap?
  // inode bitmap = # inode ceil_div by (BLOCK_SIZE * 8)
  // data_bitmap = # data blocks ceil_div by (BLOCK_SIZE * 8)
  int total_bm, i_bm, db_bm;
  i_bm = num_inode >> 10;
  if(num_inode & 1023) i_bm++; // rounding up basically
  if(DEBUG) printf("# block allocate for inode bitmap is :%d\n", i_bm);
  db_bm = num_data_b >> 10;
  if(num_data_b & 1023) db_bm++;
  if(DEBUG) printf("# block allocate for data block bitmap is :%d\n",db_bm);

  total_bm = i_bm + db_bm;
  
  return;
}

void write_to_disk(){
  return;
}

int main(int argc, char **argv){
  struct mkfs_info *mkfs_information = calloc(1, sizeof(struct mkfs_info));
  if(parse_args(argc, argv, mkfs_information) != 0) return -1;
  if(DEBUG){
	printf("Struct of mkfs after parsing args: r: %i, i: %i, d: %d\n", mkfs_information->RAID_mode, 
			mkfs_information->num_inodes, mkfs_information->num_data_b);
	for(int i=0; i<MAX_DISKS; i++){
	  printf("Disk %i: %s\n", i, mkfs_information->disks[i]);
	}
  }

  sb_init(mkfs_information->num_inodes, mkfs_information->num_data_b);

  return 0;
}




















