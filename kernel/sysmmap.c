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
