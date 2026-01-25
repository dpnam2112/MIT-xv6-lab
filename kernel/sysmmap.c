#include "types.h"
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

// Prototype: void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
uint64
sys_mmap(void)
{
  uint64 mmaped_vaddr;
  uint len;
  int prot;
  int flags;
  uint offset;
  int fd;

  argaddr(0, &mmaped_vaddr);
  argint(1, (int*) &len);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argint(5, (int*) &offset);

  // check if the mapping is overlapped with an existing mapping.
  // if yes, return EEXIST.
  struct proc *p = myproc();
  acquire(&p->lock);

  struct file *f = p->ofile[fd];
  if(f == 0){
    release(&p->lock);
    return -EINVAL;
  }

  if(f->type != T_FILE){
    release(&p->lock);
    return -EINVAL;
  }

  struct inode *ip = f->ip;
  if(ip == 0){
    return -EINVAL;
  }

  int err;
  if((err = vma_tbl_mmap_add(&p->vma_tbl, mmaped_vaddr, len, ip->inum, offset, flags, prot)) < 0){
     release(&p->lock);
     return err;
  }

  uint64 vstart = PGROUNDUP(mmaped_vaddr);
  int pgcount = PGROUNDUP(len);

  // page frame allocation is handled when page fault occurs
  int perm = PTE_U | PTE_MMAP;
  if((err = mappages(p->pagetable, vstart, pgcount, 0, perm)) < 0){
    release(&p->lock);
    return -1;
  }

  return 0;
}

uint64
sys_munmap(void)
{
  return -1;
}
