#include <upcxx/upcxx.hpp>
#include <thread>
#include <sched.h>


struct DTM {
  static void Init() {
    upcxx::init();

    upcxx::liberate_master_persona();
    std::thread progress_th([](){
      upcxx::persona_scope master_scope(upcxx::master_persona());
      while(true) {
        sched_yield();
        upcxx::progress();
      }
    });

    progress_th.detach();
    upcxx::barrier();
  }

  static void Exit() {
    upcxx::persona_scope master_scope(upcxx::master_persona());
    upcxx::barrier();
    upcxx::finalize();
  }
};
