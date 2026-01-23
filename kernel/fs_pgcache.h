#ifndef FS_PGCACHE
#define FS_PGCACHE

#include "types.h"

// A simple mechanism to track referrers, which refer to a page.
struct fs_pgcache_referrer {
	uint pid; // process id
	uint vpgaddr; // address of the mmap-ed page in the process' vm space
	struct fs_pgcache_referrer *next;
};

struct fs_pgcache_ent {
	int inum; // i-node number
	int foffset; // must be a multiple of PGSIZE
	uint8 dirty; // whether the page is modified from its initial state
	struct fs_pgcache_referrer *referrers; // used to track logical pages (pages in the processes' vm spaces) referring to this cached page
	struct spinlock *lk;
};

#define PGCACHE_MAXSIZE 64

struct fs_pgcache {
	struct fs_pgcache_ent entries[PGCACHE_MAXSIZE];
	uint size; // number of entries
};

struct fs_pgcache fs_pgcache;

// page cache interface

// initialize the page cache (the global fs_pgcache).
void fs_pgcache_init(); 

// fs_pgcache_ld_fpage: load the PGSIZE bytes page
// locating at 'foffset' in the file identified by
// inum to the page cache.
// return:
// uint64 *ret_phypg_addr* the physical address of
// the in-mem page frame in the page cache
// 0 if success, a negative integer if failure
int fs_pgcache_load(int inum, int foffset, uint64 *ret_phypg_addr);

// write back the page cache entry to the mmap-ed
// location on the disk.
// return 0 if the op is successful, a neg integer
// otherwise.
int fs_pgcache_wrt(int inum, int foffset);

// flush all pages in the page cache to their
// location on disk.
int fs_pgcache_flushall();

// fs_pgcache_get_avail_ent: allocate an available
// entry in the cache. If the cache is full, evict
// an existing entry to reserve place for the new
// one.
int fs_pgcache_alloc_ent(struct fs_pgcache_ent **ret_pgcache_ent);

struct fs_pgcache_referrer *alloc_fs_pgcache_referrer();
void free_fs_pgcache_referrer(struct fs_pgcache_referrer*);

#endif
