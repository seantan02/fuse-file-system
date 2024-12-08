#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include "wfs.h"

#define DEBUG 1

// global variable
struct shared_state *context = 0;


static int my_getattr(const char *path, struct stat *stbuf) {
  // Implementation of getattr function to retrieve file attributes
  // Fill stbuf structure with the attributes of the file/directory indicated by path
  // Zero out the stat structure first
  if(DEBUG) fprintf(stdout, "my_getattr called with path: %s!\n", path);
  memset(stbuf, 0, sizeof(struct stat));

  // Find the inode for this path
  struct wfs_inode *inode = calloc(1, sizeof(struct wfs_inode));
  int traverse_path_failed = traverse_path((char*)path, inode);
  if (traverse_path_failed) {
	free(inode);
	return traverse_path_failed;
  }

  // Populate stat structure from inode
  stbuf->st_uid = inode->uid;
  stbuf->st_gid = inode->gid;
  stbuf->st_atime = inode->atim;
  stbuf->st_mtime = inode->mtim;
  stbuf->st_size = inode->size;
  stbuf->st_mode = inode->mode;
  stbuf->st_nlink = inode->nlinks;

  free(inode);
  return 0; // Return 0 on success
}

static int my_mknod(const char* path, mode_t mode, dev_t rdev){
  if(DEBUG) fprintf(stdout, "my_mknod called!\n");
  struct wfs_inode *inode = calloc(1, sizeof(struct wfs_inode));
  if(inode == NULL){
	if(DEBUG) printf("ERROR my_mknod: Calloc failed for inode\n");
	exit(-1);
  }

  int traverse_path_failed = traverse_path((char*)path, inode);
  if(!traverse_path_failed){
	if(DEBUG) printf("ERROR my_mknod: Node already exists with path %s\n", path);
	free(inode);
	return -EEXIST;
  }

  int create_dir_failed = create_node(path, mode, 1);
  if(create_dir_failed){
	if(DEBUG) printf("ERROR my_mknod: Faield to create node\n");
	free(inode);
	return create_dir_failed;
  }
  free(inode);
  return 0;
}

static int my_mkdir(const char* path, mode_t mode){
  if(DEBUG) fprintf(stdout, "my_mkdir called!\n");
  struct wfs_inode *inode = calloc(1, sizeof(struct wfs_inode));
  if(inode == NULL){
	if(DEBUG) printf("ERROR my_mkdir: Calloc failed for inode\n");
	exit(-1);
  }

  int traverse_path_failed = traverse_path((char*)path, inode);
  if(!traverse_path_failed){
	if(DEBUG) printf("ERROR my_mkdir: Node already exists with path %s\n", path);
	free(inode);
	return -EEXIST;
  }

  int create_dir_failed = create_node(path, (mode | S_IFDIR), 0);
  if(create_dir_failed){
	if(DEBUG) printf("ERROR my_mkdir: Faield to create node\n");
	free(inode);
	return create_dir_failed;
  }

  free(inode);
  return 0;
}

static int my_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi){
  return -1;
}

