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
#include "fs_pgcache.h"
#include "file.h"

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
  ent->dirty = 0;
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
  for(struct fs_pgcache_referrer *it = ent->referrers; it != 0; it = it->prev){
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
fs_pgcache_map(struct inode *ip, int foffset, int ref_pid, int ref_vaddr, int cause, uint64 *ret_phypg_addr)
{
  if(ip == 0 || foffset % PGSIZE != 0 || ref_vaddr % PGSIZE != 0 || ret_phypg_addr == 0){
    printf("debug: in fs_pgcache_map: invalid parameters\n");
    return -EINVAL;
  }

  acquire(&fs_pgcache_lk);
  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent = &fs_pgcache.entries[i];
    if(ent->inum == ip->inum && ent->foffset == foffset){
      // this mmap-ed region is already in the page cache
      int err = fs_pgcache_ent_add_referrer(ent, ref_pid, ref_vaddr);
      if(err < 0){
        release(&fs_pgcache_lk);
        return err;
      }
      
      *ret_phypg_addr = ent->kpage_addr;
      release(&fs_pgcache_lk);
      return 0;
    }
  }

  void* kpage = kalloc();
  if(kpage == 0){
    printf("debug: no page frame available\n");
    release(&fs_pgcache_lk);
    return -ENOMEM;
  }

  struct fs_pgcache_ent *ent = 0;
  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent_i = &fs_pgcache.entries[i];
    if(ent_i->alloc == 0){
      ent = ent_i;
    }
  }
  if(ent == 0){
    printf("debug: no page cache entry available\n");
    release(&fs_pgcache_lk);
    return -ENOMEM;
  }

  ent->alloc = 1;
  ent->kpage_addr = (uint64) kpage;
  ent->foffset = foffset;
  ent->inum = ip->inum;
  ent->referrers = 0;

  int err = fs_pgcache_ent_add_referrer(ent, ref_pid, ref_vaddr);
  if(err == -ENOMEM){
    release(&fs_pgcache_lk);
    return err;
  }

  release(&fs_pgcache_lk);
  return 0;
}

// unmap the mmap-ed page
// Args:
// - dirty: either 0 or 1. if dirty is set, that means the mmap-ed page
// in the page cache should be written back to the disk later.
int
fs_pgcache_unmap(struct inode *ip, int foffset, int ref_pid, int ref_vaddr, int dirty)
{
  acquire(&fs_pgcache_lk);

  // find the entry
  struct fs_pgcache_ent *ent = 0;
  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent_i = &fs_pgcache.entries[i];
    if(ent_i->inum == ip->inum && ent_i->foffset == foffset){
      ent = ent_i;
    }
  }

  if(ent == 0){
    release(&fs_pgcache_lk);
    return -ENOENT;
  }

  struct fs_pgcache_referrer *referrer = ent->referrers;
  while(referrer != 0){
    if(referrer->pid == ref_pid && referrer->vpgaddr == ref_vaddr){
      break;
    }
    referrer = referrer->prev;
  }

  if(referrer == 0){
    release(&fs_pgcache_lk);
    return -ENOENT;
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

  if(dirty){
    ent->dirty = dirty;
  }

  if(ent->referrers == 0 && ent->dirty){
    int err = writei(ip, 0, ent->kpage_addr, foffset, PGSIZE);
    if(err < 0){
      printf("debug: error when writing page back to file");
      release(&fs_pgcache_lk);
      return -EIO;
    }
  }

  fs_pgcache_referrer_free(referrer);
  release(&fs_pgcache_lk);
  return 0;
}
