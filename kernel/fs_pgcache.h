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

#define PGCACHE_MAXSIZE 128

struct fs_pgcache {
	struct fs_pgcache_ent entries[PGCACHE_MAXSIZE];
	uint size; // number of entries
};

struct fs_pgcache fs_pgcache;

#endif
