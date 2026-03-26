#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 一个素数，减少哈希冲突
#define NBUCKET 13

struct bucket {
  struct spinlock lock;
  struct buf head;   // bucket 内 buf 的双向链表哨兵节点
};

struct {
  // 只用于串行化 eviction / miss 时的“分配旧 buf 复用”流程
  struct spinlock lock;
  struct buf buf[NBUF];
  struct bucket bucket[NBUCKET];
} bcache;

extern uint ticks;

static int
hash(uint blockno)
{
  return blockno % NBUCKET;
}

static void
bucket_insert_head(struct bucket *bk, struct buf *b)
{
  b->next = bk->head.next;
  b->prev = &bk->head;
  bk->head.next->prev = b;
  bk->head.next = b;
}

static void
bucket_remove(struct buf *b)
{
  b->prev->next = b->next;
  b->next->prev = b->prev;
}

void
binit(void)
{
  struct buf *b;
  int i;

  initlock(&bcache.lock, "bcache.eviction");

  for(i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }

  // 一开始把所有 buf 挂到 bucket 0
  // 它们的 refcnt 都是 0，因此后续 miss 时可被回收复用
  for(b = bcache.buf; b < bcache.buf + NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->dev = 0;
    b->blockno = 0;
    b->valid = 0;
    b->refcnt = 0;
    b->next = 0;
    b->prev = 0;
    b->lastuse = 0;
    bucket_insert_head(&bcache.bucket[0], b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf *cand;
  uint min_lastuse;
  int h, oldh, newh;

  h = hash(blockno);

  // 1) fast path: 只锁目标 bucket，查找是否已缓存
  acquire(&bcache.bucket[h].lock);
  for(b = bcache.bucket[h].head.next; b != &bcache.bucket[h].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[h].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket[h].lock);

  // 2) miss: 串行化 eviction 流程
  acquire(&bcache.lock);

  // 3) double check
  // 因为在刚才 release(bucket[h]) 到 acquire(bcache.lock) 之间，
  // 可能别的 CPU 已经把该块插入 cache 了。
  acquire(&bcache.bucket[h].lock);
  for(b = bcache.bucket[h].head.next; b != &bcache.bucket[h].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[h].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket[h].lock);

  // 4) 找一个 refcnt==0 且 lastuse 最小的 buf
  // 注意：这里只是挑 candidate，不立即修改。
  // 选完后再去锁 candidate 所在 bucket 并二次确认。
  for(;;){
    cand = 0;
    min_lastuse = 0xffffffff;

    for(b = bcache.buf; b < bcache.buf + NBUF; b++){
      // 这里读取 refcnt/lastuse 在严格意义上是并发共享数据。
      // 但后面我们会在 candidate 所在 bucket 锁下再次确认 refcnt==0，
      // eviction 又被 bcache.lock 串行化，因此这种“先挑再确认”的写法
      // 在这个 lab 中是常见可行方案。
      if(b->refcnt == 0 && b->lastuse < min_lastuse){
        min_lastuse = b->lastuse;
        cand = b;
      }
    }

    if(cand == 0){
      release(&bcache.lock);
      panic("bget: no buffers");
    }

    oldh = hash(cand->blockno);
    acquire(&bcache.bucket[oldh].lock);

    // 二次确认：避免 candidate 在我们扫描后被别人重新引用
    if(cand->refcnt != 0){
      release(&bcache.bucket[oldh].lock);
      continue;
    }

    // cand 此时安全可复用，把它从旧 bucket 摘掉
    bucket_remove(cand);

    newh = h;
    if(newh != oldh)
      acquire(&bcache.bucket[newh].lock);

    cand->dev = dev;
    cand->blockno = blockno;
    cand->valid = 0;
    cand->refcnt = 1;

    bucket_insert_head(&bcache.bucket[newh], cand);

    if(newh != oldh)
      release(&bcache.bucket[newh].lock);
    release(&bcache.bucket[oldh].lock);
    release(&bcache.lock);

    acquiresleep(&cand->lock);
    return cand;
  }
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid){
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk. Must be locked.
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
  int h;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  h = hash(b->blockno);
  acquire(&bcache.bucket[h].lock);
  b->refcnt--;
  if(b->refcnt == 0){
    b->lastuse = ticks;
  }
  release(&bcache.bucket[h].lock);
}

void
bpin(struct buf *b)
{
  int h;

  h = hash(b->blockno);
  acquire(&bcache.bucket[h].lock);
  b->refcnt++;
  release(&bcache.bucket[h].lock);
}

void
bunpin(struct buf *b)
{
  int h;

  h = hash(b->blockno);
  acquire(&bcache.bucket[h].lock);
  b->refcnt--;
  release(&bcache.bucket[h].lock);
}