static int my_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi){
  if(DEBUG) fprintf(stdout, "my_write called!\n");
  // get sb
  struct wfs_sb *sb = calloc(1, sizeof(struct wfs_sb));
  if(sb == NULL){
	if(DEBUG) printf("ERROR my_write: calloc failed for sb\n");
	return -1;
  }
  read_superblock(0, sb);

  // 1) check limit
  size_t max_size = (D_BLOCK + 1 + BLOCK_SIZE / sizeof(off_t)) * BLOCK_SIZE;
  if(offset + size > max_size) return -ENOSPC; // insufficient space
  // 2) Check if file exist
  struct wfs_inode *inode = calloc(1, sizeof(struct wfs_inode));
  if(inode == NULL){
	if(DEBUG) printf("ERROR my_write: free();calloc for inode failed\n");
	return -1;
  }
  // 3) check if file exist
  int file_not_exist = traverse_path((char*)path, inode);
  if(file_not_exist){
	return -1; //4) error if no inode found
  }
  // 5) check permission
  if (!(inode->mode & S_IWUSR)) {
    free(inode);
    return -EACCES; // no permission
  }
  // 6) which block of data to start writing to
  int blk_to_start = (offset >> power_of_2(BLOCK_SIZE));
  if(DEBUG) printf("DEBUG my_write: blk_to_use is %i\n", blk_to_start);
  // how many blocks do we need
  int num_blocks_needed = size >> power_of_2(BLOCK_SIZE);
  if(size & (BLOCK_SIZE-1)) num_blocks_needed += 1;
  if(DEBUG) printf("DEBUG my_write: blocks needed for write is %i\n", num_blocks_needed);
  // 7) if blk_to_use < 7 then we go through blocks 0-6
  char db_buffer[BLOCK_SIZE], db_buffer2[BLOCK_SIZE]; 
  off_t *db_offset = calloc(1, sizeof(off_t));
  off_t *db_offset2 = calloc(1, sizeof(off_t));
  if(db_offset == NULL || db_offset2 == NULL){
	if(DEBUG) printf("ERROR my_write: calloc failed for db_offset or db_offset2\n");
	return -1;
  }
  
  off_t curr_offset, inode_offset;
  curr_offset = offset & (BLOCK_SIZE-1);
  inode_offset = sb->i_blocks_ptr + (BLOCK_SIZE * inode->num);
  size_t curr_size, total_size_written;
  total_size_written = 0;
  int i, need_indirect, write_to_disk_failed, allocate_db_failed;
  need_indirect = 0;
  if(blk_to_start + num_blocks_needed - 1 > D_BLOCK) need_indirect = 1;
  //inode_changed = 0;
  // e.g: 513 should be 1 for block 1
  if(blk_to_start < IND_BLOCK){
	for(i = blk_to_start; i < IND_BLOCK; i++){
	  if(total_size_written >= size) break;
	  // check if block == 0; Allocate + Update block value if yes else read it;
	  if(inode->blocks[i] == 0){
		allocate_db_failed = allocate_db(context->raid_mode, i, db_buffer, db_offset);
		if(allocate_db_failed){
		  if(DEBUG) printf("ERROR my_write: failed to allocate db\n");
		  return allocate_db_failed;
		}
		inode->blocks[i] = *db_offset;
		//inode_changed = 1;
	  }else{ // read it
		*db_offset = inode->blocks[i];
		read_db(context->raid_mode, i, *db_offset, db_buffer);	
	  }
	  curr_size = BLOCK_SIZE - curr_offset;
	  // write BLOCK_SIZE of data to db_buffer and write to disk
	  memcpy((db_buffer + curr_offset), (buf + total_size_written), curr_size);
	  // write datablock to disk
	  write_to_disk_failed = write_db_to_disk(context->raid_mode, i, *db_offset, BLOCK_SIZE, db_buffer);
	  /*// write to inode if there's any change to inode
	  if(inode_changed){
		write_to_disk_failed = write_to_disk(-1, inode_offset, sizeof(struct wfs_inode), inode);
		if(write_to_disk_failed){
		  if(DEBUG) printf("ERROR my_write: write_to_disk failed for inode\n");
		  return write_to_disk_failed;
		}
		inode_changed = 0;
	  }*/
	  total_size_written += curr_size;
	  curr_offset = 0;
	}
  }
  // if we need to use indirect blocks:
  if(need_indirect){
	// first read in the indirect pointer
	// if blocks[7] == 0, we need to allocate a db to it as usual; else we read it in and go through
	// by converting it to off_t*
	char raid1[] = "1";
	if(inode->blocks[IND_BLOCK] == 0){
	  allocate_db_failed = allocate_db(raid1, i, db_buffer, db_offset); // raid 1 for ind.
	  if(allocate_db_failed){
		if(DEBUG) printf("ERROR my_write: failed to allocate db\n");
		return allocate_db_failed;
	  }
	  inode->blocks[IND_BLOCK] = *db_offset;
	  //inode_changed = 1;
	}else{ // read the indirect block in
	  *db_offset = inode->blocks[IND_BLOCK];
	  read_db(raid1, i, *db_offset, db_buffer);
	}
	// now we can follow the indirect pointer to perform what we need
	if(blk_to_start >= IND_BLOCK){ // that means we did not touch i yet (we didn't use dir ptr;
	  i = blk_to_start;
	}

	i -= IND_BLOCK; // set index respective to indirect data block
	// go through the indirect pointers and write data to it
	off_t *ind_ptr = (off_t*)db_buffer;
	for(; i<(BLOCK_SIZE/sizeof(off_t)); i++){
	  if(total_size_written >= size) break; // done when we write all the buffer data
	  if(*(ind_ptr + i) == 0){ // allocate db
		allocate_db_failed = allocate_db(context->raid_mode, i, db_buffer2, db_offset2);
		if(allocate_db_failed){
		  if(DEBUG) printf("ERROR my_write: failed to allocate db\n");
		  return allocate_db_failed;
		}
		*(ind_ptr + i) = *db_offset2;
	  }else{
		*db_offset2 = *(ind_ptr + i);
		read_db(context->raid_mode, i, *db_offset2, db_buffer2);
	  }
	  curr_size = BLOCK_SIZE - curr_offset;
	  memcpy((db_buffer2 + curr_offset), (buf + total_size_written), curr_size);
	  write_to_disk_failed = write_db_to_disk(context->raid_mode, i+IND_BLOCK, *db_offset2, BLOCK_SIZE, db_buffer2); // save the data
	  if(write_to_disk_failed) return write_to_disk_failed;
	  write_to_disk_failed = write_db_to_disk(raid1, i, *db_offset, BLOCK_SIZE, db_buffer);  // save the indirect ptr after data written
	  if(write_to_disk_failed) return write_to_disk_failed;
	  // update total written and offset
	  total_size_written += curr_size;
	  curr_offset = 0;
	}
    /*// save to inode if it's changed
	if(inode_changed){
	  write_to_disk_failed = write_to_disk(-1, inode_offset, sizeof(struct wfs_inode), inode);
	  if(write_to_disk_failed){
		if(DEBUG) printf("ERROR my_write: write_to_disk failed for inode\n");
		return write_to_disk_failed;
	  }
	  inode_changed = 0;
	}*/
  }
  // update inode
  inode->size += size;
  inode->mtim = time(NULL);
  // save inode
  write_to_disk_failed = write_to_disk(-1, inode_offset, sizeof(struct wfs_inode), (void*)inode);
  if(write_to_disk_failed){
	if(DEBUG) printf("ERROR my_write: write_to_disk failed for inode\n");
	return write_to_disk_failed;
  }  
  //free
  free(sb);
  free(inode);
  free(db_offset);
  free(db_offset2);

  return size;
}

