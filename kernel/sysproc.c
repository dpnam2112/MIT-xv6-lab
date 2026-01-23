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

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

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
    panic("mmap");
  }

  int err;
  if((err = vma_tbl_mmap(&p->vma_tbl, mmaped_vaddr, len, ip->inum, offset, flags, prot)) < 0){
    return err;
  }

  return 0;
}

uint64
sys_munmap(void)
{
  return -1;
}
