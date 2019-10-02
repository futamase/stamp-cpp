#ifndef TL2_EAGER_HPP
#define TL2_EAGER_HPP

#include <cstdint>
#include <cstdio>

class alignas(64) TL2TxDescriptor {
  using vwLock = uintptr_t;
  using BitMap = int;
  struct AVPair {
    volatile intptr_t* addr;
    intptr_t value;
    volatile vwLock LockFor;
    vwLock rdv;
    TL2TxDescriptor* Owner;
  };
  struct Log {
    AVPair* List;
    AVPair* put;        /* Insert position - cursor */
    AVPair* tail;       /* CCM: Pointer to last valid entry */
    AVPair* end;        /* CCM: Pointer to last entry */
    long ovf;           /* Overflow - request to grow */
    BitMap BloomFilter; /* Address exclusion fast-path test */
  };
  
  long UniqID;
  volatile long Mode;
  volatile long HoldsLocks; /* passed start of update */
  volatile long Retries;
  volatile vwLock rv;
  vwLock wv;
  vwLock abv;
  vwLock maxv;
  AVPair tmpLockEntry;
  int* ROFlag;
  int IsRO;
  long Starts;
  long Aborts; /* Tally of # of aborts */
  unsigned long long rng;
  unsigned long long xorrng [1];
  void* memCache;
  tmalloc_t* allocPtr; /* CCM: speculatively allocated */
  tmalloc_t* freePtr;  /* CCM: speculatively free'd */
  Log rdSet;
  Log wrSet;
  Log LocalUndo;
  sigjmp_buf* envPtr;
};

class STM {

  // TxOnce 
  static void Init() {
    GVInit();
    printf("Start TM\n");
  }
  // TxShutdown
  void Exit() {
    // stats
    MEMBARSTLD();
  }
};

#endif