static struct fuse_operations ops = {
    .getattr = my_getattr,
	.mknod = my_mknod,
	.mkdir = my_mkdir,
	.read    = my_read,
	.write   = my_write,
};

// helper functions
int power_of_2(int num) {
    if (num <= 0) {
        return -1; // Return -1 for invalid input
    }
    int power = 0;
    while (num > 1) {
        num >>= 1; // Right shift to divide the number by 2
        power++;
    }
    return power;
}

int find_bit_zero(uint32_t bits){
  if(bits == 0xffffffff || bits == -1){
	if(DEBUG) printf("Bit value indicates full! Bit: %x\n", bits);
    return -1;
  }

  uint32_t y = bits + 1;
  uint32_t z = (y & -y);
  return (unsigned char) power_of_2(z);
}

/*
This function filter the argc and argv
Then populate fuse_args (Should be allocated and empty when passed in)
*/
int filter_args(int argc, char **argv, int *fuse_argc, char **fuse_args){
  // we will loop through the arguments and then check if all disks exist
  int num_disks, fuse_arg_count;
  num_disks = 0;
  fuse_arg_count = 0;
  // we add program name to fuse args
  *(fuse_args+(fuse_arg_count++)) = argv[0];
  // Process arguments; Since last arguments is always mount point then we can skip the last one too
  // we need a wfs_sb to verify each disk
  struct wfs_sb *sb = calloc(1, sizeof(struct wfs_sb));
  if(sb == NULL){
	if(DEBUG) printf("Calloc failed in filter_args\n");
	exit(-1);
  }

  int j = 1;
  while (j < argc-1) {
    // check if it's fuse args
	if(strcmp(argv[j], "-s\0") == 0 || strcmp(argv[j], "-f\0") == 0 || strcmp(argv[j], "-d\0") == 0) {
      // FUSE-related arguments go into fuse_argv
      fuse_args[fuse_arg_count++] = argv[j];
	}else{
	  read_superblock_by_path(argv[j], sb);	  // read in the disk sb
	  if(context->disks[sb->disk_num] != 0){
		if(DEBUG) printf("ERROR at filter_args: duplicate disk used.\n");
		return -1;
	  };
	  context->disks[sb->disk_num] = argv[j]; // assign to its order index
	  if(DEBUG){
		printf("Disk assigned at index %i is %s\n", sb->disk_num, argv[j]);
	  }
	  num_disks++;
	}
	j++;
  }

  fuse_args[fuse_arg_count++] = argv[argc-1]; // mount point
  *fuse_argc = fuse_arg_count;
  if(DEBUG){
	printf("Total fuse args : %i\n", fuse_arg_count);
	for(int i=0; i < fuse_arg_count; i++){
	  printf("Fuse args after filter %i is %s\n", i, fuse_args[i]);
	}
  }

  read_superblock_by_path(context->disks[0], sb);
  
  if(num_disks != sb->total_disks) return -1; // not all disks used
  // update context
  context->num_disks = num_disks; 
  strcpy(context->raid_mode, sb->raid); // copy raid mode over
  // we are good; We dont free inode because we need it;
  free(sb);
  return 0;
}

// funtion to read superblock
void read_superblock_by_path(const char *disk_path, struct wfs_sb *sb) {
    int fd = open(disk_path, O_RDONLY);
    if (fd < 0) {
        perror("Error opening disk file");
        exit(-1);
    }

    ssize_t bytes_read = pread(fd, sb, sizeof(struct wfs_sb), 0);
    if (bytes_read < sizeof(struct wfs_sb)) {
        perror("Error reading superblock");
        close(fd);
        exit(-1);
    }

    close(fd);
    if(DEBUG) printf("Superblock read successfully: RAID mode = %s\n", sb->raid);
}

void read_superblock(int disk_num, struct wfs_sb *sb){
  if(context->disk_fds[disk_num] == -1){
	if(DEBUG) printf("Disk num %i has no open fd.\n", disk_num);
	exit(-1);
  }
  // we not read from the open fd
  int fd = context->disk_fds[disk_num];
  ssize_t bytes_read = pread(fd, sb, sizeof(struct wfs_sb), 0);
  if (bytes_read < sizeof(struct wfs_sb)) {
	perror("Error reading superblock");
	close(fd);
	exit(-1);
  }
  if(DEBUG) printf("Superblock read successfully: RAID mode = %s\n", sb->raid);
}

