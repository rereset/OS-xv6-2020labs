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



// 定义缓冲区映射的哈希桶数量
#define NBUFMAP_BUCKET 13

// 定义缓冲区映射的哈希函数，使用 dev 和 blockno 进行混合计算得到哈希桶索引
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%NBUFMAP_BUCKET)

// 缓冲区缓存管理结构
struct {
  struct buf buf[NBUF]; // 缓冲区数组，用于存储数据
  struct spinlock eviction_lock; // 用于保护缓冲区驱逐过程的自旋锁

  // 哈希映射：从 (dev, blockno) 映射到 buf
  struct buf bufmap[NBUFMAP_BUCKET]; // 哈希表数组，存储缓冲区映射关系
  struct spinlock bufmap_locks[NBUFMAP_BUCKET]; // 每个哈希桶对应的自旋锁
} bcache;



void
binit(void)
{
  // 初始化缓冲区映射表（bufmap）
  for(int i = 0; i < NBUFMAP_BUCKET; i++) {
    initlock(&bcache.bufmap_locks[i], "bcache_bufmap"); // 初始化每个哈希桶的锁
    bcache.bufmap[i].next = 0; // 初始化每个哈希桶的链表为空
  }

  // 初始化缓冲区
  for(int i = 0; i < NBUF; i++){
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer"); // 初始化每个缓冲区的睡眠锁
    b->lastuse = 0; // 初始化最近使用时间为0（用于最近最久未使用策略）
    b->refcnt = 0; // 初始化引用计数为0（表示缓冲区未被引用）
    // 将所有缓冲区加入到bufmap[0]哈希桶的链表中，初始时所有缓冲区都放在同一个桶中
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }

  initlock(&bcache.eviction_lock, "bcache_eviction"); // 初始化缓冲区驱逐锁
}


// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 计算哈希桶的索引，用于定位缓冲区在bufmap中的位置
  uint key = BUFMAP_HASH(dev, blockno);

  // 获取哈希桶的锁，确保并发访问的线程安全
  acquire(&bcache.bufmap_locks[key]);

  // 是否已经在缓冲区中？
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++; // 增加引用计数
      release(&bcache.bufmap_locks[key]); // 释放哈希桶的锁
      acquiresleep(&b->lock); // 获取缓冲区的睡眠锁，确保互斥访问
      return b; // 返回已存在的缓冲区
    }
  }

  // 缓冲区不在缓存中。

  // 为了获取一个可重用的缓冲区，需要在所有哈希桶中搜索，这意味着需要获取所有桶的锁。
  // 但是不能在持有一个锁的情况下尝试获取所有的锁，否则可能导致死锁。

  release(&bcache.bufmap_locks[key]); // 释放当前哈希桶的锁

  // 获取缓冲区驱逐锁，确保并发驱逐操作的线程安全
  acquire(&bcache.eviction_lock);

  // 再次检查，缓冲区是否已经在缓存中？
  // 在持有驱逐锁的情况下，没有其他的驱逐操作会发生，所以没有哈希桶的链表结构会改变。
  // 因此在这里可以在不持有哈希桶锁的情况下遍历 `bcache.bufmap[key]`。
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache.bufmap_locks[key]); // 必须获取哈希桶锁，用于 `refcnt++`
      b->refcnt++; // 增加引用计数
      release(&bcache.bufmap_locks[key]); // 释放哈希桶的锁
      release(&bcache.eviction_lock); // 释放驱逐锁
      acquiresleep(&b->lock); // 获取缓冲区的睡眠锁，确保互斥访问
      return b; // 返回已存在的缓冲区
    }
  }

  // 仍未在缓存中。

  // 现在只持有驱逐锁，没有任何哈希桶的锁。因此现在可以安全地获取任何哈希桶的锁，而不会导致死锁。

  // 在所有的桶中找到最久未使用的缓冲区。
  // 结束时持有相应桶的锁。
  struct buf *before_least = 0; 
  uint holding_bucket = -1;
  for(int i = 0; i < NBUFMAP_BUCKET; i++){
    // 在获取之前，要么不持有任何锁，要么只持有当前桶之前的桶的锁。
    // 因此这里不会发生死锁。
    acquire(&bcache.bufmap_locks[i]);
    int newfound = 0; // 表示在当前桶中找到了最久未使用的缓冲区
    for(b = &bcache.bufmap[i]; b->next; b = b->next) {
      if(b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse)) {
        before_least = b;
        newfound = 1;
      }
    }
    if(!newfound) {
      release(&bcache.bufmap_locks[i]); // 释放当前桶的锁
    } else {
      if(holding_bucket != -1) release(&bcache.bufmap_locks[holding_bucket]); // 释放之前持有的桶的锁
      holding_bucket = i;
      // 继续持有这个桶的锁...
    }
  }
  if(!before_least) {
    panic("bget: no buffers"); // 如果没有可用缓冲区，发生警告
  }
  b = before_least->next;
  
  if(holding_bucket != key) {
    // 从原来的桶中移除缓冲区
    before_least->next = b->next;
    release(&bcache.bufmap_locks[holding_bucket]); // 释放之前持有的桶的锁
    // 重新哈希并将其添加到目标桶中
    acquire(&bcache.bufmap_locks[key]); // 获取目标桶的锁
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }
  
  b->dev = dev; // 设置缓冲区的设备号
  b->blockno = blockno; // 设置缓冲区的块号
  b->refcnt = 1; // 设置引用计数为1
  b->valid = 0; // 设置缓冲区数据无效
  release(&bcache.bufmap_locks[key]); // 释放哈希桶的锁
  release(&bcache.eviction_lock); // 释放驱逐锁
  acquiresleep(&b->lock); // 获取缓冲区的睡眠锁，确保互斥访问
  return b; // 返回获取的缓冲区
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

// 释放已被锁定的缓冲区并执行一些相关操作
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse"); // 如果当前线程未持有缓冲区的睡眠锁，发生恐慌

  releasesleep(&b->lock); // 释放缓冲区的睡眠锁

  uint key = BUFMAP_HASH(b->dev, b->blockno); // 计算哈希桶的索引

  acquire(&bcache.bufmap_locks[key]); // 获取哈希桶的锁
  b->refcnt--; // 减少引用计数
  if (b->refcnt == 0) {
    b->lastuse = ticks; // 如果引用计数为零，更新最后使用时间为当前时间
  }
  release(&bcache.bufmap_locks[key]); // 释放哈希桶的锁
}


// Increment the reference count of a buffer.
void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno); // 计算哈希桶的索引

  acquire(&bcache.bufmap_locks[key]); // 获取哈希桶的锁
  b->refcnt++; // 增加引用计数
  release(&bcache.bufmap_locks[key]); // 释放哈希桶的锁
}

// Decrement the reference count of a buffer.
void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno); // 计算哈希桶的索引

  acquire(&bcache.bufmap_locks[key]); // 获取哈希桶的锁
  b->refcnt--; // 减少引用计数
  release(&bcache.bufmap_locks[key]); // 释放哈希桶的锁
}



