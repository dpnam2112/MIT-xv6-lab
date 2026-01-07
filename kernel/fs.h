// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

#define FSMAGIC 0x10203040

#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define NINDIRECT_L1 (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT_L1)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+2];   // Data block addresses
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

// macros related to block table's operations (block table is a name
// inspired from page table :))

// TODO: test LOG2() macro
// // this simply computes log2 of n. the trick is based on the IEEE-754
// // format for floating-point numbers. Another approach is to use native 
// // CPU instruction which computes # of leading zeros.
// #define LOG2(n) ((*(int*)&(float){n}) >> 23) - 127

// the assumption is that number of entries in a blok table is always 
// a multiple of 2, so it would be easier to compute the in-level offset
// using bitmask operations.
#define BLOCKTBL_ENTRYNUM (BSIZE / sizeof(uint))
#define BLOCKTBL_LOG2_ENTRYNUM 8 // hardcoded for now. log2(1024 / 4)


// depth of a symlink is the number of symlinks encountered during
// the symlink walking process, including itself.
// e.g., for symlink refering directly to a normal file or directory, 
// depth = 1.
#define SYMLINK_MAXDEPTH 16

// error codes returned by symlink_follow
#define SYMLINK_FL_EINOTFOUND -2 // inode not found
#define SYMLINK_FL_EMAXDEPTH -3 // max depth reached
