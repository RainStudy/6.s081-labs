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

struct run {
  struct run *next;
};

struct memnode {
  struct spinlock lock;
  struct run *freelist;
};

struct memnode cpu_kmem[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {;
    initlock(&cpu_kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int cpu_id = cpuid();
  
  struct memnode *kmem_cpu = &cpu_kmem[cpu_id];
  acquire(&kmem_cpu->lock);
  r->next = kmem_cpu->freelist;
  kmem_cpu->freelist = r;
  release(&kmem_cpu->lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cpu_id = cpuid();

  struct memnode *kmem_cpu = &cpu_kmem[cpu_id];
  acquire(&kmem_cpu->lock);

  // 当前 cpu freelist 为空，从其他 cpu 偷取
  if (!kmem_cpu->freelist) {
    // 一次偷 64 页
    int steal_left = 64;
    for (int i = 0; i < NCPU; i++) {
      if (i == cpu_id) continue;
      acquire(&cpu_kmem[i].lock);
      struct run *curr = cpu_kmem[i].freelist;
      while (curr && steal_left) {
        cpu_kmem[i].freelist = curr->next;
        curr->next = kmem_cpu->freelist;
        kmem_cpu->freelist = curr;
        curr = cpu_kmem[i].freelist;
        steal_left--;
      }
      release(&cpu_kmem[i].lock);
      // 偷取完成就不再继续偷取了
      if (steal_left == 0) break; 
    }
  }

  r = kmem_cpu->freelist;
  if(r)
    kmem_cpu->freelist = r->next;
  
  release(&kmem_cpu->lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
