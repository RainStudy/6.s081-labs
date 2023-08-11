// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define PG2REFIDX(_pa) ((((uint64)_pa) - KERNBASE) / PGSIZE)
#define MX_PGIDX PG2REFIDX(PHYSTOP)
#define PG_REFCNT(_pa) pg_refcnt[PG2REFIDX((_pa))]

int pg_refcnt[MX_PGIDX];
struct spinlock refcnt_lock;

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refcnt_lock, "ref cnt");
  freerange(end, (void*)PHYSTOP);
  // 填充一下，防止受垃圾数据影响
  memset(pg_refcnt, MX_PGIDX, 0);
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

  // 这里不专门给他加个锁的话会出并发问题
  acquire(&refcnt_lock);
  if (--PG_REFCNT(pa) <= 0) {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&refcnt_lock);
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
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    // 设置 ref cnt 为 1
    PG_REFCNT(r) = 1;
  }
    
  return (void*)r;
}

int cow_alloc(pagetable_t pgtbl, uint64 va) {
    va = PGROUNDDOWN(va);
    if (va > MAXVA) return -1;
    pte_t *pte = walk(pgtbl, va, 0);
    if (pte == 0) return -1;
    // 去掉标志
    *pte &= (~PTE_C);
    // 把 PTE_W 写回
    *pte |= PTE_W;
    // copy 一份
    char* mem = kalloc();
    if (mem == 0) return -1;
    uint64 pa = PTE2PA(*pte);
    if (pa == 0) return -1;
    memmove(mem, (char*) pa, PGSIZE);
    // 拿到 flag
    uint flags = PTE_FLAGS(*pte);
    // 取消原本父进程的映射
    // 这里会直接把 pa 释放掉，那父进程就会出问题，所以要引入 ref count
    uvmunmap(pgtbl, va, 1, 1);
    // 重新映射
    if (mappages(pgtbl, va, PGSIZE, (uint64) mem, flags) != 0) {
        printf("copy on write failed");
        kfree(mem);
        return -1;
    }
    return 0;
}

int uncopied_cow(pagetable_t pgtbl, uint64 va) {
  if(va >= MAXVA) 
    return 0;
  pte_t* pte = walk(pgtbl, va, 0);
  if(pte == 0)             // 如果这个页不存在
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return ((*pte) & PTE_C); // 有 PTE_C 的代表还没复制过，并且是 cow 页
}


void refcnt_inc(void* pa){
  acquire(&refcnt_lock);
  PG_REFCNT(pa)++;
  release(&refcnt_lock);
} 