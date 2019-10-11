#include <upcxx/upcxx.hpp>
#include <iostream>

int main(){
  upcxx::init();

  std::cout << upcxx::rank_me() << std::endl;

  upcxx::finalize();
}
