struct vma {
  int alloc; // if the vma is already allocated and being used, alloc = 1
	uint64 vstart; // starting virtual address
	uint64 foffset; // offset of the region in the file. must be a multiple of PGSIZE.
	int vma_type; // should be VMA_MMAP
  struct inode *ip;
	int len; // size of the mmap region
	uint64 mmap_prot; // protection mode
	uint64 mmap_flags; // flags set when `mmap` is called
  struct vma *prev;
  struct vma *next;
};

// a simple implementation of vma using linked list,
// although this would be inefficient at scale.
struct vma_tbl {
	struct vma *vma_head;
};

#define VMA_MMAP 1
