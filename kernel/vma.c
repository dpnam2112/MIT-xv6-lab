#include "types.h"
#include "spinlock.h"
#include "vma.h"
#include "err.h"
#include "riscv.h"
#include "defs.h"

/** compare two virtual memory areas.
 * lhs, rhs stand for left-hand side, right-hand side, respectively
 * return 0 if two areas are overlapped to each other
 * */
static int range_cmp(uint64 lhs_addr, int left_len, uint64 rhs_addr, int rhs_len){
  int lhs_hi = lhs_addr + left_len;
  int rhs_hi = rhs_addr + rhs_len;
  if(lhs_addr >= rhs_addr || lhs_hi <= rhs_hi){
    return 0;
  }

  if(lhs_addr > rhs_hi){
    // interpretation: lhs > rhs => lhs - rhs > 0 => a positive int
    return 1;
  }

  return -1;
}

int vma_tbl_mmap_add(struct vma_tbl *tbl, uint64 vstart, uint64 len, int inum, int foff, int mmap_flags, int mmap_prot)
{
  acquire(&tbl->lk);
  struct vma *new = vma_alloc();
  if(new == 0){
    release(&tbl->lk);
    return -ENOMEM;
  }

  vma_init(new);

  new->vstart = vstart;
  new->len = len;
  new->inum = inum;
  new->foffset = foff;
  new->mmap_flags = mmap_flags;
  new->mmap_prot = mmap_prot;

  for (struct vma *it = tbl->vma_head->next; it != tbl->vma_head; it = it->next){
    int cmp = range_cmp(new->vstart, new->len, it->vstart, it->len);
    if(cmp == 0){
      release(&tbl->lk);
      return -EEXIST;
    }
  }

  if(tbl->vma_head == 0){
    tbl->vma_head = new;
    new->prev = 0;
    new->next = new;
  } else {
    new->prev = tbl->vma_head;
    new->next = tbl->vma_head->next;
    tbl->vma_head->next = new;
  }
  return 0;
}

// remove the vma from the vma tree
int vma_tbl_mmap_rm(struct vma_tbl *tbl, uint64 vstart, int len)
{
  acquire(&tbl->lk);
  struct vma *split_target = 0; // vma to be splitted
  struct vma *it = tbl->vma_head;

  do {
    int cmp = range_cmp(it->vstart, it->len, vstart, len);
    if(cmp == 0 && (vstart >= it->vstart && vstart + len <= it->vstart + it->len)){
      split_target = it;
      break;
    }
    it = it->next;
  } while(it != tbl->vma_head);

  if(split_target == 0){
    release(&tbl->lk);
    return -ENOENT;
  }

  if(vstart == split_target->vstart && len < split_target->len){
    split_target->len = len;
    release(&tbl->lk);
    return 0;
  }

  if(vstart > split_target->vstart && vstart + len == split_target->vstart + split_target->len){
    split_target->vstart = vstart;
    release(&tbl->lk);
    return 0;
  }


  if(vstart == split_target->vstart && len == split_target->len){
    split_target->prev->next = split_target->next;
    split_target->next->prev = split_target->prev;
    vma_dealloc(split_target);
    release(&tbl->lk);
    return 0;
  }

  // split the area
  struct vma *left_piece = vma_alloc();
  if(left_piece == 0){
    release(&tbl->lk);
    return -ENOMEM;
  }

  vma_init(left_piece);

  left_piece->vstart = split_target->vstart;
  left_piece->len = vstart - split_target->vstart;
  left_piece->inum = split_target->inum;
  left_piece->foffset = split_target->foffset;
  left_piece->mmap_flags = left_piece->mmap_flags;
  left_piece->mmap_prot = left_piece->mmap_prot;

  uint64 vend = split_target->vstart + split_target->len;
  uint64 fend = split_target->foffset + split_target->len;

  split_target->vstart = vstart + len;
  split_target->len = vend - (vstart + len);
  split_target->foffset = fend - split_target->len;
  split_target->prev = left_piece;
  
  left_piece->prev = tbl->vma_head;
  left_piece->next = tbl->vma_head->next;
  tbl->vma_head->next = left_piece;

  release(&tbl->lk);
  return 0;
}
