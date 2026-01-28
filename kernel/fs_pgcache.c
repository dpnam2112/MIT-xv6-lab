#include "types.h"
#include "err.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "fs_pgcache.h"
#include "file.h"

struct fs_pgcache_referrer*
fs_pgcache_referrer_alloc();

struct fs_pgcache fs_pgcache;

int
fs_pgcache_ent_add_referrer(struct fs_pgcache_ent *ent, int refpid, uint64 vaddr)
{
  if(!ent->alloc){
    return -EINVAL;
  }

  for(struct fs_pgcache_referrer *it = ent->referrers; it != 0; it = it->prev){
    if(it->vpgaddr == vaddr && it->pid == refpid){
      return 0;
    }
  }

  struct fs_pgcache_referrer *referrer = fs_pgcache_referrer_alloc();
  if(referrer == 0){
    return -ENOMEM;
  }


  referrer->pid = refpid;
  referrer->vpgaddr = vaddr;

  if(ent->referrers == 0){
    referrer->prev = 0;
    referrer->next = 0;
  } else {
    ent->referrers->next = referrer;
    referrer->prev = ent->referrers;
    ent->referrers = referrer;
  }

  return 0;
}

void
fs_pgcache_ent_init(struct fs_pgcache_ent* ent)
{
  ent->dirty = 0;
  ent->referrers = 0;
  ent->inum = 0;
  ent->foffset = 0;
}


//
// fs_pgcache_ld_fpage: load the PGSIZE bytes page
// locating at 'foffset' in the file identified by
// inum to the page cache.
// caller must acquire ip->lock first.
// return:
// uint64 *ret_phypg_addr* the physical address of
// the in-mem page frame in the page cache
// 0 if success, a negative integer if failure
int
fs_pgcache_load(struct inode *ip, int foffset, int ref_pid, int ref_vaddr, uint64 *ret_phypg_addr)
{
  if(foffset % PGSIZE != 0){
    return -EINVAL;
  }

  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent = &fs_pgcache.entries[i];
    if(ent->inum == ip->inum && ent->foffset == foffset){
      int err = fs_pgcache_ent_add_referrer(ent, ref_pid, ref_vaddr);
      if(err < 0){
        printf("debug: error adding referrer");
        return err;
      }
      
      *ret_phypg_addr = ent->kpage_addr;
      return 0;
    }
  }

  void* kpage = kalloc();
  if(kpage == 0){
    printf("debug: no page frame available\n");
    return -ENOMEM;
  }

  struct fs_pgcache_ent *ent;
  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent_i = &fs_pgcache.entries[i];
    if(!ent_i->alloc){
      ent = ent_i;
    }
  }
  if(ent == 0){
    printf("debug: no page cache available\n");
    return -ENOMEM;
  }

  initlock(&ent->lk, "fs_pgcache_ent");
  ent->alloc = 1;
  ent->kpage_addr = (uint64) kpage;
  ent->foffset = foffset;
  ent->inum = ip->inum;
  ent->referrers = 0;

  int err = fs_pgcache_ent_add_referrer(ent, ref_pid, ref_vaddr);
  if(err == -ENOMEM){
    printf("debug: error adding referrer");
    return err;
  }

  return 0;
}

// write back the page cache entry to the mmap-ed
// location on the disk.
// return 0 if the op is successful, a neg integer
// otherwise.
int
fs_pgcache_wrt(struct inode *ip, int foffset)
{
  struct fs_pgcache_ent *ent = 0;
  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent_i = &fs_pgcache.entries[i];
    if(ent_i->inum == ip->inum && ent_i->foffset == foffset){
      ent = ent_i;
    }
  }

  if(ent == 0){
    return -ENOENT;
  } else if(!ent->alloc){
    panic("debug: inconsistent state; ent is not allocated");
  }

  if(ent->dirty){
    int err = writei(ip, 0, ent->kpage_addr, foffset, PGSIZE);
    if(err < 0){
      printf("debug: error when writing page back to file");
      return -EIO;
    }
  }

  return 0;
}
struct fs_pgcache_referrer *alloc_fs_pgcache_referrer();
void free_fs_pgcache_referrer(struct fs_pgcache_referrer*);
