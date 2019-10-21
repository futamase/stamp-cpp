#include <upcxx/upcxx.hpp>
#include <unordered_map>

// 検索
// ないなら確保し挿入
// あれば更新
// key % num_rank で格納先ノードを決定

struct reservation {
    long id;
    int price;
};

using Map = upcxx::dist_object<std::unordered_map<long, reservation*>>;
Map dmap;

reservation* TMMAP_FIND(Map& map, long id) {
    auto rank = id % upcxx::rank_me();
    if(rank == upcxx::rank_me()) {
        auto e = map->find(id);
        if(e != map->end()) return *e;
        else return nullptr;
    } else {
        reservation* p = upcxx::rpc(rank, [](Map& map, long id) -> reservation* {
            auto e = map->find(id);
            if(e != map->end()) return *e;
            else return nullptr;
        }, map, id).wait();
        return p;
    }
}

void TMMAP_INSERT(Map& map, long id, reservation* p) {
    auto rank = id % upcxx::rank_me();
    if(rank == upcxx::rank_me()) {
        map->emplace(id, p);
    } else {
        upcxx::rpc(rank, [](Map& map, long id, reservation* p) {
            map->emplace(id, p);
        }, map, id, p).wait();
    }
}

void* TM_ALLOC(size_t size) {
    return malloc(size);
}

void addReservation(Map& map, long id, int price) {
    reservation* p = nullptr;

    p = TMMAP_FIND(map, id);
    if(p) {
        // update
        p->id = id;
        p->price = price;
    } else {
        p = (reservation*)TM_ALLOC(sizeof(reservation));
        TMMAP_INSERT(map, p);
    }
}

int main() {
    upcxx::init();

    reservation obj1, obj2;
    auto neighbor = (upcxx::rank_me() + 1) % upcxx::rank_n();
    TMMAP_INSERT(dmap, neighbor, &obj1);

    upcxx::barrier();
    if((auto p = TMMAP_FIND(dmap, neighbor))) {
        std::cout << upcxx::rank_me() << " " << p->id << std::endl;
    }

    upcxx::finalize();
}