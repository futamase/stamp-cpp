#include "stm.hpp"


#ifdef STM_TEST 
int* ptr;
void alloc_test(int tid) {
    if(tid == 0)
        TM_SHARED_WRITE_P(ptr, TM_MALLOC(sizeof(int)));
    else  {
        if(TM_SHARED_READ_P(ptr))
            TM_FREE(ptr);
    }

}
int main() {

    TM_STARTUP(1);
    TM_SHUTDOWN()
    return 0;
}
#endif