void read_inode_by_path(const char *disk_path, int i_number, struct wfs_inode *inode){
  struct wfs_sb *sb = calloc(1, sizeof(struct wfs_sb));
  if(sb == NULL){
	if(DEBUG) printf("Calloc failed in function read_inode_by_path for sb\n");
	exit(-1);
  }

  read_superblock_by_path(disk_path, sb);

  off_t offset = sb->i_blocks_ptr + (i_number * BLOCK_SIZE);  // i_number should start at 0 

  int fd = open(disk_path, O_RDONLY);
  if (fd < 0) {
	perror("Error opening disk file");
	exit(-1);
  }

  ssize_t bytes_read = pread(fd, inode, sizeof(struct wfs_inode), offset);
  if (bytes_read < sizeof(struct wfs_inode)){
	perror("Error reading inode\n");
	close(fd);
	exit(-1);
  }

  close(fd);
  if(DEBUG) printf("Inode read successfully: i_number = %i\n", inode->num);
}

void read_inode(int disk_num, int i_number, struct wfs_inode *inode){
  struct wfs_sb *sb = calloc(1, sizeof(struct wfs_sb));
  if(sb == NULL){
	if(DEBUG) printf("Calloc failed in function read_inode for sb\n");
	exit(-1);
  }

  read_superblock(disk_num, sb);

  off_t offset = sb->i_blocks_ptr + (i_number * BLOCK_SIZE);  // i_number should start at 0

  int fd = context->disk_fds[disk_num];
  ssize_t bytes_read = pread(fd, inode, sizeof(struct wfs_inode), offset);
  if (bytes_read < sizeof(struct wfs_inode)){
	perror("Error reading inode\n");
	close(fd);
	exit(-1);
  }

  if(DEBUG) printf("Inode read successfully: i_number = %i\n", inode->num);
}

void read_bitmap(int disk_num, int for_inode, char *dest){
  struct wfs_sb *sb = calloc(1, sizeof(struct wfs_sb));
  if(sb == NULL){
    if(DEBUG) printf("ERROR read_bitmap: calloc failed for sb\n");
    exit(1);
  }
  read_superblock(disk_num, sb); // read from disk 0 since all disk are the same;
  // if for inode
  off_t start_addr;
  size_t size;
  if(for_inode){
	start_addr = sb->i_bitmap_ptr;
	size = sb->d_bitmap_ptr - start_addr;
  }else{
	start_addr = sb->d_bitmap_ptr;
	unsigned int num_data_blocks = sb->num_data_blocks;
	// round down to get the multiple of 4
	size = (num_data_blocks & ~7);
	if(num_data_blocks & 7) size += 8;
	size = size / 8; // 1 byte = 8 bits
  }    

  if(DEBUG )printf("Reading bitmap for size: %li starting at %li\n", size, start_addr);
  int fd = context->disk_fds[disk_num];
  ssize_t bytes_read = pread(fd, dest, size, start_addr);
  if (bytes_read < size){
	perror("Error reading bitmap\n");
	close(fd);
	exit(-1);
  }

  free(sb); // done
  if(DEBUG) printf("Bitmap read successfully\n");
}

int write_to_disk(int disk_num, off_t offset, size_t size, char *src){
  /*
  if disk_num is set to -1 then we will write to all disks else disk[disk_num]
  */
  if(disk_num == -1){
	// for all disks we write into them
	for(int i=0; i < MAX_DISKS; i++){
	  if(context->disk_fds[i] == -1) continue;
	  if (pwrite(context->disk_fds[i], src, size, offset) != size) {
		return -1;
      }
	}
  }else{
	if(context->disk_fds[disk_num] == -1){
	  if(DEBUG) printf("ERROR write_to_disk: Given disk has no open fds\n");
	  exit(-1);
	}
	if (pwrite(context->disk_fds[disk_num], src, size, offset) != size) {
	  return -1;
	}
  }
  return 0;
}

int write_db_to_disk(char *raid_mode, int db_num, off_t offset, size_t size, char *src){
  if(strcmp(raid_mode, "0\0") == 0){
	int disk_to_use = db_num % context->num_disks;
    if(context->disk_fds[disk_to_use] == -1){
      if(DEBUG) printf("ERROR read_db: Given db_block_index points to invalid disk fd.\n");
      exit(-1);
    }
	if(write_to_disk(disk_to_use, offset, size, src) != 0){
	  if(DEBUG) printf("ERROR write_db_to_disk: write_to_disk failed\n");
	  exit(-1);
	}
 
	return 0;
  }else if(strcmp(raid_mode, "1\0") == 0 || strcmp(raid_mode, "1v\0") == 0){
	if(write_to_disk(-1, offset, size, src) != 0){
	  if(DEBUG) printf("ERROR write_db_to_disk: write_to_disk for all disks failed\n");
	  exit(-1);
	}
	return 0;
  }else{
    if(DEBUG) printf("ERROR read_db: unrecognized raid mode: %s\n", raid_mode);
	return -1;
  }
}

