// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define HASH(n) n % NBUFMAP_BUCKET

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.

  // 散列表，BUCKET
  struct buf bufmap[NBUFMAP_BUCKET];
  struct spinlock buflockmap[NBUFMAP_BUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for (int i = 0; i < NBUFMAP_BUCKET; i++) {
    initlock(&bcache.buflockmap[i], "");
    // Create linked list of buffers
    bcache.bufmap[i].prev = &bcache.bufmap[i];
    bcache.bufmap[i].next = &bcache.bufmap[i];
  }

  // 暂时全部放在 0 号 bucket 里面
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    int hash = HASH(b->blockno);
    b->next = bcache.bufmap[hash].next;
    b->prev = &bcache.bufmap[hash];
    initsleeplock(&b->lock, "buffer");
    bcache.bufmap[hash].next->prev = b;
    bcache.bufmap[hash].next = b;
    b->ticks = ticks;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 简单取模算个hash
  int hash_no = HASH(blockno);

  acquire(&bcache.buflockmap[hash_no]);

  // Is the block already cached?
  for(b = bcache.bufmap[hash_no].next; b != &bcache.bufmap[hash_no]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buflockmap[hash_no]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 没找到的情况下也需要将锁释放 否则会死锁
  release(&bcache.buflockmap[hash_no]);

  // 没找到就线性扫描
  // 上把大锁串行化
  acquire(&bcache.lock);

  acquire(&bcache.buflockmap[hash_no]);
  for(b = bcache.bufmap[hash_no].next; b != &bcache.bufmap[hash_no]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.buflockmap[hash_no]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.buflockmap[hash_no]);

  struct buf *min_buf = 0;
  uint min_ticks = ~0;

  for (int i = 0; i < NBUFMAP_BUCKET; i++) {
    acquire(&bcache.buflockmap[i]);
    int find = 0;
    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    for(b = bcache.bufmap[i].prev; b != &bcache.bufmap[i]; b = b->prev) {
      if(b->refcnt == 0 && b->ticks < min_ticks) {
        // 如果跟上次找到的不是同一个 bucket，释放锁
        if (min_buf != 0) {
          int last = HASH(min_buf->blockno);
          if (last != i)
            release(&bcache.buflockmap[last]);
        }
        min_ticks = b->ticks;
        min_buf = b;
        find = 1;
      }
    }

    // 没有找到就释放锁
    if (!find) 
      release(&bcache.buflockmap[i]);
  }

  if (min_buf == 0)
    panic("bget: no buffers");

  // 算出 最小 buf 的 blockno
  int minb_i = HASH(min_buf->blockno);

  min_buf->dev = dev;
  min_buf->blockno = blockno;
  min_buf->valid = 0;
  min_buf->refcnt = 1;

  if (minb_i != hash_no) {
    min_buf->prev->next = min_buf->next;
    min_buf->next->prev = min_buf->prev;
  }
  release(&bcache.buflockmap[minb_i]);

  if (minb_i != hash_no) {
    acquire(&bcache.buflockmap[hash_no]);

    min_buf->next = bcache.bufmap[hash_no].next;
    min_buf->prev = &bcache.bufmap[hash_no];
    bcache.bufmap[hash_no].next->prev = min_buf;
    bcache.bufmap[hash_no].next = min_buf;

    release(&bcache.buflockmap[hash_no]);
  }

  // 释放大锁
  release(&bcache.lock);

  acquiresleep(&min_buf->lock);

  return min_buf;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int hi = HASH(b->blockno);
  acquire(&bcache.buflockmap[hi]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->ticks = ticks;
  }
  release(&bcache.buflockmap[hi]);
}

void
bpin(struct buf *b) {
  int hi = HASH(b->blockno);
  acquire(&bcache.buflockmap[hi]);
  b->refcnt++;
  release(&bcache.buflockmap[hi]);
}

void
bunpin(struct buf *b) {
  int hi = HASH(b->blockno);
  acquire(&bcache.buflockmap[hi]);
  b->refcnt--;
  release(&bcache.buflockmap[hi]);
}


