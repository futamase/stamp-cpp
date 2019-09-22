#ifndef STM_HPP
#define STM_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <list>
#include <cstring>
#include <stdexcept>

#include "tmalloc.hpp"

//extern "C" {
//#include <umalloc.h>
//}

struct 


class LockVar {
    uintptr_t lock = 0;
    public:
    bool TestWriteBit() const { return (lock & 1); } 
    void SetWriteBit() { lock |= 1; }
    void ClearWriteBit() { lock &= ~1; }
    void ClearWriteBitAndIncTimestamp() { lock = (lock & ~1) + 2; }

    int Readers() const { return lock >> 1; }
    void IncReaders() { lock += 2; }
    void DecReaders() { lock -= 2; } 
    uintptr_t WriterID() const { return lock >> 1; }

    uintptr_t operator*() const { return lock; }
};
class GlobalLock {
    static const unsigned long TABLE_SIZE = 1 << 20;
    public:
    static LockVar& AddrToLockVar(void* addr) {
      static LockVar vars[TABLE_SIZE];
      int idx = (reinterpret_cast<uintptr_t>(addr) >> 3) & (TABLE_SIZE - 1);
      return vars[idx];
    }
};
static std::mutex alloc_lock;
static std::mutex meta_mutex;

struct alignas(64) TxDescriptor {
    enum class Type {
        Read, Write
    };
    struct LogEntry {
        void* addr;
        Type type;
        char value[128];
        uintptr_t old_version;
        std::size_t size;
    };
    std::unordered_map<uintptr_t, LogEntry> dataset;
    TMAllocList allocations, frees;
    unsigned depth = 0;
    int my_tid;

    TxDescriptor() : allocations(1 << 5), frees(1 << 5) {}

    bool validate() {
      bool success = true;
      for(auto& entry : dataset) {
        auto& meta = GlobalLock::AddrToLockVar(entry.second.addr);
        {
          std::lock_guard<std::mutex> guard(meta_mutex);
          if(entry.second.type == Type::Read) {
            if(*meta != entry.second.old_version) {
              std::cout << "version check failed: addr=" << entry.second.addr << " meta=" << *meta << std::endl;
              success = false;
            }
          }
        } // lock scope
        if(!success) return false;
      } // for scope
      return true;
    }
    void reset(bool succeeded) {
        for(auto& entry : dataset) {
            auto& meta = GlobalLock::AddrToLockVar(entry.second.addr);
            std::lock_guard<std::mutex> guard(meta_mutex);
            if(entry.second.type == Type::Read) {
                // nothing
            } else {
                if(succeeded) {
                    meta.ClearWriteBitAndIncTimestamp();
                } else {
                    memcpy(entry.second.addr, entry.second.value, entry.second.size);
                    meta.ClearWriteBit();
                }
            }
        }
        if(succeeded) {
          frees.ReleaseAllForward(TMAllocList::visitor_type{});
        } else {
          allocations.ReleaseAllReverse(TMAllocList::visitor_type{});
        }
        frees.clear();
        allocations.clear();
        dataset.clear();
    }

    bool open_for_read(void* addr) {
        auto entry = dataset.find(reinterpret_cast<uintptr_t>(addr));
        if(entry != dataset.end())  {
            return true;
        }

        bool result = false; 
        {
            std::lock_guard<std::mutex> guard(meta_mutex);

            auto& meta = GlobalLock::AddrToLockVar(addr);

            if(!meta.TestWriteBit()) {
              auto& new_entry = dataset[reinterpret_cast<uintptr_t>(addr)];
              new_entry.addr = addr;  
              new_entry.old_version = *meta;
              new_entry.type = Type::Read;

              result = true;
            } else {
            }
        }
        return result;
    }
    bool open_for_write(void* addr, std::size_t size) {
        auto entry = dataset.find(reinterpret_cast<uintptr_t>(addr));
        if(entry != dataset.end()) {
            if(entry->second.type == Type::Write) {
                return true;
            }
        }

        bool result;
        {
            std::lock_guard<std::mutex> guard(meta_mutex);

            auto& meta = GlobalLock::AddrToLockVar(addr);

            if(!meta.TestWriteBit()) 
                result = true;

            if(result && entry != dataset.end()) {
                if(entry->second.type == Type::Read && *meta != entry->second.old_version) 
                    result = false;
            }

            if(result) {
                meta.SetWriteBit();

                // 新しいエントリが生成される可能性もある
                auto& entry = dataset[reinterpret_cast<uintptr_t>(addr)];
                entry.addr = addr;
                entry.type = Type::Write;
                entry.size = size;
                std::memcpy(entry.value, addr, size);
            }
        }
        return result;
    }
    static TxDescriptor& Get(int tid) {
      static std::unordered_map<int, TxDescriptor> desc_table;
      auto& e = desc_table[tid];
      e.my_tid = tid;
      return e;
    }
};