void read_db(char *raid_mode, int db_block_index, off_t block_offset, char * dest){
  /*
  This function reads datablock from disks depending on the raid mode and db_block_index and block_offset.
  if raid_mode == 0, this function will read from disks[db_block_index % num_disks]
  else, this function will read either from disk 0 or majority depending if it's 1 or 1v; (db_block_index is ignored)
  */
  // check which raid_mode is it
  char temp_buffer[BLOCK_SIZE];
  int successful_reads = 0;

  if(strcmp(raid_mode, "0\0") == 0){
	// read from disks[db_block_index % context->num_disks]
	int disk_to_use = db_block_index % context->num_disks;
	if(context->disk_fds[disk_to_use] == -1){
	  if(DEBUG) printf("ERROR read_db: Given db_block_index points to invalid disk fd.\n");
	  exit(-1);
	}

	if (pread(context->disk_fds[disk_to_use], temp_buffer, BLOCK_SIZE, block_offset) == BLOCK_SIZE) {
	  memcpy(dest, temp_buffer, BLOCK_SIZE);
	  successful_reads ++; 
	}

	return;
  }else if(strcmp(raid_mode, "1\0") == 0){

    for (int i = 0; i < context->num_disks; i++) {
      if(context->disk_fds[i] == -1){
		if(DEBUG) printf("ERROR read_db: Disk fd is error\n");
		exit(-1);
	  }

	  if (pread(context->disk_fds[i], temp_buffer, BLOCK_SIZE, block_offset) == BLOCK_SIZE) {
        if (successful_reads == 0) {
          memcpy(dest, temp_buffer, BLOCK_SIZE);
        }
        successful_reads++;
		return;
      }
    }
  }else if(strcmp(raid_mode, "1v\0") == 0){
	return;
  }else{
	if(DEBUG) printf("ERROR read_db: unrecognized raid mode: %s\n", raid_mode);
	exit(1);
  }
}

int traverse_path(char *path, struct wfs_inode *inode){
  /*
  This function will traverse a given path and return its inode if exist by populating inode.
  inode set to NULL if failed.
  */
  // we will first retrieve root node
  // make a duplicate of the path
  char *path_copy = strdup(path);
  if(DEBUG) printf("DEBUG traverse_path: Path name traversing: %s\n", path_copy);
  struct wfs_inode *tmp_inode = calloc(1, sizeof(struct wfs_inode));
  if(tmp_inode == NULL){
	if(DEBUG) printf("ERROR find_inode: calloc failed for tmp_inode\n");
	exit(-1);
  }

  // we will now read root inode
  read_inode(0, 0, tmp_inode);

  const char delimiter[2] = "/";
  char *token;

  token = strtok(path_copy, delimiter);
  while(token != NULL) {
	if(DEBUG) printf( "%s\n", token);
	// if there's more token we will then read the tmp_inode
	// then check if tmp_inode is a file or dir
	// for file, we will check if next token is null, failed if not
    // otherwise return file's inode if exist, else return tmp_inode
	// for dir, we will go through block ptrs and follow any non-empty ptr
	// for each ptr we will read the datablock and split datablock in 16s
	// then see if there's a math of name == token
	// if found, update the current inode to the found one and continue
	// if nothing found after all loops, return error
	if(tmp_inode->mode & S_IFDIR){ // if directory
	  char tmp_db[BLOCK_SIZE];
	  struct wfs_dentry *tmp_dentry;
	  unsigned char found = 0;
	  // go through the block field
	  for(int i=0; i < IND_BLOCK; i++){
		if(DEBUG) printf("DEBUG index in traverse_path block index: %i\n", i);
		if(tmp_inode->blocks[i] == 0) continue; // empty ptr
		read_db(context->raid_mode, i, tmp_inode->blocks[i], tmp_db); // read from disk for data block index i
		tmp_dentry = (struct wfs_dentry*) tmp_db;
		for(int j=0; j< (BLOCK_SIZE / sizeof(struct wfs_dentry)); j++){
		  if(strcmp(tmp_dentry[j].name, token) == 0){
			// read the new inode and update curr inode
			read_inode(0, tmp_dentry[j].num, tmp_inode);
			found = 1;
			break;
		  }
		}
		if(found) break;
	  }
	  if(found == 0){
		if(DEBUG) printf("ERROR traverse_path: no matching entry found in directory with name: %s\n", token);
		return -ENOENT;
	  }
	}else{ // if it's a file
	  char *nextToken = strtok(NULL, path_copy);
	  if(nextToken != NULL){
		if(DEBUG) printf("ERROR traverse_path: expect file: %s to have next token as null but get %s\n", token, nextToken);
		exit(-1);
	  }
	}
	token = strtok(NULL, delimiter);
  }

  memcpy(inode, tmp_inode, sizeof(struct wfs_inode)); // copy over the inode if found
  free(tmp_inode); // no memory leak
  free(path_copy);

  return 0;
}

