// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

typedef struct {
  struct run *freelist;
  struct spinlock lk;
} cpu_kmem_t;

cpu_kmem_t cpu_kmems[NCPU];

cpu_kmem_t*
get_cpu_kmem(){
  push_off();
  int cpu_id = cpuid();
  cpu_kmem_t *kmem = &cpu_kmems[cpu_id];
  pop_off();
  return kmem;
}

void
cpu_kmem_init(int cpu_i)
{
  cpu_kmem_t *cpu_kmem = &cpu_kmems[cpu_i];
  initlock(&cpu_kmem->lk, "cpu_kmem");

  uint64 pgstart_addr = PGROUNDUP((uint64) end);
  uint64 freepg_count = (PHYSTOP  - pgstart_addr) / PGSIZE;
  uint64 percpu_pgcount = freepg_count / NCPU;

  uint64 pa_start = pgstart_addr + percpu_pgcount * cpu_i * PGSIZE;
  uint64 pa_end = pgstart_addr + percpu_pgcount * (cpu_i + 1) * PGSIZE;

  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE < (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void
kinit()
{
  for (int i = 0; i < NCPU; i++){
    cpu_kmem_init(i);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  cpu_kmem_t *kmem = get_cpu_kmem();
  struct run *r;
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  acquire(&kmem->lk);
  r = (struct run*)pa;
  r->next = kmem->freelist;
  kmem->freelist = r;
  release(&kmem->lk);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int cpu_i = cpuid();
  cpu_kmem_t *kmem = &cpu_kmems[cpu_i];
  pop_off();

  acquire(&kmem->lk);
  r = kmem->freelist;
  if(r){
    kmem->freelist = r->next;
    release(&kmem->lk);
    memset((char*)r, 5, PGSIZE); // fill with junk
  }else{
    release(&kmem->lk);
    for (int i = 0; i < NCPU; i++){
      if (i == cpu_i){
        continue;
      }
      
      acquire(&cpu_kmems[i].lk);
      r = cpu_kmems[i].freelist;
      if (r == 0){
        release(&cpu_kmems[i].lk);
        continue;
      }

      cpu_kmems[i].freelist = r->next;
      release(&cpu_kmems[i].lk);
      memset((char*)r, 5, PGSIZE); // fill with junk
      break;
    }
  }
  return (void*)r;
}
