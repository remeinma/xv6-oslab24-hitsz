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

// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   struct buf head;
// } bcache;

#define NBUCKETS 13
struct {
  struct spinlock total_lock; // 全局锁
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  //struct buf head;
  struct buf hashbucket[NBUCKETS]; //每个哈希队列一个linked list及一个lock
} bcache;


void
binit(void)
{
  struct buf *b;

  initlock(&bcache.total_lock, "bcache");
  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  for (int i = 0; i < NBUCKETS; i++)
  {
    initlock(&bcache.lock[i], "bcache");
    // 双向链表初始化
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }
  for (int i = 0; i < NBUF; i++)
  {
    uint h = i % NBUCKETS;
    b = &bcache.buf[i];
    b->next = bcache.hashbucket[h].next;
    b->prev = &bcache.hashbucket[h];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[h].next->prev = b;
    bcache.hashbucket[h].next = b;
  }
  
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint m = blockno % NBUCKETS;

  acquire(&bcache.lock[m]);

  // Is the block already cached?
  for(b = bcache.hashbucket[m].next; b != &bcache.hashbucket[m]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[m]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.hashbucket[m].prev; b != &bcache.hashbucket[m]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[m]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 该 哈希桶内没有空余桶
  release(&bcache.lock[m]);

  acquire(&bcache.total_lock);
  for (int i = 0; i < NBUCKETS; i++)
  {
    if(i == m)  continue;
    acquire(&bcache.lock[i]);

    // Recycle the least recently used (LRU) unused buffer.
    for(b = bcache.hashbucket[i].prev; b != &bcache.hashbucket[i]; b = b->prev){
      if(b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        //将该块从桶i中删除移至桶m中
        b->prev->next = b->next;
        b->next->prev = b->prev;

        acquire(&bcache.lock[m]);
        b->next = &bcache.hashbucket[m];
        b->prev = bcache.hashbucket[m].prev;
        bcache.hashbucket[m].prev->next = b;
        bcache.hashbucket[m].prev = b;
        release(&bcache.lock[m]);

        release(&bcache.lock[i]);
        release(&bcache.total_lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[i]);
  }

  release(&bcache.total_lock);

  panic("bget: no buffers");
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

  uint m = b->blockno % NBUCKETS;

  acquire(&bcache.lock[m]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[m].next;
    b->prev = &bcache.hashbucket[m];
    bcache.hashbucket[m].next->prev = b;
    bcache.hashbucket[m].next = b;
  }
  
  release(&bcache.lock[m]);
}

void
bpin(struct buf *b) {
  uint m = b->blockno % NBUCKETS;

  acquire(&bcache.lock[m]);
  b->refcnt++;
  release(&bcache.lock[m]);
}

void
bunpin(struct buf *b) {
  uint m = b->blockno % NBUCKETS;

  acquire(&bcache.lock[m]);
  b->refcnt--;
  release(&bcache.lock[m]);
}


