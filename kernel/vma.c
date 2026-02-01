#include "param.h"
#include "types.h"
#include "spinlock.h"
#include "err.h"
#include "riscv.h"
#include "defs.h"
#include "vma.h"
#include "proc.h"
#include "fcntl.h"


static struct vma vmas[ALLOC_VMA_MAX];
static struct spinlock vmas_lk;


// this function is intended to be called
// during the OS initialization process.
void
vmainit()
{
  initlock(&vmas_lk, "vma");
  for(int i = 0; i < ALLOC_VMA_MAX; i++)
  {
    struct vma *vma = &vmas[i];
    vma->alloc = 0;
  }
}

/** compare two virtual memory areas.
 * lhs, rhs stand for left-hand side, right-hand side, respectively
 * return 0 if two areas are overlapped to each other
 * */
static int
overlap_range_cmp(uint64 lhs_addr, int lhs_len, uint64 rhs_addr, int rhs_len)
{
  int lhs_hi = lhs_addr + lhs_len;
  int rhs_hi = rhs_addr + rhs_len;

  if(rhs_addr >= lhs_addr && rhs_hi <= lhs_hi){
    return 0;
  }

  if((lhs_addr >= rhs_addr && lhs_addr < rhs_hi) || (lhs_hi > rhs_addr && lhs_hi <= rhs_hi)){
    return 0;
  }


  // not-overlapped cases

  if(lhs_addr > rhs_hi){
    // interpretation: lhs > rhs => lhs - rhs > 0 => a positive int
    return 1;
  }

  return -1;
}

void
vma_tbl_init(struct vma_tbl *tbl)
{
  tbl->vma_head = 0;
}

int
vma_tbl_mmap_add(struct vma_tbl *tbl, uint64 vstart, uint64 len, struct inode *ip, int foff, int mmap_flags, int mmap_prot)
{
  for (struct vma *it = tbl->vma_head; it != 0; it = it->next){
    int cmp = overlap_range_cmp(vstart, len, it->vstart, it->len);
    if(cmp == 0){
      return -EEXIST;
    }
  }

  struct vma *new = vma_alloc();
  if(new == 0){
    return -ENOMEM;
  }

  new->vstart = vstart;
  new->len = len;
  new->ip = ip;
  new->foffset = foff;
  new->mmap_flags = mmap_flags;
  new->mmap_prot = mmap_prot;
  new->vma_type = VMA_MMAP;


  if(tbl->vma_head == 0){
    tbl->vma_head = new;
    new->prev = 0;
    new->next = 0;
  } else {
    new->next = tbl->vma_head;
    tbl->vma_head->prev = new;
    tbl->vma_head = new;
  }
  return 0;
}

