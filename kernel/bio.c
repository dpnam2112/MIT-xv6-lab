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
bcache_buffer_clr(struct buf* b){
  b->dev = -1;
  b->blockno = -1;
  b->refcnt = 0;
  b->valid = 0;
  b->prev = 0;
  b->next = 0;
}

void
bcache_buffer_init(struct buf* b){
  initsleeplock(&b->lock, "buffer");
  bcache_buffer_clr(b);
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

struct buf *
bcache_bucket_pop_free_buf(bcache_bucket_t *bucket) {
  struct buf* b;
  for (b = bucket->head; b != 0; b = b->next){
    if (b->refcnt == 0){
      bcache_bucket_remove(bucket, b);
      return b;
    }
  }
  return 0;
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

bcache_bucket_t*
bcache_get_bucket(uint dev, uint blockno){
  uint buck_i = (dev + blockno) % BUCKETNUM;
  return &bcache.buckets[buck_i];
}


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
    bcache_buffer_init(b);
    
    // put the buffer to a random bucket
    bcache_bucket_t *bucket = &bcache.buckets[(b - bcache.buf) % BUCKETNUM];
    bcache_bucket_add_tail(bucket, b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  bcache_bucket_t *bucket = bcache_get_bucket(dev, blockno);
  acquire(&bucket->lock);
  for (struct buf *buf = bucket->head; buf != 0; buf = buf->next){
    if (buf->dev == dev && buf->blockno == blockno){
      buf->refcnt++;
      release(&bucket->lock);
      acquiresleep(&buf->lock);
      return buf;
    }
  }

  struct buf* buf = bcache_bucket_pop_free_buf(bucket);
  if (buf == 0){
    for (bcache_bucket_t *buckit = bcache.buckets; buckit != bcache.buckets + BUCKETNUM; buckit++){
      if (buckit == bucket){
        continue;
      }

      acquire(&buckit->lock);
      buf = bcache_bucket_pop_free_buf(buckit);
      if (buf != 0){
        buf->refcnt = 1;
        release(&buckit->lock);
        break;
      }
      release(&buckit->lock);
    }
  } else {
    buf->refcnt = 1;
  }

  if (buf == 0){
    panic("bget: no buffers");
  }

  buf->blockno = blockno;
  buf->dev = dev;
  buf->valid = 0;
  bcache_bucket_add_tail(bucket, buf);
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
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");
  releasesleep(&b->lock);
  bunpin(b);
}

void
bpin(struct buf *b) {
  bcache_bucket_t *bucket = bcache_get_bucket(b->dev, b->blockno);
  acquire(&bucket->lock);
  b->refcnt++;
  release(&bucket->lock);
}

void
bunpin(struct buf *b) {
  bcache_bucket_t *bucket = bcache_get_bucket(b->dev, b->blockno);
  acquire(&bucket->lock);
  if (b->refcnt > 0){
    b->refcnt--;
  }
  release(&bucket->lock);
}
