#include "BloomFilter.hpp"
#include <iostream>
#include <random>
#include <algorithm>

int arr[1024];
BloomFilter<uintptr_t> filter(39260, 6);

void hoge(int* p) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    filter.Add(&addr, sizeof(uintptr_t));
}

bool hage(int* p) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    return filter.Contains(&addr, sizeof(uintptr_t));
}

std::random_device seed_gen;
std::mt19937 engine(seed_gen());
std::uniform_int_distribution<> dist(0, 1024);
int random_indices[256];

// 比較したい
// 何と
// vectorに登録して検索する場合と
// bloom にも登録して検索する場合
void init_scene() {
    for(int r = 0; r < 256; r++){
        random_indices[r] = dist(engine);
    }
}
struct DatasetEntry {
    void* addr;
    int idx;
    char value[128];
    DatasetEntry(void*ad, int i) :addr(ad), idx(i) {}
};

void vec_only() {
    std::vector<DatasetEntry> set;
    set.reserve(1024);
    for(int i = 0; i < 256; i++){
        auto addr = &arr[random_indices[i]];//reinterpret_cast<uintptr_t>(&arr[random_indices[i]]);
        // 登録済みでないなら挿入
        auto it = std::find_if(set.begin(), set.end(), [&addr](const DatasetEntry& e)
            { return e.addr == addr; });
        if(it == set.end()) 
            set.emplace_back(addr, random_indices[i]);
    }

    // デバッグ出力
    // std::cout << "vec_only>>> " << set.size() << std::endl;
    // for(const auto& e : set) {
    //     std::cout << e.idx << " ";
    // }
    // std::cout << "\nvec_only<<<" << std::endl;
}
void vec_and_bloom() {
    BloomFilter<int*> bf(39260, 6);
    std::vector<DatasetEntry> set;
    set.reserve(1024);
    for(int i = 0; i < 256; i++){
        auto addr = &arr[random_indices[i]];//reinterpret_cast<uintptr_t>(&arr[random_indices[i]]);
        // まずbloom filterに通す
        if(!bf.Contains(&addr, sizeof(int*))){
            //確実に入っていないなら挿入
            bf.Add(&addr, sizeof(int*));
            set.emplace_back(addr, random_indices[i]);
            continue;
        } 
        // 入っていそうなら詳細検索
        auto it = std::find_if(set.begin(), set.end(), [&addr](const DatasetEntry& e)
            { return e.addr == addr; });
        // 本当は入ってなかったら挿入
        if(it == set.end()) 
            set.emplace_back(addr, random_indices[i]);
    }

    // std::cout << "vec_and_bloom>>> " << set.size() << std::endl;
    // for(const auto& e : set) {
    //     std::cout << e.idx << " ";
    // }
    // std::cout << "\nvec_and_bloom<<<" << std::endl;
}

int main() {
    // エラー率 p = 1 %
    // 挿入される要素数 n = 4096
    // とした時のフィルタサイズ m, ハッシュ関数の数 kを指定
    // BloomFilter<int> filter(39260, 6);

    init_scene();

    vec_only();

    vec_and_bloom();
}