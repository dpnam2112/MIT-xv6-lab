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

#define BUCKETNUM 17

typedef struct bcache_bucket {
  struct buf *head;
  struct buf *tail;
  struct spinlock lock;
} bcache_bucket_t;

void
bcache_bucket_init(bcache_bucket_t *bucket){
  initlock(&bucket->lock, "bcache.bucket");
  bucket->head = 0;
  bucket->tail = 0;
}

void
bcache_bucket_add_tail(bcache_bucket_t *bucket, struct buf *buf){
  if (bucket->tail == 0){
    bucket->head = buf;
    buf->prev = 0;
    bucket->tail = buf;
    buf->next = 0;
  } else {
    bucket->tail->next = buf;
    buf->prev = bucket->tail;
    bucket->tail = buf;
    bucket->tail->next = 0;
  }
}

struct buf *
bcache_bucket_pop_tail(bcache_bucket_t *bucket) {
    struct buf *buf;

    if (bucket->tail == 0) {
        return 0;
    }

    buf = bucket->tail;
    bucket->tail = buf->prev;

    if (bucket->tail == 0) {
        bucket->head = 0;
    } else {
        bucket->tail->next = 0;
    }
    buf->next = 0;
    buf->prev = 0;
    return buf;
}

struct buf *
bcache_bucket_remove(bcache_bucket_t *bucket, struct buf *buf) {
  if (buf->prev != 0) {
    buf->prev->next = buf->next;
  } else {
    bucket->head = buf->next;
  }

  // check if the removed buffer has a next node
  if (buf->next != 0) {
    buf->next->prev = buf->prev;
  } else {
    bucket->tail = buf->prev;
  }

  buf->next = 0;
  buf->prev = 0;
  return buf;
}

uint
bcache_bucket_hash(uint dev, uint blockno){
  return (dev + blockno) % BUCKETNUM;
}

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  bcache_bucket_t buckets[BUCKETNUM];
} bcache;

void
binit(void)
{
  initlock(&bcache.lock, "bcache");

  // init buckets
  for (bcache_bucket_t *bucket = bcache.buckets; bucket < bcache.buckets + BUCKETNUM; bucket++){
    bcache_bucket_init(bucket);
  }

  // init buffers
  for (struct buf *b = bcache.buf; b != bcache.buf + NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->prev = 0;
    b->next = 0;
    b->refcnt = 0;
    b->valid = 0;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  uint bucket_i = bcache_bucket_hash(dev, blockno);
  bcache_bucket_t *bucket = &bcache.buckets[bucket_i];

  acquire(&bucket->lock);
  for (struct buf *buf = bucket->head; buf != 0; buf = buf->next){
    if (buf->dev == dev && buf->blockno == blockno){
      buf->refcnt++;
      release(&bucket->lock);
      acquiresleep(&buf->lock);
      return buf;
    }
  }

  struct buf* buf;

  acquire(&bcache.lock);
  for (buf = bcache.buf; buf != bcache.buf + NBUF; buf++){
    if (buf->refcnt == 0){
      break;
    }
  }

  if (buf == bcache.buf + NBUF){
    panic("bget: no buffers");
  }

  buf->blockno = blockno;
  buf->dev = dev;
  buf->refcnt = 1;
  buf->valid = 0;
  bcache_bucket_add_tail(bucket, buf);
  release(&bcache.lock);
  release(&bucket->lock);
  acquiresleep(&buf->lock);
  return buf;

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

  uint bucket_i = bcache_bucket_hash(b->dev, b->blockno);
  bcache_bucket_t *bucket = &bcache.buckets[bucket_i];

  acquire(&bucket->lock);
  if (b->refcnt > 0)
    b->refcnt--;

  // ref count = 0 -> remove the buffer from the bucket
  if (b->refcnt == 0) {
    bcache_bucket_remove(bucket, b);
  }

  release(&bucket->lock);
}

void
bpin(struct buf *b) {
  uint bucket_i = bcache_bucket_hash(b->dev, b->blockno);
  bcache_bucket_t *bucket = &bcache.buckets[bucket_i];
  acquire(&bucket->lock);
  b->refcnt++;
  release(&bucket->lock);
}

void
bunpin(struct buf *b) {
  uint bucket_i = bcache_bucket_hash(b->dev, b->blockno);
  bcache_bucket_t *bucket = &bcache.buckets[bucket_i];
  acquire(&bucket->lock);
  b->refcnt--;
  release(&bucket->lock);
}
