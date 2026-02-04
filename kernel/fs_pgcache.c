// NOTE: the eviction logic is still missing in the
// current implementation.
// if there are no entries available in the cache,
// the system would raise error.

#include "param.h"
#include "types.h"
#include "err.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fs_pgcache.h"

struct fs_pgcache_referrer referrers[ALLOC_FS_PGCACHE_REFERRER_MAX];
struct spinlock referrers_lk;

struct fs_pgcache_referrer*
fs_pgcache_referrer_alloc(){
  acquire(&referrers_lk);
  for (int i = 0; i < ALLOC_FS_PGCACHE_REFERRER_MAX; i++){
    if(referrers[i].alloc == 0){
      referrers[i].alloc = 1;
      release(&referrers_lk);
      return &referrers[i];
    }
  }
  release(&referrers_lk);
  return 0;
}

void
fs_pgcache_referrer_free(struct fs_pgcache_referrer *referrer)
{
  if(referrer->alloc != 1){
    panic("fs_pgcache_referrer_free");
  }

  acquire(&referrers_lk);
  referrer->alloc = 0;
  referrer->vpgaddr = 0;
  referrer->pid = 0;
  referrer->next = 0;
  release(&referrers_lk);
}

struct fs_pgcache fs_pgcache;
struct spinlock fs_pgcache_lk;

void
fs_pgcache_ent_init(struct fs_pgcache_ent* ent)
{
  ent->referrers = 0;
  ent->inum = 0;
  ent->foffset = 0;
}

void
fs_pgcache_init()
{
  initlock(&fs_pgcache_lk, "fs_pgcache");
  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent = &fs_pgcache.entries[i];
    fs_pgcache_ent_init(ent);
  }
}

// referrer is added to the head of the tracking list.
// if the referrer (refpid, ref_vpgaddr) already exists,
// return immediately.
int
fs_pgcache_ent_add_referrer(struct fs_pgcache_ent *ent, int refpid, uint64 ref_vpgaddr)
{
  for(struct fs_pgcache_referrer *it = ent->referrers; it != 0; it = it->next){
    if(it->vpgaddr == ref_vpgaddr && it->pid == refpid){
      return 0;
    }
  }

  struct fs_pgcache_referrer *referrer = fs_pgcache_referrer_alloc();
  if(referrer == 0){
    printf("debug: fs_pgcache_ent_add_referrer: no allocable mem\n");
    return -ENOMEM;
  }

  referrer->pid = refpid;
  referrer->vpgaddr = ref_vpgaddr;

  if(ent->referrers == 0){
    referrer->prev = 0;
    referrer->next = 0;
    ent->referrers = referrer;
  } else {
    referrer->next = ent->referrers;
    ent->referrers->prev = referrer;
    ent->referrers = referrer;
  }

  return 0;
}

// map the on-disk page to an available page in
// the page cache.
int
fs_pgcache_map(struct inode *ip, off_t foffset, int ref_pid, uint64 ref_vaddr, uint64 *ret_phypg_addr)
{
  if(ip == 0 || foffset % PGSIZE != 0 || ref_vaddr % PGSIZE != 0 || ret_phypg_addr == 0){
    printf("debug: in fs_pgcache_map: invalid parameters\n");
    return -EINVAL;
  }
 
  acquire(&fs_pgcache_lk);
  struct fs_pgcache_ent *free_ent = 0;

  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent = &fs_pgcache.entries[i];
    if(ent->alloc == 1 && ent->inum == ip->inum && ent->foffset == foffset){
      // this mmap-ed region is already in the page cache
      int err = fs_pgcache_ent_add_referrer(ent, ref_pid, ref_vaddr);
      printf("debug: fs_pgcache_map: referrer added, ref_pid=%d, ref_vaddr=%lu, inum=%d, foffset=%lu\n", ref_pid, ref_vaddr, ip->inum, foffset);
      if(err < 0){
        release(&fs_pgcache_lk);
        return err;
      }
      
      *ret_phypg_addr = ent->kpage_addr;
      release(&fs_pgcache_lk);
      return 0;
    } else if(ent->alloc == 0 && free_ent == 0){
      free_ent = ent;
    }
  }

  void* kpage = kalloc();
  if(kpage == 0){
    printf("debug: no page frame available\n");
    release(&fs_pgcache_lk);
    return -ENOMEM;
  }

  int err;

  struct fs_pgcache_ent *ent = free_ent;
  if(ent == 0){
    printf("debug: no page cache entry available\n");
    release(&fs_pgcache_lk);
    return -ENOMEM;
  }

  memset(kpage, 0, PGSIZE);
  err = readi(ip, 0, (uint64) kpage, foffset, PGSIZE);
  if(err < 0){
    printf("debug: fs_pgcache_map(): error while performing I/O.\n");
    release(&fs_pgcache_lk);
    return -1;
  }

  err = fs_pgcache_ent_add_referrer(ent, ref_pid, ref_vaddr);
  if(err == -ENOMEM){
    printf("debug: fs_pgcache_map(): error adding referrer\n");
    release(&fs_pgcache_lk);
    return err;
  }

  ent->alloc = 1;
  ent->kpage_addr = (uint64) kpage;
  ent->foffset = foffset;
  ent->inum = ip->inum;
  *ret_phypg_addr = ent->kpage_addr;

  printf("debug: fs_pgcache_map: new mapped page, ref_pid=%d, ref_vaddr=%lu, inum=%d, foffset=%lu\n", ref_pid, ref_vaddr, ip->inum, foffset);

  release(&fs_pgcache_lk);
  return 0;
}

