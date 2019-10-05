#include "stm.hpp"
#include <thread>

int a;
char dum[64];
int b;

void wo(int tid) {
  TM_BEGIN(tid);
    TM_SHARED_WRITE(b, 10);
    int tmp = TM_SHARED_READ(a);
  TM_END();
}

int main() {
  TM_STARTUP(1);

  std::thread th(wo, 0);

  th.join();
  TM_SHUTDOWN();
}
