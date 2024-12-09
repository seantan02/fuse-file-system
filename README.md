CS537 Fall 2024 Project 6 - FUSE-based Filesystem with RAID

This repository contains the implementation of a custom filesystem using FUSE (Filesystem in Userspace). The filesystem supports RAID (Redundant Array of Independent Disks) configurations and implements basic file and directory operations.

Features

Filesystem Operations:
Create files and directories.
Read and write to files.
List directory contents (ls support).
Remove files and directories.
Fetch file and directory attributes (e.g., stat support).
RAID Support:
RAID 0 (Striping): Data is distributed across multiple disks to optimize performance.
RAID 1 (Mirroring): All data and metadata are mirrored across disks for redundancy.
RAID 1v (Verified Mirroring): Reads compare data across disks, ensuring majority correctness.
Filesystem Details:
Block size: 512 bytes.
Metadata includes superblock, inodes, data block bitmaps, and data blocks.
Directory entries support fixed names with alphanumeric characters and underscores.
Project Files

mkfs.c
A utility to initialize disk images as empty filesystems. Handles RAID configuration and metadata setup.

Usage:

./mkfs -r <raid_mode> -d <disk1> -d <disk2> ... -i <num_inodes> -b <num_blocks>
Example:

./mkfs -r 1 -d disk1 -d disk2 -i 32 -b 200
wfs.c
The main FUSE-based filesystem implementation. Handles mounting the filesystem, RAID operations, and all file and directory operations.

Usage:

./wfs <disk1> <disk2> ... [FUSE options] <mount_point>
Example:

./wfs disk1 disk2 -f -s mnt
Installation

Clone the repository and navigate to its directory:
git clone <repository_url>
cd <repository_directory>
Compile the code using the provided Makefile:
make
Create disk images for testing:
./create_disk.sh disk1
./create_disk.sh disk2
Initialize the filesystem:
./mkfs -r 1 -d disk1 -d disk2 -i 32 -b 200
Mount the filesystem:
mkdir mnt
./wfs disk1 disk2 -f -s mnt
Testing

Open a second terminal to interact with the mounted filesystem:

# Create directories and files
mkdir mnt/a
echo "Hello, Filesystem!" > mnt/a/file.txt

# List directory contents
ls mnt/a

# View file contents
cat mnt/a/file.txt

# Remove files and directories
rm mnt/a/file.txt
rmdir mnt/a
Unmount the filesystem:

./umount.sh mnt
Error Handling

File/Directory Not Found: Returns -ENOENT.
File/Directory Already Exists: Returns -EEXIST.
Insufficient Disk Space: Returns -ENOSPC.
Debugging

Inspect the filesystem structure using xxd:
xxd -e -g 4 disk1 | less
Run the filesystem in debug mode:
./wfs disk1 disk2 -f -s mnt
Use gdb for debugging:
gdb --args ./wfs disk1 disk2 -f -s mnt
Author

This project was implemented by Sean Tan Siong Ann as part of the CS537 Fall 2024 course.
