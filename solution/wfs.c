#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include "wfs.h"

#define DEBUG 1

// global variable
struct shared_state *context = 0;


static int my_getattr(const char *path, struct stat *stbuf) {
  // Implementation of getattr function to retrieve file attributes
  // Fill stbuf structure with the attributes of the file/directory indicated by path
  // Zero out the stat structure first
  memset(stbuf, 0, sizeof(struct stat));

  // Handle root directory
  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;  // Directory with read/write/execute permissions
    stbuf->st_nlink = 2;  // . and ..
    return 0;
  }

  // Find the inode for this path
  struct wfs_inode *inode = calloc(1, sizeof(struct wfs_inode));
  traverse_path((char *)path, inode);
  if (inode == NULL) {
    return -ENOENT;  // File not found
  }

  // Populate stat structure from inode
  stbuf->st_uid = inode->uid;
  stbuf->st_gid = inode->gid;
  stbuf->st_atime = inode->atim;
  stbuf->st_mtime = inode->mtim;
  stbuf->st_size = inode->size;
  stbuf->st_mode = inode->mode;
  stbuf->st_nlink = inode->nlinks;

  return 0; // Return 0 on success
}

static int my_mknod(const char* path, mode_t mode, dev_t rdev){
  return -1;
}

static int my_mkdir(const char* path, mode_t mode){
  return -1;
}

static struct fuse_operations ops = {
    .getattr = my_getattr,
	.mkdor = my_mknod,
	.mkdir = my_mkdir
};

// helper functions
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

void read_db(char *raid_mode, int num_disks, off_t block_offset, char * dest){
  // check which raid_mode is it
  if(strcmp(raid_mode, "0\0") == 0){
	return;
  }else if(strcmp(raid_mode, "1\0") == 0){
	int successful_reads = 0;
    char temp_buffer[BLOCK_SIZE];

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

void traverse_path(char *path, struct wfs_inode *inode){
  /*
  This function will traverse a given path and return its inode if exist by populating inode.
  inode set to NULL if failed.
  */
  // we will first retrieve root node
  struct wfs_inode *tmp_inode = calloc(1, sizeof(struct wfs_inode));
  if(tmp_inode == NULL){
	if(DEBUG) printf("ERROR find_inode: calloc failed for tmp_inode\n");
	exit(-1);
  }

  // we will now read root inode
  read_inode(0, 0, tmp_inode);

  const char delimiter[2] = "/";
  char *token;

  token = strtok(path, delimiter);
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
		read_db(context->raid_mode, context->num_disks, 0, tmp_db);
		tmp_dentry = (struct wfs_dentry*) tmp_db;
		for(int j=0; j< (BLOCK_SIZE / sizeof(struct wfs_dentry)); j++){
		  if(strcmp(tmp_dentry[i].name, token) == 0){
			// read the new inode and update curr inode
			read_inode(0, tmp_dentry[i].num, tmp_inode);
			found = 1;
			break;
		  }
		}
		if(found) break;
	  }
	  if(found == 0){
		if(DEBUG) printf("ERROR traverse_path: no matching entry found in directory with name: %s\n", token);
		if(inode != NULL && inode != 0) free(inode);
		inode = NULL;
		return;
	  }
	}else{ // if it's a file
	  char *nextToken = strtok(NULL, path);
	  if(nextToken != NULL){
		if(DEBUG) printf("ERROR traverse_path: expect file: %s to have next token as null but get %s\n", token, nextToken);
		exit(-1);
	  }
	}
	token = strtok(NULL, path);
  }

  memcpy(inode, tmp_inode, sizeof(struct wfs_inode)); // copy over the inode if found
  free(tmp_inode); // no memory leak
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







