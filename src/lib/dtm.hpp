#ifndef DTM_HPP
#define DTM_HPP

#include <upcxx/upcxx.hpp>

#include "TxDescriptor.hpp"
#include "tx_exception.hpp"

class DTM {
    template<class T>
    using dist_par_vec = upcxx::dist_object<std::vector<std::vector<T>>>;

    static dist_par_vec<TxDescriptor> desc_table;

    public:
    static void Init(int numThraeds) {
        upcxx::init();

        desc_table->resize(upcxx::rank_n());

        for(int p = 0; p < upcxx::rank_n(); p++) {
            desc_table->at(p).resize(numThraeds);
            for(int t = 0; t < numThraeds; t++)
                desc_table->at(p).at(t).my_tid = t;
        }
    }
    static void Exit() {
        upcxx::finalize();
    }
    template<typename T>
    static T Load(int pid, int tid, T* addr) {
    }
};
template<class Key, class Data>
class drbt {
  upcxx::dist_object<RBTree<Key, Data>> rbt;
  void InvokeOn(Key key, 
  void TxGet(Key key){
    auto pid = key2pid(key);
    if(pid == upcxx::rank_me()){
    }
  }
};

#endif
