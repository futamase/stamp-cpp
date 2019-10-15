#ifndef DTM_HPP
#define DTM_HPP

#include <upcxx/upcxx.hpp>
#include <type_traits>

#include "TxDescriptor.hpp"
#include "tx_exception.hpp"
#include "rbtree.hpp"

class DTM {
    template<class T>
    using dist_par_vec = upcxx::dist_object<std::vector<std::vector<T>>>;

    static dist_par_vec<TxDescriptor> desc_table;

    public:
    static TxDescriptor& GetDesc(int pid, int tid) {
      return desc_table[pid][tid];
    }
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
  using rbt_t = RBTree<Key, Data>;
  using drbt_t = upcxx::dist_object<rbt_t>;
  drbt_t rbt;
  // SFINAE よりもスマートだろうがバカ
  template<class Fn, class ...TArgs, class Ret = typename std::invoke_result_t<Fn, Targs...>>
  Ret InvokeOn(int rank, Fn fn, TArgs&&... args) {
    if constexpr (std::is_same<Ret, void>::value) {
      if(rank == upcxx::rank_me()) {
        fn(std::forward<TArgs>(args)...);
      } else {
        upcxx::rpc(rank, []() -> bool {
          try {
            fn(std::forward<TArgs>(args)...);
            return true;
          } catch(const inter_tx_exception& e) {
            auto& desc = DTM::GetDesc(src_pid, src_tid);
            desc.reset(false);
            return false;
          }
        });
      }
    } else { /*fnの戻り値がvoidではない*/
      if(rank == upcxx::rank_me()) {
        return fn(std::forward<TArgs>(args)...);
      } else {
        upcxx::rpc(rank, [](){
          fn()
        }, /**/);
      }
    }
  }
  auto fn = std::bind(&rbt_t::TxGet, )
  Data TxGet(int tid, Key key){
    auto pid = get_target_rank(key);
    if(pid == upcxx::rank_me()){
      return rbt->TxGet(tid, key);
    } else {
      upcxx::rpc(pid, [](drbt_t& drbt, int src_pid, int src_tid, Key key) { 
        try {
          drbt->TxGet(src_tid, key);
        } catch(const inter_tx_exception& e) {
          
        }
      }, rbt, pid, tid).wait();
    }
  }
};
template<class Key>
inline int get_target_rank(Key key) { return key % upcxx::rank_n(); }

#define MAP_T upcx::dist_object<RBTree<long, void*>>
#define MAP_ALLOC(hash, cmp) new MAP_T()
#define MAP_FREE(map) delete map

#define MAP_FIND(map, key) \
  get_target_rank(key) == rank_me() ? map->CALL_TMFN(Get, key) \
  : upcxx::rpc(get_target_rank(key), [](MAP_T& m, long key){ return m->Get(key);}, map, key).wait()

#define MAP_FIND(map, key) \
  get_target_rank(key) == rank_me() ? map->Get(key) \
  : upcxx::rpc(get_target_rank(key), [](MAP_T& m, long key){ return m->Get(key);}, map, key).wait()

// 要件
/* 1. key の値から保存先プロセスIDを求められる
 * 2. インターフェースは1ノードのデータ構造と同じ
 * 3. 簡単に分散データ構造を構築したい
 * 3a.既存のデータ構造を変更せずに構築したい
 */

/* STM_{ READ, WRITE, ALLOC, FREE, ABORT }はメモリプロセスが呼ぶ
 * 失敗した場合
 * - 自ノードなら throw
 * - 他ノードなら 失敗したことを伝えて自身のトラッキングデータを削除
 * → 結局リモート関数も勝手にTxで囲んだ方が楽な気がする
 */
#endif
