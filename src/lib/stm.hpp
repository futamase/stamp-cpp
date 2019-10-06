#ifndef STM_HPP
#define STM_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <list>
#include <stdexcept>

#include "TxDescriptor.hpp"
#include "tx_exception.hpp"

class STM {
    static std::vector<TxDescriptor> desc_table;
    public:
    static TxDescriptor& GetDesc(int tid) {
      auto& e = desc_table[tid];
      e.my_tid = tid;
      return e;
    }
    static void Init(int numThreads) {
      desc_table.resize(numThreads);
      for(size_t tid = 0; tid < desc_table.size(); tid++)
        desc_table[tid].my_tid = tid;
    }
    static void Exit() {
      for(size_t tid = 0; tid < desc_table.size(); tid++) {
        fprintf(stderr, "stats[tid:%ld] %s\n", tid, desc_table[tid].stats.concat_all_stats().c_str());
      }
    }
    template<typename T>
    static T Load(int tid, T* addr) {
        static_assert(std::is_scalar<T>::value == true, "stm_write cannot use non scalar type");
        auto& tx_desc = GetDesc(tid);

        if(tx_desc.open_for_read(addr)) {
          return *addr;
        } else {
          tx_desc.reset(false);
          // fprintf(stderr, "Aborted by:open_for_read... addr:%p\n", addr);
          throw inter_tx_exception("Load", tid, addr);
        }
        
        return (T)0;
    }
    template<typename T>
    static void Store(int tid, T* addr, T val) {
      auto& tx_desc = GetDesc(tid);

      if(tx_desc.open_for_write(addr, sizeof(T))) {
        std::memcpy(addr, &val, sizeof(T));
      } else {
        tx_desc.reset(false);
        throw inter_tx_exception("Store", tid, addr);
      }
    }
    static void Abort(int tid) {
      auto& tx_desc = GetDesc(tid);

      tx_desc.reset(false);
      throw inter_tx_exception("Explicit", tid);
    }
    static void* Alloc(int tid, size_t size) {
      auto& tx_desc = GetDesc(tid);
      void* ptr = tx_desc.allocations.Reserve(size);
      if(ptr) {
        tx_desc.allocations.Append(ptr);
      } else {
        tx_desc.reset(false);
        throw inter_tx_exception("Allocation", tid);
      }

      return ptr;
    }
    static void Free(int tid, void* addr) {
      auto& tx_desc = GetDesc(tid);
      tx_desc.frees.Append(addr);
      if(!tx_desc.open_for_write(addr, 0)) {
        tx_desc.reset(false);
        throw inter_tx_exception("Free", tid, addr);
      }
    }
};

#define STM_SELF threadID
#define STM_INIT(numThreads)  STM::Init(numThreads)
#define STM_EXIT()            STM::Exit()
#define STM_THREAD_ENTER()
#define STM_THREAD_EXIT() 
#define TxBegin(tid) \
  int STM_SELF = tid; \
  STM::GetDesc(STM_SELF).depth++; \
  while(true) { \
    int __retries = 0; \
    try {
#define TxCommit() \
      auto& __desc = STM::GetDesc(STM_SELF);\
      if(!--__desc.depth) { \
        VersionedWriteLock::AddrToLockVar(0, true); \
        if(!__desc.validate())  \
          throw inter_tx_exception("Validation", STM_SELF); \
        __desc.reset(true); \
      } \
      STM::GetDesc(STM_SELF).stats.commits++; \
      if(STM::GetDesc(STM_SELF).stats.commits % 100 == 0) \
      break; \
    } catch(const inter_tx_exception& err) {\
      /*...*/ \
      STM::GetDesc(STM_SELF).stats.aborts++; \
      if(err.ptr) \
        fprintf(stderr, "[tid:%d] Abort(%s) Addr : %p\n", err.tid, err.what(), err.ptr); \
      else \
        fprintf(stderr, "[tid:%d] Abort(%s)\n", err.tid, err.what()); \
    } \
    __retries++;\
  }
#define TxAbort()             STM::Abort(STM_SELF)
#define TxLoad(addr)          STM::Load(STM_SELF, addr)
#define TxStore(addr, value)  STM::Store(STM_SELF, addr, value)
#define TxAlloc(size)         STM::Alloc(STM_SELF, size)
#define TxFree(ptr)           STM::Free(STM_SELF, ptr)

// tl2/stm.h
#define STM_READ(var)               TxLoad(&(var))
#define STM_READ_P(var_addr)        TxLoad((var_addr))
#define STM_WRITE(var, val)         TxStore(&var, val)
#define STM_WRITE_P(var_addr,val)   TxLoad(var_addr, val)
// <<<<

// stamp/lib/tm.h
#define TM_ARG              STM_SELF,
#define TM_ARG_ALONE        STM_SELF
#define TM_ARGDECL          int TM_ARG
#define TM_ARGDECL_ALONE    int TM_ARG_ALONE
#define TM_CALLABLE

#define TM_STARTUP(numThread) STM_INIT(numThread)
#define TM_SHUTDOWN()         STM_EXIT() 
#define TM_THREAD_ENTER()     STM_THREAD_ENTER()
#define TM_THREAD_EXIT()      STM_THREAD_EXIT() 
#define P_MALLOC(size) malloc(size)
#define P_FREE(ptr) free(ptr)
#define TM_MALLOC(size) TxAlloc(size)
#define TM_FREE(ptr) TxFree(ptr)

#define TM_BEGIN(tid) TxBegin(tid)
#define TM_BEGIN_RO(tid) TM_BEGIN(tid)
#define TM_END()      TxCommit()
#define TM_RESTART()  TxAbort()

#define TM_SHARED_READ(var)           TxLoad(&(var))//STM_READ(&(var))
#define TM_SHARED_READ_P(var_addr)    TxLoad(&(var_addr))//STM_READ_P((var_addr))
#define TM_SHARED_READ_F(var)         TxLoad(&(var))//STM_READ_F(&(var))

#define TM_SHARED_WRITE(var, val)     TxStore(&(var), val)//STM_WRITE((var), (val))
#define TM_SHARED_WRITE_P(var, val)   TxStore(&(var), val)//STM_WRITE_P((var), (val))
#define TM_SHARED_WRITE_F(var, val)   TxStore(&(var), val)//STM_WRITE_F((var), (val))

#define TM_LOCAL_WRITE(var, val)      ({var = val; var;})//STM_LOCAL_WRITE(var, val)
#define TM_LOCAL_WRITE_P(var, val)    ({var = val; var;})//STM_LOCAL_WRITE_P(var, val)
#define TM_LOCAL_WRITE_F(var, val)    ({var = val; var;})//STM_LOCAL_WRITE_F(var, val)
// <<<<

// rbtree.c
#define TX_LDF(o, f)        TM_SHARED_READ((o)->f)

#endif