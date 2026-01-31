#include "types.h"
#include "fs.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vma.h"
#include "stat.h"
#include "sleeplock.h"
#include "err.h"
#include "file.h"
#include "fcntl.h"

// Prototype: void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
uint64
sys_mmap(void)
{
  uint64 uvaddr;
  uint len;
  int prot;
  int flags;
  uint offset;
  int fd;

  argaddr(0, &uvaddr);
  argint(1, (int*) &len);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argint(5, (int*) &offset);

  if(uvaddr != 0 || prot == 0 || offset % PGSIZE != 0){
    // uvaddr == 0 is not supported.
    // in real-world implementations, the kernel should
    // take this argument as a hint to place the mapping
    // in the virtual address space.
    return -EINVAL;
  }

  // check if the mapping is overlapped with an existing mapping.
  // if yes, return EEXIST.
  struct proc *p = myproc();
  struct file *f = p->ofile[fd];
  if(f == 0 || f->type != T_FILE || f->ip == 0){
    return -EINVAL;
  }

  struct inode *ip = idup(f->ip);

  uint64 vstart = PGROUNDUP(p->sz);
  // ensure that there is no other region occupied
  for(uint64 pgaddr = vstart; pgaddr < vstart + len; pgaddr = pgaddr + PGSIZE){
    pte_t *pte = walk(p->pagetable, pgaddr, 1);
    if(pte == 0){
      return -ENOMEM;
    }

    if(*pte & PTE_V){
      // this region is already occupied
      return -EINVAL;
    }

    uint64 pa = 0; // dummy physical address

    // disable read and write permissions, data will be loaded
    // into the memory when page fault occurs.
    int pte_flags = PTE_V | PTE_U | PTE_MMAP;
    *pte = PA2PTE(pa) | pte_flags;
  }

  int err = vma_tbl_mmap_add(p->vma_tbl, vstart, len, ip, offset, flags, prot);
  if(err < 0){
    return err;
  }

  p->sz = vstart + len;
  return 0;
}

uint64
sys_munmap(void)
{
  // munmap should synchronize the mmap-ed region
  // in the file with its in memory counterpart
  // to pass this lab.
  
  uint64 vstart;
  int len;

  argint(0, (int*) &vstart);
  argint(1, &len);

  if(vstart % PGSIZE != 0 || len < 0){
    return -EINVAL;
  }

  struct proc *p = myproc();
  struct vma *vma = vma_tbl_mmap_rm(p->vma_tbl, vstart, len);
  if(vma == 0 || vma->vma_type != VMA_MMAP){
    // no vma found, or something went wrong
    return -1;
  }

  int err;
  if((err = vma_mmap_eitherflush(vma)) < 0){
    return -1;
  }

  return 0;
}