// remove the vma from the vma tree
// if the to-be-removed vma is a subset of another existing
// vma, that vma should be splitted into two pieces.
//
// return the vma object representing the removed vma.
// the caller is responsible for managing its lifetime.
struct vma *
vma_tbl_mmap_rm(struct vma_tbl *tbl, uint64 vstart, int len)
{
  struct vma *split_target = 0; // vma to be splitted
  struct vma *it = tbl->vma_head;

  while(it != 0){
    int cmp = overlap_range_cmp(it->vstart, it->len, vstart, len);
    if(cmp == 0 && (vstart >= it->vstart && vstart + len <= it->vstart + it->len)){
      split_target = it;
      break;
    }
    it = it->next;
  }

  if(split_target == 0){
    return 0;
  }

  if(vstart == split_target->vstart && len < split_target->len){
    split_target->len = len;
    return 0;
  }

  if(vstart > split_target->vstart && vstart + len == split_target->vstart + split_target->len){
    split_target->vstart = vstart;
    return 0;
  }


  if(vstart == split_target->vstart && len == split_target->len){
    if(split_target->prev != 0){
      split_target->prev->next = split_target->next;
    }

    if(split_target->next != 0){
      split_target->next->prev = split_target->prev;
    }

    if(split_target == tbl->vma_head){
      tbl->vma_head = split_target->next;
    }

    return split_target;
  }

  // split the area and add that area to the vma list
  struct vma *left_piece = vma_alloc();
  if(left_piece == 0){
    return 0;
  }

  left_piece->vstart = split_target->vstart;
  left_piece->len = vstart - split_target->vstart;
  left_piece->ip = idup(split_target->ip);
  left_piece->foffset = split_target->foffset;
  left_piece->mmap_flags = left_piece->mmap_flags;
  left_piece->mmap_prot = left_piece->mmap_prot;
  left_piece->next = tbl->vma_head;
  tbl->vma_head->prev = left_piece;
  tbl->vma_head = left_piece;

  uint64 vend = split_target->vstart + split_target->len;
  uint64 fend = split_target->foffset + split_target->len;

  split_target->vstart = vstart + len;
  split_target->len = vend - (vstart + len);
  split_target->foffset = fend - split_target->len;
  split_target->prev = left_piece;

  struct vma *ret = vma_alloc();
  if(ret != 0){
    ret->mmap_flags = split_target->mmap_flags;
    ret->mmap_prot = split_target->mmap_prot;
    ret->ip = idup(split_target->ip);
    ret->vstart = vstart;
    ret->len = len;
    ret->vma_type = VMA_MMAP;
  }
  
  return ret;
}

struct vma*
vma_tbl_lookup(struct vma_tbl *tbl, uint64 vaddr, int len)
{
  for(struct vma *v = tbl->vma_head; v != 0; v = v->next){
    if(overlap_range_cmp(v->vstart, v->len, vaddr, len) == 0){
      return v;
    }
  }

  return 0;
}

struct vma*
vma_alloc()
{
  struct vma *found = 0;
  acquire(&vmas_lk);
  for(int i = 0; i < ALLOC_VMA_MAX; i++){
    struct vma *vma = &vmas[i];
    if(vma->alloc == 0){
      found = vma;
    }
  }

  if(found != 0){
    found->alloc = 1;
    found->prev = 0;
    found->next = 0;
    found->ip = 0;
    found->len = 0;
    found->vstart = 0;
  }

  release(&vmas_lk);
  return found;
}

void
vma_free(struct vma *vma)
{
  if(vma->alloc != 1){
    panic("vma_free");
  }

  acquire(&vmas_lk);
  vma->alloc = 0;
  vma->prev = 0;
  vma->next = 0;
  vma->ip = 0;
  vma->len = 0;
  vma->vstart = 0;

  if(vma->ip){
    iput(vma->ip);
  }

  release(&vmas_lk);
}

int
vma_mmap_eitherflush(struct vma *vma)
{
  struct proc *p = myproc();

  if(vma->vma_type != VMA_MMAP){
    return -EINVAL;
  }

  if(vma->mmap_flags & MAP_SHARED){
    struct inode *ip = idup(vma->ip);
    ilock(ip);
    for(uint64 vaddr = vma->vstart; vaddr < vma->vstart + vma->len; vaddr += PGSIZE){
      pte_t *pte = walk(p->pagetable, vaddr, 0);
      if(pte == 0){
        panic("vma_mmap_eitherflush: pte doesn't exist");
      }

      int dirty = *pte & PTE_D;
      int accessed = *pte & PTE_A;

      if(!(dirty || accessed)){
        continue;
      }
      
      uint foffset = vma->vstart + (vaddr - vma->vstart);
      int err;

      // 'unmap' the page from the page cache
      // the page should be written back to the file,
      // in the case it is dirty.
      if((err = fs_pgcache_unmap(ip, foffset, p->pid, vaddr, dirty)) < 0){
        printf("debug: vma_mmap_eitherflush: fs_pgcache_unmap failed, err=%d\n", err);
        iunlockput(ip);
        return -1;
      }
    }
    iunlockput(ip);
  }

  // there is no need to flush MAP_PRIVATE vma
  return 0;
}
