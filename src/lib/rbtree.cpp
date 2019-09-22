#include "rbtree.hpp"

#ifdef RBTREE_TEST 

MAP_T* map;
int gData = 114514;

int main() {
    TM_STARTUP(1);
    map = MAP_ALLOC(nullptr, nullptr);
    int tid = 0;
    std::thread worker([tid]() {
        TM_THREAD_ENTER();
        {
            TM_BEGIN(tid);
            if(TMMAP_CONTAINS(map, 314)) {
                std::cout << TMMAP_FIND(map, 314) << std::endl;
            } else {
                std::cout << "key 314 not found" << std::endl;
                TMMAP_INSERT(map, 314, &gData);
                std::cout << "key 314 inserted" << std::endl;
            }
            TM_END();
        }
        TM_THREAD_EXIT();
    });
    worker.join();
    MAP_FREE(map);
    TM_SHUTDOWN();
}
#endif