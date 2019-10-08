#ifndef TX_DESCRIPTOR
#define TX_DESCRIPTOR

#include <mutex>
#include <string>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <list>
#include <algorithm>
#include <iostream>
#include <random>

#include "BloomFilter.hpp"
#include "tmalloc.hpp"
#include "debug_print.hpp"
#include "tx_exception.hpp"

class LockVar {
    volatile uintptr_t lock = 0;
    public:
    bool TestWriteBit() const { return (lock & 1); } 
    void SetWriteBit() { lock |= 1; }
    void ClearWriteBit() { lock &= ~1; }
    void ClearWriteBitAndIncTimestamp() { lock = (lock & ~1) + 2; }

    int Readers() const { return lock >> 1; }
    void IncReaders() { lock += 2; }
    void DecReaders() { lock -= 2; } 
    uintptr_t WriterID() const { return lock >> 1; }
    // これは正直クソ
    uintptr_t operator*() const { return lock; }
    // int idx = -1;  // for test
};
class VersionedWriteLock {
    static const unsigned long TABLE_SIZE = 1UL << 20;
    public:
    static LockVar& AddrToLockVar(void* addr) {
      static LockVar vars[TABLE_SIZE];
      int idx = ((reinterpret_cast<uintptr_t>(addr) + 128) >> 3) & (TABLE_SIZE - 1);
      // if(vars[idx].idx == -1) vars[idx].idx = idx; // for test
      return vars[idx];
    }
    static void PrintWriteBitIsSet() {
    }
};

static std::mutex alloc_lock;
static std::mutex meta_mutex;

struct alignas(64) TxDescriptor {
    enum class Type {
        Read = 1, 
        Write = 2
    };
    struct LogEntry {
        void* addr;
        Type type;
        uintptr_t old_version;
        std::size_t size;
        char value[128];
        bool ReferToSameMetadata(const LockVar& meta) const {
          auto& myMeta = VersionedWriteLock::AddrToLockVar(addr);
          return (&myMeta == &meta); 
        }
    };
    struct Stats {
      unsigned long commits = 0;
      unsigned long aborts = 0;
      std::string concat_all_stats() const {
        return " commits:" + std::to_string(commits) + " aborts:" + std::to_string(aborts);
      }
    } stats;
    TMAllocList allocations, frees;
    // std::list<LogEntry> dataset;
    std::vector<LogEntry> readSet;
    std::vector<LogEntry> writeSet;
    BloomFilter<uintptr_t> writeSetFilter;
    // std::unordered_map<uintptr_t, LogEntry> dataset;
    unsigned depth = 0;
    int my_tid;

    // std::random_device rnd_dvc;
    std::mt19937_64 mt;
    void backoff(long attempt) {
      unsigned long long stall = mt() & 0xF;
      stall += attempt >> 2;
      stall *= 10;
      volatile unsigned long long i = 0;
      while(i < stall) {
        i++;
      }
    }


