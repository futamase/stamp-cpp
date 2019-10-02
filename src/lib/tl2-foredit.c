#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include "platform.h"
#include "tl2.h"
#include "tmalloc.h"
#include "util.h"

static void txSterilize (void*, size_t);
__INLINE__ long ReadSetCoherent (Thread*);
__INLINE__ long ReadSetCoherentPessimistic (Thread*);
enum tl2_config {
    TL2_INIT_WRSET_NUM_ENTRY = 1024,
    TL2_INIT_RDSET_NUM_ENTRY = 8192,
    TL2_INIT_LOCAL_NUM_ENTRY = 1024,
};
typedef enum {
    TIDLE       = 0, /* Non-transactional */
    TTXN        = 1, /* Transactional mode */
    TABORTING   = 3, /* aborting - abort pending */
    TABORTED    = 5, /* defunct - moribund txn */
    TCOMMITTING = 7,
} Modes;
typedef enum {
    LOCKBIT  = 1,
    NADA
} ManifestContants;

typedef int            BitMap;
typedef unsigned char  byte;

typedef struct _AVPair {
    struct _AVPair* Next;
    struct _AVPair* Prev;
    volatile intptr_t* Addr;
    intptr_t Valu;
    volatile vwLock* LockFor; /* points to the vwLock covering Addr */
    vwLock rdv;               /* read-version @ time of 1st read - observed */
    struct _Thread* Owner;
    long Ordinal;
} AVPair;

typedef struct _Log {
    AVPair* List;
    AVPair* put;        /* Insert position - cursor */
    AVPair* tail;       /* CCM: Pointer to last valid entry */
    AVPair* end;        /* CCM: Pointer to last entry */
    long ovf;           /* Overflow - request to grow */
    BitMap BloomFilter; /* Address exclusion fast-path test */
} Log;

