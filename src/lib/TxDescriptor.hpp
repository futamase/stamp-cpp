#ifndef TX_DESCRIPTOR
#define TX_DESCRIPTOR

#include <mutex>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <list>
#include <algorithm>

#include "tmalloc.hpp"
#include "debug_print.hpp"

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
class VersionedWriteLock {
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
        uintptr_t old_version;
        std::size_t size;
        char value[128];
    };
    struct Stats {
      unsigned long commits = 0;
      unsigned long aborts = 0;
      std::string concat_all_stats() const {
        return " commits:" + std::to_string(commits) + " aborts:" + std::to_string(aborts);
      }
    } stats;
    std::list<LogEntry> dataset;
    // std::unordered_map<uintptr_t, LogEntry> dataset;
    TMAllocList allocations, frees;
    unsigned depth = 0;
    int my_tid;

    TxDescriptor() : allocations(1 << 5), frees(1 << 5) {}

    bool validate() {
      bool success = true;
      for(auto& entry : dataset) {
        auto& meta = VersionedWriteLock::AddrToLockVar(entry.addr);
        {
          std::lock_guard<std::mutex> guard(meta_mutex);
          if(entry.type == Type::Read) {
            if(*meta != entry.old_version) {
              DEBUG_PRINT("version check failed: addr=%p meta=%ld", entry.addr, *meta);
              success = false;
            }
          }
        } // lock scope
        if(!success) return false;
      } // for scope
      return true;
    }
    void reset(bool succeeded) {
        {
          std::lock_guard<std::mutex> guard(meta_mutex);
          for(auto& entry : dataset) {
              auto& meta = VersionedWriteLock::AddrToLockVar(entry.addr);
              if(entry.type == Type::Read) {
                  // nothing
              } else {
                  if(succeeded) {
                      meta.ClearWriteBitAndIncTimestamp();
                  } else {
                      memcpy(entry.addr, entry.value, entry.size);
                      meta.ClearWriteBit();
                  }
              }
          }
          if(succeeded) {
            frees.ReleaseAllForward([](void* ptr, size_t size){
              auto& free_meta = VersionedWriteLock::AddrToLockVar(ptr);
              free_meta.IncReaders();
            });
          } else {
            allocations.ReleaseAllReverse(TMAllocList::visitor_type{});
          }
        }
        frees.clear();
        allocations.clear();
        dataset.clear();
    }

    bool open_for_read(void* addr) {
        auto entry = std::find_if(dataset.begin(), dataset.end(),
          [addr](const LogEntry& e){ return e.addr == addr; });
        if(entry != dataset.end())  {
            return true;
        }

        bool result = false; 
        {
            std::lock_guard<std::mutex> guard(meta_mutex);

            auto& meta = VersionedWriteLock::AddrToLockVar(addr);

            if(!meta.TestWriteBit()) {
              dataset.emplace_back();
              auto& new_entry = dataset.back();
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
        auto entry = std::find_if(dataset.begin(), dataset.end(),
          [addr](const LogEntry& e){ return e.addr == addr; });
        if(entry != dataset.end()) {
            if(entry->type == Type::Write) {
                return true;
            }
        }

        bool result = false;
        {
            std::lock_guard<std::mutex> guard(meta_mutex);

            auto& meta = VersionedWriteLock::AddrToLockVar(addr);

            if(!meta.TestWriteBit()) 
                result = true;

            if(result && entry != dataset.end()) {
                // 自身が以前に読み出したが，他者が書き込んだ
                if(entry->type == Type::Read && *meta != entry->old_version) 
                    result = false;
            }

            if(result) {
                meta.SetWriteBit();

                if(entry == dataset.end()) {
                  dataset.emplace_back();
                  auto& new_entry = dataset.back();
                  new_entry.type = Type::Write;
                  new_entry.addr = addr;
                  new_entry.size = size;
                  std::memcpy(new_entry.value, addr, size);
                } else { // 自身が以前に読み出し，初めて書き込む場合
                  entry->type = Type::Write;
                  entry->size = size;
                  std::memcpy(entry->value, addr, size);
                }
            }
        }
        return result;
    }
};

#endif