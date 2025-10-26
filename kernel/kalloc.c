// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

#define PGOFFSET(addr) ((PGROUNDDOWN((uint64) addr) - PGROUNDDOWN(KERNBASE)) >> PGSHIFT)

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  uint32 refcount[PHYPAGECOUNT];
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // kfree will decrement this value
    kmem.refcount[PGOFFSET(p)] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);

  uint64 pgoffset = PGOFFSET(pa);
  if (kmem.refcount[pgoffset] == 0){
    release(&kmem.lock);
    panic("kfree: refcount is 0");
  }
  kmem.refcount[pgoffset]--;

  if (kmem.refcount[pgoffset] == 0){
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);
    r = (struct run*)pa;
    r->next = kmem.freelist;
    kmem.freelist = r;
  }
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r == 0){
    release(&kmem.lock);
    return 0;
  }
  kmem.freelist = r->next;
  kmem.refcount[PGOFFSET(r)]++;
  release(&kmem.lock);
  memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// Add a new page reference
// Returns addr
void
kmem_addpageref(void* addr)
{
  acquire(&kmem.lock);
  uint64 offset = PGOFFSET(addr);
  if (kmem.refcount[offset] == 0)
    panic("kmem_addpageref: page not allocated");
  kmem.refcount[offset]++;
  release(&kmem.lock);
}

void*
kmem_detachpageref(void* addr)
{
  acquire(&kmem.lock);
  if (kmem.refcount[PGOFFSET(addr)] == 0){
    release(&kmem.lock);
    panic("kmem_detachpageref: detach a page that has no ref");
  }
  if (kmem.refcount[PGOFFSET(addr)] == 1){
    release(&kmem.lock);
    return addr;
  }
  struct run *r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  else{
    release(&kmem.lock);
    return 0;
  }
  void* new = r;
  kmem.refcount[PGOFFSET(new)]++;
  kmem.refcount[PGOFFSET(addr)]--;
  memmove(new, addr, PGSIZE);
  release(&kmem.lock);
  return new;
}
