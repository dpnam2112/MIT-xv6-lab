#ifndef VM_H
#define VM_H

// virtual memory area

#include "param.h"
#include "types.h"

struct vma {
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
  struct spinlock lk;
};

struct vma_tbl vma_tables[NPROC];

#define VMA_TBL_MMAP_E

struct vma_tbl *vma_tbl_alloc();
void vma_tbl_dealloc(struct vma_tbl*);

void vma_init(struct vma*);
struct vma *vma_alloc();
void vma_dealloc(struct vma*);

// look up the mmap region covering the given virtual address.
// used in the trap-handling logic triggered when a process tries to access
// a mmap-ed memory region.
struct vma *vma_tbl_lookup(struct vma_tbl *tbl, uint64 vaddr);

// Add a new mmap entry to the mmap table.
// used in the logic to create mmap region. 
// edge cases:
// - what if the file size <= foffset? i.e., if the mapping
// is allowed, it would create 'slack space'. For this case,
// this function should fill in the slack spaces with zeros.
// Args:
// - vaddr: starting virtual address of the area to be mapped
// - len: length of the mapping
// - inum: i-node number of the file where the mapping resides
// - foff: offset of the mapping in the file.
// Returns:
// - 0 if the op is successful. Otherwise, a negative integer is returned.
int vma_tbl_mmap_add(struct vma_tbl *tbl, uint64 vstart, uint64 len, struct inode*, int foff, int mmap_flags, int mmap_prot);

// used in munmap
// edge case: partial ummap, e.g., unmap a 4-KiB region in a 16 kiB mmap region.
int vma_tbl_mmap_rm(struct vma_tbl *tbl, uint64 vaddr, int len);
#endif