void set_up_context_fds(){
  // this function will looop through all the disks and open up fds base on their number
  // this assume the context contains the final list of disk names
  for(int i=0; i < MAX_DISKS; i++){
	if(context->disks[i] == NULL){
	  context->disk_fds[i] = -1;
	  continue;
	}

	context->disk_fds[i] = open(context->disks[i], O_RDWR);
	if (context->disk_fds[i] < 0) {
	  if(DEBUG) printf("ERROR set_up_context_fds: disk cannot be opened\n");
	  exit(-1);
	}
  }
}

// create
int allocate_inode(struct wfs_inode *inode){
  // 1) Get superblock
  // 2) Get the inode bitmap ptr
  // 3) Read 32 bits (convert to uint32_t) (quicker this way) *Make sure stop and not overread*
  // 4) Check if value called x = 0xffffffff
  // 5) If yes, go to next one
  // 6) Else do: 
  // int y = x+1; int z = (y & -y); unsigned char index = (unsigned char) power_of_2(z);
  // use the index to find the inode; Empty the inode; Then set the inode number;
  // update bit map by newbitmap = x + z
  struct wfs_sb *sb = calloc(1, sizeof(struct wfs_sb));
  if(sb == NULL){
	if(DEBUG) printf("ERROR allocate_inode: calloc failed for sb\n");
	return -1;
  }
  read_superblock(0, sb); // read from disk 0 since all disk are the same;
  // 2)
  unsigned int size, found;
  size = sb->d_bitmap_ptr - sb->i_bitmap_ptr;
  if(DEBUG) printf("DEBUG allocate_inode: allocate size %i for bitmap\n", size);
  char buffer[size];
  read_bitmap(0, 1, buffer);
  uint32_t *bits = (uint32_t*) buffer;

  found = 0;

  int i, index;
  i = 0;
  while(i < (size / 4)){
	uint32_t bit_val = *(bits + i);
    index = find_bit_zero(bit_val);
	if(index == -1){
	  i++;
	  continue;
	}else{
	  found = 1;
	}

	//*(bits + i) += *z;
	*(bits + i) |= (1 << index);
    int write_bitmap_failed;
    write_bitmap_failed = write_to_disk(-1, sb->i_bitmap_ptr, size, (char*)bits); // write to all disks
	if(write_bitmap_failed){
	  if(DEBUG) printf("ERROR allocate_inode: Failed to write to bitmap\n");
	  return write_bitmap_failed;
	}
	break;
  }

  if(found == 0) return -ENOSPC;
  // find the inode and empty it
  read_inode(0, index, inode); // read the inode
  // empty the inode
  memset(inode, 0, sizeof(struct wfs_inode));
  // set inode number back; 
  inode->num = index+(i * 31)+i;

  free(sb); // free sb when done 
  return 0;
}

int allocate_db(char *raid_mode, int num_blocks_in_use, char *dest, off_t *ptr){
  /*
  This function allocate a new data block and return the ptr addr to caller depending on the raid mode;
  If raid mode == 0, num_blocks_in_use will be used to determined which disk to use by num_blocks_in_use%num_disks
  else we will always allocate as if there's only 1 disk. (num_blocks_in_use is ignored)
  */
  // similar to inode but instead we need to utilize raid mode
  struct wfs_sb *sb = calloc(1, sizeof(struct wfs_sb));
  if(sb == NULL){
    if(DEBUG) printf("ERROR allocate_inode: calloc failed for sb\n");
    return -1;
  }
  read_superblock(0, sb); // read from disk 0 since all disk are the same;

  int disk_to_read, disk_to_write;

  if(strcmp(raid_mode, "0\0") == 0){ // raid 0 STRIPE
	// we will use a formula to identify the off_t depending
	disk_to_read = num_blocks_in_use % context->num_disks;
	disk_to_write = disk_to_read;
  }else if(strcmp(raid_mode, "1\0") == 0 || strcmp(raid_mode, "1v\0") == 0){
	disk_to_read = 0;
	disk_to_write = -1;
  }else{
	if(DEBUG) printf("ERROR allocate_db: Invalid raid_mode\n");
	exit(-1);
  }
  // now we will allocate the db and return the off_t pointer
  unsigned int size, num_data_blocks, found;
  num_data_blocks = sb->num_data_blocks;
  size = (num_data_blocks &~7);
  if(num_data_blocks & 7) size += 8;
  size = size / 8;
  if(DEBUG) printf("DEBUG allocate_db: allocate size %i for bitmap\n", size);
  char buffer[size];
  read_bitmap(disk_to_read, 0, buffer);
  uint32_t *bits = (uint32_t*) buffer;
 
  found = 0;
  int i, index;
  i = 0;
  while(i < (size / 4)){
	uint32_t bit_val = *(bits + i);
    index = find_bit_zero(bit_val);
    if(index == -1){
      i++;
      continue;
    }else{
	  found = 1;
	}
 
    *(bits + i) |= (1 << index);
    int write_bitmap_failed;
    write_bitmap_failed = write_to_disk(disk_to_write, sb->d_bitmap_ptr, size, (char*)bits);
    if(write_bitmap_failed){
      if(DEBUG) printf("ERROR allocate_db: Failed to write to bitmap\n");
      return write_bitmap_failed;
    }
    break;
  }
 
  if(found == 0) return -ENOSPC;

  // offset value of datablock = index 
  *ptr = sb->d_blocks_ptr + ((index + (i * 31) + i) * BLOCK_SIZE);  // the offset value in the disk
  read_db(context->raid_mode, disk_to_read, *ptr, dest); // read the db
  // empty the inode
  memset(dest, 0, BLOCK_SIZE);
 
  free(sb); // free sb when done 
  return 0;
}

