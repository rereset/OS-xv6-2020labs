// 磁盘缓冲区结构体
struct buf {
  int valid;        // 数据是否已从磁盘读取？
  int disk;         // 是否磁盘“拥有”缓冲区？
  uint dev;         // 设备标识符
  uint blockno;     // 区块编号
  struct sleeplock lock;   // 睡眠锁，用于保护缓冲区访问
  uint refcnt;      // 引用计数，用于跟踪缓冲区的引用数量
  uint lastuse;     // *新添加的，用于跟踪最近最久未使用的缓冲区
  struct buf *next; // 下一个缓冲区的指针，用于构建链表
  uchar data[BSIZE]; // 存储磁盘数据的缓冲区
};