// unmap the mmap-ed page
// Args:
// - dirty: either 0 or 1. if dirty is set, that means the mmap-ed page
// in the page cache should be written back to the disk later.
int
fs_pgcache_unmap(struct inode *ip, off_t foffset, int ref_pid, uint64 ref_vaddr, int dirty)
{
  acquire(&fs_pgcache_lk);

  // find the entry
  struct fs_pgcache_ent *ent = 0;
  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent_i = &fs_pgcache.entries[i];
    if(ent_i->alloc == 1 && ent_i->inum == ip->inum && ent_i->foffset == foffset){
      ent = ent_i;
      break;
    }
  }

  if(ent == 0){
    printf("debug: fs_pgcache_unmap: referrer not found, ref_pid=%d ref_vaddr=%lu inum=%d foffset=%lu\n", ref_pid, ref_vaddr, ip->inum, foffset);
    release(&fs_pgcache_lk);
    return 0;
  }

  struct fs_pgcache_referrer *referrer = ent->referrers;
  while(referrer != 0){
    if(referrer->pid == ref_pid && referrer->vpgaddr == ref_vaddr){
      break;
    }
    referrer = referrer->next;
  }

  if(referrer == 0){
    printf("debug: fs_pgcache_unmap: no referrer, ref_pid=%d ref_vaddr=%lu inum=%d foffset=%lu\n", ref_pid, ref_vaddr, ip->inum, foffset);
    release(&fs_pgcache_lk);
    return 0;
  }

  // remove the referrer from the referrer-tracking list
  if(referrer->prev != 0){
    referrer->prev->next = referrer->next;
  }

  if(referrer->next != 0){
    referrer->next->prev = referrer->prev;
  }

  if(ent->referrers == referrer){
    ent->referrers = referrer->next;
  }

  fs_pgcache_referrer_free(referrer);
  printf("debug: fs_pgcache_unmap: referrer removed, ref_pid=%d ref_vaddr=%lu inum=%d foffset=%lu\n", ref_pid, ref_vaddr, ip->inum, foffset);

  if(dirty){
    int wrt_size = (ip->size - foffset < PGSIZE) ? ip->size - foffset : PGSIZE;
    release(&fs_pgcache_lk);
    int err = writei(ip, 0, ent->kpage_addr, foffset, wrt_size);
    if(err < 0){
      printf("debug: error when writing page back to file");
      return -EIO;
    }
    printf("debug: fs_pgcache_unmap: wrote dirty page, inum=%d foffset=%lu\n", ip->inum, foffset);
    acquire(&fs_pgcache_lk);
  }

  if(ent->referrers == 0){
    kfree((void*) ent->kpage_addr);
    ent->kpage_addr = 0;
    ent->alloc = 0;
    ent->foffset = 0;
    ent->inum = 0;
  }

  release(&fs_pgcache_lk);
  return 0;
}