int create_node(const char *path, mode_t mode, int is_file){
  /*
  This function creates an entry according to the given path and is_file
  is_file: 0 if not, else it is;
  For non-file, we will treat it as dir and create '.', and '..' entry.
  */
  // check if the path already exist
  struct wfs_inode *inode = calloc(1, sizeof(struct wfs_inode));
  struct wfs_inode *tmp_inode = calloc(1, sizeof(struct wfs_inode));
  if(inode == NULL || tmp_inode == NULL){
	if(DEBUG) printf("ERROR create_node: Calloc failed for inode or tmp_inode\n");
	return -1;
  }

  int inode_allocation_stat = allocate_inode(inode);
  if(inode_allocation_stat != 0){
	if(DEBUG) printf("ERROR create_node: Allocate inode failed!\n");
	return inode_allocation_stat;
  }
  // initialize the inode
  inode->mode = mode;
  inode->uid = getuid();
  inode->gid = getgid();
  inode->size = 0;
  inode->nlinks = 1;
  time_t now = time(NULL);
  inode->atim = now;
  inode->mtim = now;
  inode->ctim = now;
  memset(inode->blocks, 0, sizeof(off_t) * N_BLOCKS);

  // create dentry
  struct wfs_dentry *dentry = calloc(1, sizeof(struct wfs_dentry));
  if(dentry == NULL){
	if(DEBUG) printf("ERROR create_node: calloc failed for dentry\n");
	return -1;
  }
  // replace the last '/' if the last character is a '/'
  char *mutable_path, *path_copy, *parent;

  mutable_path = strdup(path);
  int path_len, parent_path_len;
  path_len = strlen(path);
  if(mutable_path[path_len-1] == '/'){
	mutable_path[path_len-1] = '\0';
	path_len -= 1;
  }
  if(DEBUG) printf("DEBUG create_node: path name contains '/' at last and so removed. Name: %s\n", mutable_path);
  // first update the parent
  path_copy = strdup(mutable_path);
  parent = dirname(path_copy);
  if(DEBUG) printf("DEBUG parent name is %s\n", parent);
  if(*(parent) == '.' || strcmp(parent, mutable_path) == 0){
	if(DEBUG) printf("ERROR create_node: parent directory not found\n");
	return -ENOENT;
  }
  int traverse_path_failed = traverse_path(parent, tmp_inode);
  if(traverse_path_failed){
	return traverse_path_failed;
  }

  parent_path_len = strlen(parent);
  char name[MAX_NAME];
  memset(name, 0, MAX_NAME); // set all to zeroes
  // check if name exceeded the length
  if(mutable_path[parent_path_len] == '/'){ // we skip one
	if(path_len - parent_path_len - 1 > (MAX_NAME-1)){
	  if(DEBUG) printf("ERROR create_node: node name too long: %s\n", mutable_path);
	  return -1;
	}
	strcpy(name, (mutable_path+parent_path_len+1));
  }else{
	if(path_len - parent_path_len > (MAX_NAME-1)){
	  if(DEBUG) printf("ERROR create_node: node name too long: %s\n", mutable_path);
	  return -1;
	}
	strcpy(name, (mutable_path+parent_path_len)); 
  }

  if(DEBUG) printf("DEBUG create_node: node name extracted: %s\n", name);
  // update dentry
  strcpy(dentry->name, name);
  dentry->num = inode->num;
  // add dentry to parent inode and then write to disk
  int add_dentry_failed = add_dentry(tmp_inode, dentry);
  if(add_dentry_failed){
	if(DEBUG) printf("ERROR create_node: Failed to add dentry.\n");
	return add_dentry_failed;
  }

  struct wfs_sb *sb = calloc(1, sizeof(struct wfs_sb));
  if(sb == NULL){
	if(DEBUG) printf("ERROR create_node: calloc failfed for sb");
	exit(-1);
  }
  read_superblock(0, sb);
  off_t inode_offset = sb->i_blocks_ptr + (inode->num * BLOCK_SIZE);
  write_to_disk(-1, inode_offset, sizeof(struct wfs_inode), (void*) inode);

  free(sb);  
  free(inode);
  free(tmp_inode);
  free(path_copy);
  free(mutable_path);
  free(dentry);

  return 0;
}