    TxDescriptor() 
      : allocations(1 << 5), frees(1 << 5), writeSetFilter(39260, 6), mt(std::random_device{}())
    {
      readSet.reserve(4096);
      writeSet.reserve(1024);
    }
    bool validate() const {
      bool success = true;
      {
        std::lock_guard<std::mutex> guard(meta_mutex);
        for(const auto& entry : readSet) {
          auto& meta = VersionedWriteLock::AddrToLockVar(entry.addr);
          {
            if(*meta != entry.old_version) {
              auto writeEntry = std::find_if(writeSet.begin(), writeSet.end(),
                [&meta](const LogEntry& e){ return e.ReferToSameMetadata(meta); });
              if(writeEntry != writeSet.end()) {
                DEBUG_PRINT("Same Meta! %s", "yes");
              } else {
                DEBUG_PRINT("version check failed: addr=%p meta=%ld, idx=%x ", entry.addr, *meta, ((reinterpret_cast<uintptr_t>(entry.addr)+128) >> 3) & ((1 << 20) - 1));
                success = false;
              }
            }
          } // lock scope
          if(!success) break;
        } // for scope
      }
      return success;
    }
    void reset(bool succeeded) {
        {
          std::lock_guard<std::mutex> guard(meta_mutex);
          for(auto& entry : writeSet) {
              auto& meta = VersionedWriteLock::AddrToLockVar(entry.addr);
              if(succeeded) {
                  meta.ClearWriteBitAndIncTimestamp();
                  // std::cout << "ClearWriteBitAndIncTimestamp called after value=" << *meta << std::endl;
              } else {
                  memcpy(entry.addr, entry.value, entry.size);
                  meta.ClearWriteBit();
              }
          }
          if(succeeded) {
            frees.ReleaseAllForward([](void* ptr, size_t size){
              // auto& free_meta = VersionedWriteLock::AddrToLockVar(ptr);
              // free_meta.IncReaders();
            });
          } else {
            allocations.ReleaseAllReverse(TMAllocList::visitor_type{});
          }
        }
        frees.clear();
        allocations.clear();
        readSet.clear();
        writeSet.clear();
        writeSetFilter.Clear();
    }
    bool open_for_read(void* addr) {
        if(writeSetFilter.Contains(reinterpret_cast<uintptr_t>(addr), sizeof(uintptr_t))) {
            auto we = std::find_if(writeSet.begin(), writeSet.end(),
              [addr](const LogEntry& e){ return e.addr == addr; });
            if(we != writeSet.end())  {
                return true;
            }
        }

        auto re = std::find_if(readSet.begin(), readSet.end(),
          [addr](const LogEntry& e){ return e.addr == addr; });
        if(re != readSet.end())  {
            return true;
        }

        bool result = false; 
        {
            std::lock_guard<std::mutex> guard(meta_mutex);

            auto& meta = VersionedWriteLock::AddrToLockVar(addr);

            if(!meta.TestWriteBit() || 
              std::find_if(writeSet.begin(), writeSet.end(), 
                [&meta](const LogEntry& e){ return e.ReferToSameMetadata(meta); })
                != writeSet.end()
            ) {
              readSet.emplace_back();
              auto& new_entry = readSet.back();
              new_entry.addr = addr;  
              new_entry.old_version = *meta;
              // new_entry.type = Type::Read;

              result = true;
              // std::cout << "Register in ReadSet [" << addr << "] type(1=R,2=W)=" << (int)dataset.back().type << " meta_idx=" << meta.idx<< std::endl;
            }
        }
        return result;
    }
    bool open_for_write(void* addr, std::size_t size) {
        if(writeSetFilter.Contains(reinterpret_cast<uintptr_t>(addr), sizeof(uintptr_t))) {
            auto we = std::find_if(writeSet.begin(), writeSet.end(),
              [addr](const LogEntry& e){ return e.addr == addr; });
            if(we != writeSet.end()) {
              return true;
            }
        }

        auto re = std::find_if(readSet.begin(), readSet.end(),
          [addr](const LogEntry& e){ return e.addr == addr; });

        bool result = false;
        {
            std::lock_guard<std::mutex> guard(meta_mutex);

            auto& meta = VersionedWriteLock::AddrToLockVar(addr);

            // 誰も書き込んでいない
            if(!meta.TestWriteBit()) {
                result = true;
            } else {
                // 誰かが書き込んだが、それが自分であった場合
                // つまり複数の変数を同一のロックで保護するようなもの？
              auto otherWrite = std::find_if(writeSet.begin(), writeSet.end(), 
                [&meta](const LogEntry& e){ return e.ReferToSameMetadata(meta); });
              if(otherWrite != writeSet.end()) 
                return true;
            }

            if(result && re != readSet.end()) {
                // 自身が以前に読み出したが，他者が書き込んだ
                // (本来ここにあったtype == Readは必要ないだろうがクソ野郎)
                if(*meta != re->old_version) {
                    result = false;
                }
            }

            if(result) {
                meta.SetWriteBit();

                // ここに到達するパターン
                // パターン1. 読み出しも書き込みも初めて
                // パターン2. 読み出ししたが書き込みは初めて
                writeSet.emplace_back();
                auto& new_entry = writeSet.back();
                // new_entry.type = Type::Write;
                new_entry.addr = addr;
                new_entry.size = size;
                std::memcpy(new_entry.value, addr, size);
                // std::cout << "Register in WriteSet [" << addr << "] type(1=R 2=W)=" << (int)dataset.back().type << " meta_idx=" << meta.idx << std::endl;
                writeSetFilter.Add(reinterpret_cast<uintptr_t>(addr), sizeof(uintptr_t));
            }
        }
        return result;
    }
};

#endif
