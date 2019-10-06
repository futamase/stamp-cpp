#include "stm.hpp"
#include <thread>

int a;
char dum[64];
int b;

const int N = 10000;

void wo(int tid) {
  for(int n = 0; n < N; n++) {
    TM_BEGIN(tid);
    for(int i = 0; i < 100; i++){
      TM_SHARED_WRITE(b, 10);
      int tmp = TM_SHARED_READ(a);
    }
    TM_END();
  }
}

int main() {
  TM_STARTUP(1);

  std::thread th(wo, 0);

  th.join();
  TM_SHUTDOWN();
}
