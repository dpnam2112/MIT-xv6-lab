#include "types.h"
#include "fs.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "vma.h"
#include "proc.h"
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
  off_t offset;
  int fd;

  argaddr(0, &uvaddr);
  argint(1, (int*) &len);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argaddr(5, (uint64*) &offset);


  if(uvaddr != 0 || prot == 0 || offset % PGSIZE != 0){
    // uvaddr == 0 is not supported.
    // in real-world implementations, the kernel should
    // take this argument as a hint to place the mapping
    // in the virtual address space.
    return -EINVAL;
  }

  // check if the mapping is overlapped with an existing mapping.
  struct proc *p = myproc();
  struct file *f = p->ofile[fd];
  if(f == 0 || f->type != T_FILE || f->ip == 0){
    printf("debug(mmap): invalid file\n");
    return -1;
  }

  if(flags & MAP_SHARED && ((f->readable == 0 && prot & PROT_READ) || (f->writable == 0 && prot & PROT_WRITE))){
    // if MAP_SHARED is set, prot config should respect read/write permissions of the file
    return -1;
  }


  uint64 vstart = PGROUNDUP(p->sz);
//  printf("debug: sys_mmap: pid=%d vstart=%lu len=%d inum=%d foffset=%lu\n", p->pid, vstart, len, f->ip->inum, offset);

  // ensure that there is no other region occupied
  for(uint64 pgaddr = vstart; pgaddr < vstart + len; pgaddr = pgaddr + PGSIZE){
    pte_t *pte = walk(p->pagetable, pgaddr, 1);

    uint64 pa = 0; // dummy physical address
    if(PTE2PA(*pte) != 0 || (*pte & 0x3ff)){
      // already in use
      // we don't count PTE_V being set here,
      // since walk always allocate a valid PTE,
      // in the case it is told to.
      return -1;
    }

    // disable read and write permissions, data will be loaded
    // into the memory when page fault occurs.
    int pte_flags = PTE_U | PTE_V | PTE_MMAP;
    *pte = PA2PTE(pa) | pte_flags;
  }

  struct inode *ip = idup(f->ip);
  int err = vma_tbl_mmap_add(&p->vma_tbl, vstart, len, ip, offset, flags, prot);
  if(err < 0){
    iput(ip);
    return err;
  }
  p->sz = vstart + len;
  return vstart;
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
//  printf("debug: sys_munmap: pid=%d vstart=%lu len=%d\n", p->pid, vstart, len);
  struct vma *vma = vma_tbl_mmap_rm(&p->vma_tbl, vstart, len);
  if(vma == 0 || vma->vma_type != VMA_MMAP){
    // no vma found, or something went wrong
    return -1;
  }

  int err;
  begin_op();
  if((err = vma_mmap_freepages(vma)) < 0){
    printf("debug: error freeing pages\n");
    end_op();
    return -1;
  }

  vma_free(vma);
  end_op();
  return 0;
}
