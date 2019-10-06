#include "BloomFilter.hpp"
#include <iostream>

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

int main() {
        // エラー率 p = 1 %
        // 挿入される要素数 n = 4096
        // とした時のフィルタサイズ m, ハッシュ関数の数 kを指定
        // BloomFilter<int> filter(39260, 6);

        for(int i = 0; i < 1024; i++) {
            arr[i] = i;
        }

        for(int i = 0; i < 512; i++) {
            // filter.Add(&arr[i], 1);
            hoge(arr+i);
        }

        for(int i = 256; i < 768; i++) {
            // if(filter.Contains(&arr[i], 1)) 
            if(hage(arr+i))
                std::cout << i << " hit" << std::endl;
            else 
                std::cout << i << " bad" << std::endl;
        }
}