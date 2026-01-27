#include "fs_pgcache.h"
//
// fs_pgcache_ld_fpage: load the PGSIZE bytes page
// locating at 'foffset' in the file identified by
// inum to the page cache.
// return:
// uint64 *ret_phypg_addr* the physical address of
// the in-mem page frame in the page cache
// 0 if success, a negative integer if failure
int
fs_pgcache_load(struct inode*, int foffset, int ref_pid, int ref_vaddr, uint64 *ret_phypg_addr);

// write back the page cache entry to the mmap-ed
// location on the disk.
// return 0 if the op is successful, a neg integer
// otherwise.
int
fs_pgcache_wrt(struct inode*, int foffset);

// flush all pages in the page cache to their
// location on disk.
int
fs_pgcache_flushall();

// allocate an available
// entry in the cache. If the cache is full, evict
// an existing entry to reserve place for the new
// one.
int
fs_pgcache_alloc_ent(struct fs_pgcache_ent **ret_pgcache_ent);

struct fs_pgcache_referrer *alloc_fs_pgcache_referrer();
void free_fs_pgcache_referrer(struct fs_pgcache_referrer*);