int add_dentry(struct wfs_inode *inode, struct wfs_dentry *dentry){
  // 1) Get the current size of entries
  // 2) Go through block 0 - 6 in the inode's blocks
  // 3) For each of the block do:
  //	if block == null || block == 0
  //	a) allocate datablock; update inode's block value for this;
  //	b) set the offset to 0 and break
  //	else:
  //	c) read the block
  //	d) (dentry *) the value; go through the dentry one by one to find the first empty spot;
  //	e) if no empty spot found continue; Otherwise we set offset to the spot then break;
  // 4) if no free data block found, return disk memory error
  // 5) else do:
  // 6) memcpy to datablock from dentry at offset; inode's entry size+= sizeof(dentry);
  // 7) write to disk for inode and db
  // get superblock
  struct wfs_sb *sb = calloc(1, sizeof(struct wfs_sb));
  if(sb == NULL){
	if(DEBUG) printf("ERROR add_dentry: calloc failfed for sb");
	exit(-1);
  }
  read_superblock(0, sb);

  // 1)
  int dentry_index = -1;
  char db_buffer[BLOCK_SIZE];
  struct wfs_dentry *tmp_dentry = 0;
  off_t *db_offset = calloc(1, sizeof(off_t));
  if(db_offset == NULL){
	if(DEBUG) printf("ERROR add_dentry: Calloc failed for db_num;\n");
	exit(-1);
  }

  // 2)
  int i;
  for(i=0; i<IND_BLOCK; i++){
	// 3)
	if(dentry_index >= 0) break; // found one
	if(inode->blocks[i] == 0){
	  int allocate_db_failed = allocate_db(context->raid_mode, i, db_buffer, db_offset);
	  if(allocate_db_failed){
		if(DEBUG) printf("ERROR add_dentry: failed to allocate db\n");
		return allocate_db_failed;
	  }

	  inode->blocks[i] = *db_offset;
	  dentry_index = 0;
	  continue;
	}else{
	//char *raid_mode, int db_block_index, off_t block_offset, char * dest
	  read_db(context->raid_mode, i, inode->blocks[i], db_buffer);
	  tmp_dentry = (struct wfs_dentry*) db_buffer;
	  for(int j=0; j < (BLOCK_SIZE/sizeof(struct wfs_dentry)); j++){
		if(strcmp(tmp_dentry[j].name, "\0") != 0) continue;
		dentry_index = j;
		*db_offset = inode->blocks[i];
		break;
	  }
	}
  }

  if(dentry_index == -1) return -ENOSPC;
  tmp_dentry = (struct wfs_dentry*) db_buffer;
  memcpy((tmp_dentry + dentry_index), (void *)dentry, sizeof(struct wfs_dentry));
  // update the size and nlinks only if the dentry is a non-special entry
  if (strcmp(dentry->name, ".") != 0 && strcmp(dentry->name, "..") != 0) {
	inode->size += sizeof(struct wfs_dentry);
    inode->nlinks += 1;
  }
  // write to disk
  off_t inode_offset = sb->i_blocks_ptr + (inode->num * BLOCK_SIZE);
  write_to_disk(-1, inode_offset, sizeof(struct wfs_inode), (void*) inode);
  // write a BLOCK of dentry datablock to disk depending on raid mode and datablock index
  write_db_to_disk(context->raid_mode, i-1, *db_offset, BLOCK_SIZE, db_buffer);

  if(DEBUG){
	read_db(context->raid_mode, i, *db_offset, db_buffer);
	tmp_dentry = (struct wfs_dentry*) db_buffer;
	printf("Disk show dentry as dentry name: %s num :%i\n", tmp_dentry->name, tmp_dentry->num);
  }

  // free all
  free(sb);
  free(db_offset); 
  return 0;
}


int main(int argc, char *argv[]) {
  // Initialize FUSE with specified operations
  if(argc < 5) return -1; // has to at least be more than 5 arguments since we need at least 1 disk
  // Filter argc and argv here and then pass it to fuse_main
  context = calloc(1, sizeof(struct shared_state));
  if(context == NULL){
	if(DEBUG) printf("ERROR main: calloc failed for state.\n");
	exit(-1);
  }

  char *fuse_argv[argc];
  int *fuse_argc = calloc(1, sizeof(int));
  if(fuse_argc == NULL){
	if(DEBUG) printf("Calloc failed in main for fuse_argc\n");
	exit(-1);
  }

  if(filter_args(argc, argv, fuse_argc, fuse_argv) != 0){
	if(DEBUG) printf("filter_args failed!\n");
	exit(-1);
  }

  fuse_argv[*fuse_argc] = NULL;

  // open up all the disks and add the fds to the context
  set_up_context_fds(context);

  int result = fuse_main(*fuse_argc, fuse_argv, &ops, NULL);
  free(context); // prevent memory leak
  return result;
}







