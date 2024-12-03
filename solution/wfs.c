#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include "wfs.h"

#define DEBUG 1

static int my_getattr(const char *path, struct stat *stbuf) {
    // Implementation of getattr function to retrieve file attributes
    // Fill stbuf structure with the attributes of the file/directory indicated by path
    // ...

    return 0; // Return 0 on success
}

static struct fuse_operations ops = {
    .getattr = my_getattr,
    // Add other functions (read, write, mkdir, etc.) here as needed
};

// helper functions
/*
This function filter the argc and argv
Then populate fuse_args (Should be allocated and empty when passed in)
*/
int filter_args(int argc, char **argv, struct disk_information *df, int *fuse_argc, char **fuse_args){
  // we will loop through the arguments and then check if all disks exist
  int num_disks, fuse_arg_count, total_disk;
  num_disks = 0;
  fuse_arg_count = 0;

  // Process arguments; Since last arguments is always mount point then we can skip the last one too
  int j = 1;
  while (j < argc-1) {
    // check if it's fuse args
	if(strcmp(argv[j], "-s\0") == 0 || strcmp(argv[j], "-f\0") == 0) {
      // FUSE-related arguments go into fuse_argv
      fuse_args[fuse_arg_count++] = argv[j];
	}else{
	  df->disks[num_disks++] = argv[j];
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
  // now check if all disks are valid and we have the exact number of disks created
  struct wfs_sb *sb = calloc(1, sizeof(struct wfs_sb));
  if(sb == NULL){
	if(DEBUG) printf("Calloc failed in filter_args\n");
	exit(-1);
  }

  if(DEBUG) printf("Disk name at index 0 : %s\n", df->disks[0]);
  read_superblock(df->disks[0], sb);
  total_disk = sb->total_disks;
  
  if(num_disks != total_disk) return -1; // not all disks used
  
  for(int i = 1; i < num_disks; i++){
	if(df->disks[i] == NULL) continue;
	read_superblock(df->disks[i], sb); // should exit if there's error
  }

  // we are good
  free(sb);
  return 0;
}

// funtion to read superblock
void read_superblock(const char *disk_path, struct wfs_sb *sb) {
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
    printf("Superblock read successfully: RAID mode = %s\n", sb->raid);
}


int main(int argc, char *argv[]) {
  // Initialize FUSE with specified operations
  if(argc < 5) return -1; // has to at least be more than 5 arguments since we need at least 1 disk
  // Filter argc and argv here and then pass it to fuse_main
  struct disk_information *df = calloc(1, sizeof(struct disk_information));
  char *fuse_argv[argc];
  int *fuse_argc = calloc(1, sizeof(int));
  if(fuse_argc == NULL){
	if(DEBUG) printf("Calloc failed in main for fuse_argc\n");
	exit(-1);
  }

  if(filter_args(argc, argv, df, fuse_argc, fuse_argv) != 0){
	if(DEBUG) printf("filter_args failed!\n");
  }
  return fuse_main(*fuse_argc, fuse_argv, &ops, NULL);
}