struct _Thread {
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

static pthread_key_t    global_key_self;
static struct sigaction global_act_oldsigbus;
static struct sigaction global_act_oldsigsegv;

#define TL2_USE_AFTER_FREE_MARKER       (-1)

#ifndef TL2_CACHE_LINE_SIZE
#  define TL2_CACHE_LINE_SIZE           (64)
#endif

__INLINE__ unsigned long long
MarsagliaXORV (unsigned long long x)
{
    if (x == 0) {
        x = 1;
    }
    x ^= x << 6;
    x ^= x >> 21;
    x ^= x << 7;
    return x;
}

__INLINE__ unsigned long long
MarsagliaXOR (unsigned long long* seed)
{
    unsigned long long x = MarsagliaXORV(*seed);
    *seed = x;
    return x;
}

__INLINE__ unsigned long long
TSRandom (Thread* Self)
{
    return MarsagliaXOR(&Self->rng);
}

__INLINE__ intptr_t
AtomicAdd (volatile intptr_t* addr, intptr_t dx)
{
    intptr_t v;
    for (v = *addr; CAS(addr, v, v+dx) != v; v = *addr) {}
    return (v+dx);
}
__INLINE__ intptr_t
AtomicIncrement (volatile intptr_t* addr)
{
    intptr_t v;
    for (v = *addr; CAS(addr, v, v+1) != v; v = *addr) {}
    return (v+1);
}
#  define _TABSZ  (1<< 20)
static volatile vwLock LockTab[_TABSZ];
static volatile vwLock GClock[TL2_CACHE_LINE_SIZE];
#define _GCLOCK  GClock[32]

__INLINE__ void
GVInit ()
{
    _GCLOCK = 0;
}
__INLINE__ vwLock
GVRead (Thread* Self)
{
    return _GCLOCK;
}
vwLock
GVGenerateWV_GV4 (Thread* Self, vwLock maxv)
{
	if (maxv == 0) {
		maxv = Self->maxv;
	}
    vwLock gv = _GCLOCK;
    vwLock wv = gv + 2;
    vwLock k = CAS(&_GCLOCK, gv, wv);
    if (k != gv) {
        wv = k;
    }
    Self->wv = wv;
    Self->maxv = wv;
    return wv;
}
__INLINE__ long
GVAbort (Thread* Self)
{
    return 0; /* normal interference */
}
volatile long StartTally         = 0;
volatile long AbortTally         = 0;
volatile long ReadOverflowTally  = 0;
volatile long WriteOverflowTally = 0;
volatile long LocalOverflowTally = 0;
#define TL2_TALLY_MAX          (((unsigned long)(-1)) >> 1)

#define LDLOCK(a)                     *(a)     /* for PS */
#define FILTERHASH(a)                   ((UNS(a) >> 2) ^ (UNS(a) >> 5))
#define FILTERBITS(a)                   (1 << (FILTERHASH(a) & 0x1F))
#define TABMSK                        (_TABSZ-1)
#define COLOR                           (128)
#define PSSHIFT                         ((sizeof(void*) == 4) ? 2 : 3)
#define PSLOCK(a) (LockTab + (((UNS(a)+COLOR) >> PSSHIFT) & TABMSK)) /* PS1M */
volatile vwLock*
pslock (volatile intptr_t* Addr)
{
    return PSLOCK(Addr);
}
__INLINE__ AVPair* MakeList (long sz, Thread* Self) {
    AVPair* ap = (AVPair*) malloc((sizeof(*ap) * sz) + TL2_CACHE_LINE_SIZE);
    assert(ap);
    memset(ap, 0, sizeof(*ap) * sz);
    AVPair* List = ap;
    AVPair* Tail = NULL;
    long i;
    for (i = 0; i < sz; i++) {
        AVPair* e = ap++;
        e->Next    = ap;
        e->Prev    = Tail;
        e->Owner   = Self;
        e->Ordinal = i;
        Tail = e;
    }
    Tail->Next = NULL;

    return List;
}
 void FreeList (Log*, long) __attribute__ ((noinline));
/*__INLINE__*/ void
FreeList (Log* k, long sz) {
    /* Free appended overflow entries first */
    AVPair* e = k->end;
    if (e != NULL) {
        while (e->Ordinal >= sz) {
            AVPair* tmp = e;
            e = e->Prev;
            free(tmp);
        }
    }

    /* Free continguous beginning */
    free(k->List);
}
__INLINE__ AVPair*
ExtendList (AVPair* tail) {
    AVPair* e = (AVPair*)malloc(sizeof(*e));
    assert(e);
    memset(e, 0, sizeof(*e));
    tail->Next = e;
    e->Prev    = tail;
    e->Next    = NULL;
    e->Owner   = tail->Owner;
    e->Ordinal = tail->Ordinal + 1;
    /*e->Held    = 0; -- done by memset*/
    return e;
}
__INLINE__ void
WriteBackForward (Log* k)
{
    AVPair* e;
    AVPair* End = k->put;
    for (e = k->List; e != End; e = e->Next) {
        *(e->Addr) = e->Valu;
    }
}
__INLINE__ void
WriteBackReverse (Log* k)
{
    AVPair* e;
    for (e = k->tail; e != NULL; e = e->Prev) {
        *(e->Addr) = e->Valu;
    }
}
__INLINE__ AVPair*
FindFirst (Log* k, volatile vwLock* Lock)
{
    AVPair* e;
    AVPair* const End = k->put;
    for (e = k->List; e != End; e = e->Next) {
        if (e->LockFor == Lock) {
            return e;
        }
    }
    return NULL;
}

__INLINE__ AVPair* RecordStore (Log* k, 
             volatile intptr_t* Addr, intptr_t Valu,
             volatile vwLock* Lock, vwLock cv) {
    AVPair* e = k->put;
    if (e == NULL) {
        k->ovf++;
        e = ExtendList(k->tail);
        k->end = e;
    }
    ASSERT(Addr != NULL);
    k->tail    = e;
    k->put     = e->Next;
    e->Addr    = Addr;
    e->Valu    = Valu;
    e->LockFor = Lock;
    e->rdv     = cv;

    return e;
}
__INLINE__ void SaveForRollBack (Log* k, volatile intptr_t* Addr, intptr_t Valu) {
    AVPair* e = k->put;
    if (e == NULL) {
        k->ovf++;
        e = ExtendList(k->tail);
        k->end = e;
    }
    k->tail    = e;
    k->put     = e->Next;
    e->Addr    = Addr;
    e->Valu    = Valu;
    e->LockFor = NULL;
}
__INLINE__ int TrackLoad (Thread* Self, volatile vwLock* LockFor) {
    Log* k = &Self->rdSet;
    AVPair* e = k->put;
    if (e == NULL) {
        // なぜオーバーフローしたら一貫性検証するのか
        if (!ReadSetCoherentPessimistic(Self)) {
            return 0;
        }
        k->ovf++;
        e = ExtendList(k->tail);
        k->end = e;
    }
    // リード時にはAddrすら登録せず、
    // Addrのマップ先であるメタデータのアドレスを記憶しておく
    k->tail    = e;
    k->put     = e->Next;
    e->LockFor = LockFor;
    return 1;
}
static void useAfterFreeHandler (int signum, siginfo_t* siginfo, void* context)
{
    Thread* Self = (Thread*)pthread_getspecific(global_key_self);

    if (Self == NULL || Self->Mode == TIDLE) {
        psignal(signum, NULL);
        exit(siginfo->si_errno);
    }

    if (Self->Mode == TTXN) {
        if (!ReadSetCoherentPessimistic(Self)) {
            TxAbort(Self);
        }
    }

    psignal(signum, NULL);
    abort();
}
static void registerUseAfterFreeHandler ()
{
    struct sigaction act;

    memset(&act, sizeof(struct sigaction), 0);
    act.sa_sigaction = &useAfterFreeHandler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART | SA_SIGINFO;

    if (sigaction(SIGBUS, &act, &global_act_oldsigbus) != 0) {
        perror("Error: Failed to register SIGBUS handler");
        exit(1);
    }

    if (sigaction(SIGSEGV, &act, &global_act_oldsigsegv) != 0) {
        perror("Error: Failed to register SIGSEGV handler");
        exit(1);
    }
}
static void restoreUseAfterFreeHandler () {
    if (sigaction(SIGBUS, &global_act_oldsigbus, NULL) != 0) {
        perror("Error: Failed to restore SIGBUS handler");
        exit(1);
    }

    if (sigaction(SIGSEGV, &global_act_oldsigsegv, NULL) != 0) {
        perror("Error: Failed to restore SIGSEGV handler");
        exit(1);
    }
}
void TxOnce () {
    CTASSERT((_TABSZ & (_TABSZ-1)) == 0); /* must be power of 2 */

    GVInit();
    printf("TL2 system ready: GV=%s\n", _GVFLAVOR);

    pthread_key_create(&global_key_self, NULL); /* CCM: do before we register handler */
    registerUseAfterFreeHandler();

}
void TxShutdown () {
    printf("TL2 system shutdown:\n"
           "  GCLOCK=0x%lX Starts=%li Aborts=%li\n"
           "  Overflows: R=%li W=%li L=%li\n",
           (unsigned long)_GCLOCK, StartTally, AbortTally,
           ReadOverflowTally, WriteOverflowTally, LocalOverflowTally);

    pthread_key_delete(global_key_self);

    restoreUseAfterFreeHandler();

    MEMBARSTLD();
}
Thread* TxNewThread () {
    PROF_STM_NEWTHREAD_BEGIN();

    Thread* t = (Thread*)malloc(sizeof(Thread));
    assert(t);

    PROF_STM_NEWTHREAD_END();

    return t;
}
void TxFreeThread (Thread* t) {
    AtomicAdd((volatile intptr_t*)((void*)(&ReadOverflowTally)),  t->rdSet.ovf);

    long wrSetOvf = 0;
    Log* wr;
    wr = &t->wrSet;
    {
        wrSetOvf += wr->ovf;
    }
    AtomicAdd((volatile intptr_t*)((void*)(&WriteOverflowTally)), wrSetOvf);

    AtomicAdd((volatile intptr_t*)((void*)(&LocalOverflowTally)), t->LocalUndo.ovf);

    AtomicAdd((volatile intptr_t*)((void*)(&StartTally)),         t->Starts);
    AtomicAdd((volatile intptr_t*)((void*)(&AbortTally)),         t->Aborts);

    tmalloc_free(t->allocPtr);
    tmalloc_free(t->freePtr);

    FreeList(&(t->rdSet),     TL2_INIT_RDSET_NUM_ENTRY);
    FreeList(&(t->wrSet),     TL2_INIT_WRSET_NUM_ENTRY);
    FreeList(&(t->LocalUndo), TL2_INIT_LOCAL_NUM_ENTRY);

    free(t);
}
void TxInitThread (Thread* t, long id) {
    /* CCM: so we can access TL2's thread metadata in signal handlers */
    pthread_setspecific(global_key_self, (void*)t);

    memset(t, 0, sizeof(*t));     /* Default value for most members */

    t->UniqID = id;
    t->rng = id + 1;
    t->xorrng[0] = t->rng;

    t->wrSet.List = MakeList(TL2_INIT_WRSET_NUM_ENTRY, t);
    t->wrSet.put = t->wrSet.List;

    t->rdSet.List = MakeList(TL2_INIT_RDSET_NUM_ENTRY, t);
    t->rdSet.put = t->rdSet.List;

    t->LocalUndo.List = MakeList(TL2_INIT_LOCAL_NUM_ENTRY, t);
    t->LocalUndo.put = t->LocalUndo.List;

    t->allocPtr = tmalloc_alloc(1);
    assert(t->allocPtr);
    t->freePtr = tmalloc_alloc(1);
    assert(t->freePtr);

    t->tmpLockEntry.Owner = t;
}
__INLINE__ void txReset (Thread* Self) {
    Self->Mode = TIDLE;

    Self->wrSet.put = Self->wrSet.List;
    Self->wrSet.tail = NULL;

    Self->wrSet.BloomFilter = 0;
    Self->rdSet.put = Self->rdSet.List;
    Self->rdSet.tail = NULL;

    Self->LocalUndo.put = Self->LocalUndo.List;
    Self->LocalUndo.tail = NULL;
    Self->HoldsLocks = 0;

    Self->maxv = 0;
}
__INLINE__ void txCommitReset (Thread* Self) {
    txReset(Self);
    Self->Retries = 0;
}
__INLINE__ Thread* OwnerOf (vwLock v) {
    return ((v & LOCKBIT) ? (((AVPair*) (v ^ LOCKBIT))->Owner) : NULL);
}
__INLINE__ long ReadSetCoherent (Thread* Self) {
    intptr_t dx = 0;
    vwLock rv = Self->rv;
    Log* const rd = &Self->rdSet;
    AVPair* const EndOfList = rd->put;
    AVPair* e;

    ASSERT((rv & LOCKBIT) == 0);

    for (e = rd->List; e != EndOfList; e = e->Next) {
        ASSERT(e->LockFor != NULL);
        vwLock v = LDLOCK(e->LockFor);
        if (v & LOCKBIT) {
            AVPair* p = (AVPair*)(v & ~LOCKBIT);
            if (p->Owner == Self) {
                if (p->rdv > rv) {
                    return 0; /* someone wrote after we read (and wrote) this */
                }
            } else {
                return 0; /* someone else has locked (and written) this */
            }
        } else {
            dx |= (v > rv);
        }
    }

    return (dx == 0);
}
__INLINE__ long ReadSetCoherentPessimistic (Thread* Self) {
    vwLock rv = Self->rv;
    Log* const rd = &Self->rdSet;
    AVPair* const EndOfList = rd->put;
    AVPair* e;

    ASSERT((rv & LOCKBIT) == 0);

    for (e = rd->List; e != EndOfList; e = e->Next) {
        ASSERT(e->LockFor != NULL);
        vwLock v = LDLOCK(e->LockFor);
        if (v & LOCKBIT) {
            AVPair* p = (AVPair*)(v & ~LOCKBIT);
            if (p->Owner == Self) {
                if (p->rdv > rv) {
                    return 0; /* someone wrote after we read (and wrote) this */
                }
            } else {
                // 誰かが書き込んだからアウト
                return 0; 
            }
        } else {
            if (v > rv) {
               return 0;
            }
        }
    }

    return 1;
}
__INLINE__ void RestoreLocks (Thread* Self) {
    Log* wr = &Self->wrSet;
    {
        AVPair* p;
        AVPair* const End = wr->put;
        for (p = wr->List; p != End; p = p->Next) {
            // ASSERT(p->Addr != NULL);
            // ASSERT(p->LockFor != NULL);
            // ASSERT(OwnerOf(*(p->LockFor)) == Self);
            // ASSERT(*(p->LockFor) == (UNS(p)|LOCKBIT));
            // ASSERT((p->rdv & LOCKBIT) == 0);
            *(p->LockFor) = p->rdv;
        }
    }
}
__INLINE__ void DropLocks (Thread* Self, vwLock wv) {
    Log* wr = &Self->wrSet;
    {
        AVPair* p;
        AVPair* const End = wr->put;
        for (p = wr->List; p != End; p = p->Next) {
            // ASSERT(p->Addr != NULL);
            // ASSERT(p->LockFor != NULL);
            // ASSERT(wv > p->rdv);
            // ASSERT(OwnerOf(*(p->LockFor)) == Self);
            // ASSERT(*(p->LockFor) == (UNS(p)|LOCKBIT));
            *(p->LockFor) = wv;
        }
    }
}
__INLINE__ void backoff (Thread* Self, long attempt) {
    unsigned long long stall = TSRandom(Self) & 0xF;
    stall += attempt >> 2;
    stall *= 10;
    volatile typeof(stall) i = 0;
    while (i++ < stall) {
        PAUSE();
    }
}
__INLINE__ long TryFastUpdate (Thread* Self) {
    // グローバルカウンタの更新
    vwLock wv = GVGenerateWV(Self, Self->maxv);
    // 一貫性検証
    if (!ReadSetCoherent(Self)) {
        return 0;
    }
    MEMBARSTST(); /* Ensure the above stores are visible  */
    DropLocks(Self, wv); /* Release locks and increment the version */

    MEMBARSTLD();

    return 1; /* success */
}
void TxAbort (Thread* Self) {
    Self->Mode = TABORTED;

    WriteBackReverse(&Self->wrSet);
    RestoreLocks(Self);

    if (Self->LocalUndo.put != Self->LocalUndo.List) {
        WriteBackReverse(&Self->LocalUndo);
    }

    Self->Retries++;
    Self->Aborts++;

    if (GVAbort(Self)) {
        goto __rollback;
    }
    if (Self->Retries > 3) { /* TUNABLE */
        backoff(Self, Self->Retries);
    }

    __rollback:

    tmalloc_releaseAllReverse(Self->allocPtr, NULL);
    tmalloc_clear(Self->freePtr);

    PROF_STM_ABORT_END();
    SIGLONGJMP(*Self->envPtr, 1);
    ASSERT(0);
}
void TxStore (Thread* Self, volatile intptr_t* addr, intptr_t valu) {
    volatile vwLock* LockFor = PSLOCK(addr);
    vwLock cv = LDLOCK(LockFor);

    // 誰かが書いていてそれが自分
    if ((cv & LOCKBIT) && (((AVPair*)(cv ^ LOCKBIT))->Owner == Self)) {
        cv = ((AVPair*)(cv ^ LOCKBIT))->rdv; // What?
    } else {
        long c = 100; /* TUNABLE */
        AVPair* p = &(Self->tmpLockEntry);
        for (;;) {
            cv = LDLOCK(LockFor);
            // tmpLockEntryのアドレスをメタデータに記録
            if ((cv & LOCKBIT) == 0 &&
                UNS(CAS(LockFor, cv, (UNS(p)|UNS(LOCKBIT)))) == UNS(cv)) {
                break;
            }
            // だめだった
            if (--c < 0) {
                PROF_STM_WRITE_END();
                TxAbort(Self);
                ASSERT(0);
            }
        }
    }

    Log* wr = &Self->wrSet;
    AVPair* e = RecordStore(wr, addr, *addr, LockFor, cv);
    if (cv > Self->maxv) {
        Self->maxv = cv;
    }

    *LockFor = UNS(e) | UNS(LOCKBIT);

    *addr = valu;
}
intptr_t TxLoad (Thread* Self, volatile intptr_t* Addr) {
    intptr_t Valu;
    ASSERT(Self->Mode == TTXN);

    // メタデータ取得
    volatile vwLock* LockFor = PSLOCK(Addr);
    vwLock cv = LDLOCK(LockFor);
    vwLock rdv = cv & ~LOCKBIT;

    MEMBARLDLD();
    Valu = LDNF(Addr);
    MEMBARLDLD();
    // (誰も書いておらず、書こうとしていない) || (書こうとしているのが自分)
    if ((rdv <= Self->rv && LDLOCK(LockFor) == rdv) ||
        ((cv & LOCKBIT) && (((AVPair*)rdv)->Owner == Self))) {
        if (!TrackLoad(Self, LockFor)) {
            TxAbort(Self);
        }
        return Valu;
    }
    Self->abv = rdv;
    TxAbort(Self);
    ASSERT(0);
    return 0;
}
static void txSterilize (void* Base, size_t Length) {
    PROF_STM_STERILIZE_BEGIN();

    intptr_t* Addr = (intptr_t*)Base;
    intptr_t* End = Addr + Length;
    ASSERT(Addr <= End);
    while (Addr < End) {
        volatile vwLock* Lock = PSLOCK(Addr);
        intptr_t val = *Lock;
        /* CCM: invalidate future readers */
        CAS(Lock, val, (_GCLOCK & ~LOCKBIT));
        Addr++;
    }
    memset(Base, (unsigned char)TL2_USE_AFTER_FREE_MARKER, Length);

    PROF_STM_STERILIZE_END();
}
void TxStoreLocal (Thread* Self, volatile intptr_t* Addr, intptr_t Valu) {
    PROF_STM_WRITELOCAL_BEGIN();

    SaveForRollBack(&Self->LocalUndo, Addr, *Addr);
    *Addr = Valu;

    PROF_STM_WRITELOCAL_END();
}
void TxStart (Thread* Self, sigjmp_buf* envPtr, int* ROFlag) {
    // PROF_STM_START_BEGIN();

    // ASSERT(Self->Mode == TIDLE || Self->Mode == TABORTED);
    txReset(Self);

    Self->rv = GVRead(Self);
    // ASSERT((Self->rv & LOCKBIT) == 0);
    MEMBARLDLD();

    Self->Mode = TTXN;
    Self->ROFlag = ROFlag;
    Self->IsRO = ROFlag ? *ROFlag : 0;
    Self->envPtr= envPtr;

    // ASSERT(Self->LocalUndo.put == Self->LocalUndo.List);
    // ASSERT(Self->wrSet.put == Self->wrSet.List);

    Self->Starts++;

    // PROF_STM_START_END();
}
int TxCommit (Thread* Self) {
    // 一度も書き込んでない
    if (Self->wrSet.put == Self->wrSet.List) {
        txCommitReset(Self);
        tmalloc_clear(Self->allocPtr);
        tmalloc_releaseAllForward(Self->freePtr, &txSterilize);
        return 1;
    }

    if (TryFastUpdate(Self)) {
        txCommitReset(Self);
        tmalloc_clear(Self->allocPtr);
        tmalloc_releaseAllForward(Self->freePtr, &txSterilize);
        return 1;
    }

    TxAbort(Self);

    return 0;
}
void AfterCommit (Thread* Self)
{
    txCommitReset(Self);
    tmalloc_clear(Self->allocPtr);
    tmalloc_releaseAllForward(Self->freePtr, &txSterilize);
}
void* TxAlloc (Thread* Self, size_t size)
{
    void* ptr = tmalloc_reserve_tl2(size);
    if (ptr) {
        tmalloc_append(Self->allocPtr, ptr);
    }

    return ptr;
}

void *tm_calloc (size_t n, size_t size) {
    size_t numByte = (n) * (size);
    void* ptr = tmalloc_reserve_tl2(numByte);
    if (ptr) {
        memset(ptr, 0, numByte);
    }
    return ptr;
}
void
TxFree (Thread* Self, void* ptr)
{
    tmalloc_append(Self->freePtr, ptr);
    TxStore(Self, (volatile intptr_t*)ptr, 0);
}




/* =============================================================================
 *
 * End of tl2.c
 *
 * =============================================================================
 */
