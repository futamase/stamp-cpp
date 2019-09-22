#ifndef TMALLOC_HPP
#define TMALLOC_HPP
#include <vector>
#include <functional>
#include <cstddef>
#include <cstdlib>

// like tl2/tmalloc

#define BLK2DATA(blk)   ((void*)((char*)(blk) + sizeof(tmalloc_info)))
#define DATA2BLK(data)  ((void*)((char*)(data) - sizeof(tmalloc_info)))
#define INFO2BLK(info)  ((void*)(info))
#define BLK2INFO(blk)   ((tmalloc_info*)(blk))
#define DATA2INFO(data) ((tmalloc_info*)DATA2BLK(data))
#define INFO2DATA(info) (BLK2DATA(INFO2BLK(info)))
struct tmalloc_info {
    std::size_t size;
    char pad[sizeof(void*) - sizeof(std::size_t)];
};

struct TMAllocList : public std::vector<void*> {
    using visitor_type = std::function<void(void*, std::size_t)>;
    TMAllocList(std::size_t capa) {
        reserve(capa);
    }
    void* Reserve(std::size_t size) {
        // void* blk_ptr = malloc(sizeof(tmalloc_info) + size);
        // if(!blk_ptr)
        //     return nullptr;

        // tmalloc_info* info = BLK2INFO(blk_ptr);
        // info->size = size;
        // void* data_ptr = BLK2DATA(blk_ptr);
        void* data_ptr = malloc(size);
        return data_ptr;
    }
    void Append(void* ptr) {
        push_back(ptr);
    }
    void Release(void* ptr) {
        //void* blk_ptr = DATA2BLK(ptr);
        free(ptr);
    }
    void ReleaseAllForward(visitor_type fn) {
        for(auto i = begin(); i != end(); ++i) {
            // void* data = *i;
            // tmalloc_info* info = DATA2INFO(data);
            // //std::size_t size = info->size;
            // //fn(data, size);
            // void* blk_ptr = INFO2BLK(info);
            void* blk_ptr = *i;
            free(blk_ptr);
        }
    }
    void ReleaseAllReverse(visitor_type fn)  {
        for(auto ri = rbegin(); ri != rend(); ++ri) {
            // void* data = *ri;
            // tmalloc_info* info = DATA2INFO(data);
            // //std::size_t size = info->size;
            // //fn(data, size);
            // void* blk_ptr = INFO2BLK(info);
            void* blk_ptr = *ri;
            free(blk_ptr);
        }
    }
    void FreeAll() {
        for(auto ite = begin(); ite != end(); ++ite) {
            free(*ite);
        }
    }
};

#endif