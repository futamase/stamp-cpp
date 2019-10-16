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
      return desc_table[tid];
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
          tx_desc.stats.loads++;
          return *addr;
        } else {
          // fprintf(stderr, "Aborted by:open_for_read... addr:%p\n", addr);
          throw inter_tx_exception(AbortStatus::Load, tid);
        }
        
        return (T)0;
    }
    template<typename T>
    static void Store(int tid, T* addr, T val) {
      auto& tx_desc = GetDesc(tid);

      if(tx_desc.open_for_write(addr, sizeof(T))) {
        tx_desc.stats.stores++;
        std::memcpy(addr, &val, sizeof(T));
      } else {
        throw inter_tx_exception(AbortStatus::Store, tid);
      }
    }
    static void Abort(int tid) {
      throw inter_tx_exception(AbortStatus::Explicit, tid);
    }
    static void* Alloc(int tid, size_t size) {
      auto& tx_desc = GetDesc(tid);
      void* ptr = tx_desc.allocations.Reserve(size);
      if(ptr) {
        tx_desc.allocations.Append(ptr);
      } else {
        throw inter_tx_exception(AbortStatus::Allocation, tid);
      }

      return ptr;
    }
    static void Free(int tid, void* addr) {
      auto& tx_desc = GetDesc(tid);
      tx_desc.frees.Append(addr);
      if(!tx_desc.open_for_write(addr, 0)) {
        throw inter_tx_exception(AbortStatus::Free, tid);
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
    unsigned long ___loads = STM::GetDesc(STM_SELF).stats.loads; \
    unsigned long ___stores = STM::GetDesc(STM_SELF).stats.stores; \
    try {
#define TxCommit() \
      auto& __desc = STM::GetDesc(STM_SELF);\
      if((__desc.depth - 1) == 0) { \
        if(!__desc.validate())  \
          throw inter_tx_exception(AbortStatus::Validation, STM_SELF); \
        __desc.reset(true); \
        __desc.stats.commits++; \
        __desc.stats.ave_loads_per_tx = (double)(__desc.stats.loads + (__desc.stats.loads - ___loads)) / (double)__desc.stats.commits; \
        __desc.stats.ave_stores_per_tx = (double)(__desc.stats.stores + (__desc.stats.stores - ___stores)) / (double)__desc.stats.commits; \
      } \
      __desc.depth--; \
      break; \
    } catch(const inter_tx_exception& err) {\
      auto& __desc = STM::GetDesc(err.tid); \
      __desc.reset(false); \
      STM::GetDesc(STM_SELF).stats.loads = ___loads; \
      STM::GetDesc(STM_SELF).stats.stores = ___stores; \
      STM::GetDesc(STM_SELF).stats.aborts++; \
    } \
    STM::GetDesc(STM_SELF).backoff(__retries); \
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