class STM {
    public:
    template<typename T>
    static T Load(int tid, T* addr) {
        static_assert(std::is_scalar<T>::value == true, "stm_write cannot use non scalar type");
        //std::cout << __func__ << " : addr=" << addr << std::endl;
        auto& tx_desc = TxDescriptor::Get(tid);

        bool success = tx_desc.open_for_read(addr);
        if(success) {
          return *addr;
        } else {
          tx_desc.reset(false);
          std::cerr << "Aborted by:open_for_read... addr:" << addr << std::endl;
          throw std::runtime_error("Load");
        }
        
        return (T)0;
    }
    template<typename T>
    static void Store(int tid, T* addr, T val) {
        // std::cout << __func__ << " : addr="<<addr<< std::endl;
      auto& tx_desc = TxDescriptor::Get(tid);

      if(tx_desc.open_for_write(addr, sizeof(T))) {
        std::memcpy(addr, &val, sizeof(T));
      } else {
        tx_desc.reset(false);
        throw std::runtime_error("Store");
      }
    }
    static void Abort(int tid) {
        std::cout << __func__ << std::endl;
      auto& tx_desc = TxDescriptor::Get(tid);

      tx_desc.reset(false);
      throw std::runtime_error("Explicit");
    }
    static void* Alloc(int tid, size_t size) {
      auto& tx_desc = TxDescriptor::Get(tid);
      void* ptr = tx_desc.allocations.Reserve(size);
      if(ptr) {
        // std::cout << "allocate ["  << ptr << "]" << std::endl;
        tx_desc.allocations.Append(ptr);
      } else {
        std::cerr << "cannot reserve memory" << std::endl;
        Abort(tid);
      }

      return ptr;
    }
    static void Free(int tid, void* addr) {
        std::cout << __func__ << std::endl;
      auto& tx_desc = TxDescriptor::Get(tid);
      tx_desc.frees.Append(addr);
      if(!tx_desc.open_for_write(addr, sizeof(void*)))
        Abort(tid);
    }
};

#define STM_SELF threadID
#define STM_INIT() 
#define STM_EXIT() 
#define TxBegin(tid) \
  for(int __retries = 0; __retries < 10; ) { \
    int STM_SELF = tid; \
    TxDescriptor::Get(STM_SELF).depth++; \
    try {
#define TxCommit() \
      auto& __desc = TxDescriptor::Get(STM_SELF);\
      if(!--__desc.depth) { \
        if(!__desc.validate())  \
          throw std::runtime_error("Validate"); \
        __desc.reset(true); \
      } \
      std::cout << "Committed!" << std::endl; \
      break; \
    } catch(const std::runtime_error& err) {\
      /*...*/ \
      std::cerr << "Abort : " << err.what() << std::endl; \
    } \
    __retries++;\
  }
#define TxAbort() \
  STM::Abort(STM_SELF)
#define TxLoad(addr) STM::Load(STM_SELF, addr)
#define TxStore(addr, value)  STM::Store(STM_SELF, addr, value)
#define TxAlloc(size) STM::Alloc(STM_SELF, size)
#define TxFree(ptr) STM::Free(STM_SELF, ptr)

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

#define TM_STARTUP(numThread) STM_INIT()
#define TM_SHUTDOWN()         STM_EXIT() 
#define TM_THREAD_ENTER()
#define TM_THREAD_EXIT()
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