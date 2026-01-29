#include "types.h"
#include "err.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "fs_pgcache.h"
#include "file.h"


#define NREFERRER_OBJ_MAX 128

struct fs_pgcache_referrer referrers[NREFERRER_OBJ_MAX];
struct spinlock referrers_lk;

struct fs_pgcache_referrer*
fs_pgcache_referrer_alloc(){
  acquire(&referrers_lk);
  for (int i = 0; i < NREFERRER_OBJ_MAX; i++){
    if(referrers[i].used == 0){
      referrers[i].used = 1;
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
  acquire(&referrers_lk);
  referrer->used = 0;
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
  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent = &fs_pgcache.entries[i];
    fs_pgcache_ent_init(ent);
  }
}

int
fs_pgcache_ent_add_referrer(struct fs_pgcache_ent *ent, int refpid, uint64 vaddr)
{
  if(!ent->used){
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

// fs_pgcache_ld_fpage: load the PGSIZE bytes page
// locating at 'foffset' in the file identified by
// inum to the page cache.
// caller must acquire ip->lock first.
// return:
// uint64 *ret_phypg_addr* the physical address of
// the in-mem page frame in the page cache
// 0 if success, a negative integer if failure
int
fs_pgcache_map(struct inode *ip, int foffset, int ref_pid, int ref_vaddr, uint64 *ret_phypg_addr)
{
  acquire(&fs_pgcache_lk);
  if(foffset % PGSIZE != 0){
    release(&fs_pgcache_lk);
    return -EINVAL;
  }

  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent = &fs_pgcache.entries[i];
    if(ent->inum == ip->inum && ent->foffset == foffset){
      int err = fs_pgcache_ent_add_referrer(ent, ref_pid, ref_vaddr);
      if(err < 0){
        printf("debug: error adding referrer");
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

  struct fs_pgcache_ent *ent;
  for(int i = 0; i < PGCACHE_MAXSIZE; i++){
    struct fs_pgcache_ent *ent_i = &fs_pgcache.entries[i];
    if(!ent_i->used){
      ent = ent_i;
    }
  }
  if(ent == 0){
    printf("debug: no page cache available\n");
    release(&fs_pgcache_lk);
    return -ENOMEM;
  }

  initlock(&ent->lk, "fs_pgcache_ent");
  ent->used = 1;
  ent->kpage_addr = (uint64) kpage;
  ent->foffset = foffset;
  ent->inum = ip->inum;
  ent->referrers = 0;

  int err = fs_pgcache_ent_add_referrer(ent, ref_pid, ref_vaddr);
  if(err == -ENOMEM){
    release(&fs_pgcache_lk);
    printf("debug: error adding referrer");
    return err;
  }

  release(&fs_pgcache_lk);
  return 0;
}

// write back the page cache entry to the mmap-ed
// location on the disk.
// return 0 if the op is successful, a neg integer
// otherwise.
int
fs_pgcache_wrt(struct inode *ip, int foffset)
{
  acquire(&fs_pgcache_lk);
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

  if(ent->dirty){
    int err = writei(ip, 0, ent->kpage_addr, foffset, PGSIZE);
    if(err < 0){
      printf("debug: error when writing page back to file");
      release(&fs_pgcache_lk);
      return -EIO;
    }
  }

  release(&fs_pgcache_lk);
  return 0;
}

int
fs_pgcache_unmap(struct inode *ip, int foffset, int ref_pid, int ref_vaddr)
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

  // remove the referrer
  if(referrer->prev != 0){
    referrer->prev->next = referrer->next;
    if(referrer->next != 0){
      referrer->next->prev = referrer->prev;
    }
  }

  if(ent->referrers == referrer){
    ent->referrers = referrer->prev;
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
