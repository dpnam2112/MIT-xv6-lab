// A simple mechanism to track referrers, which refer to a page.
struct fs_pgcache_referrer {
  int alloc;
	int pid; // process id
	uint64 vpgaddr; // address of the mmap-ed page in the process' vm space
	struct fs_pgcache_referrer *next;
  struct fs_pgcache_referrer *prev;
};

struct fs_pgcache_ent {
  int alloc;
	int inum; // i-node number
	off_t foffset; // must be a multiple of PGSIZE
  uint64 kpage_addr;
	struct fs_pgcache_referrer *referrers; // used to track logical pages
	struct spinlock lk;
};

struct fs_pgcache {
	struct fs_pgcache_ent entries[PGCACHE_MAXSIZE];
	uint size; // number of entries
};
