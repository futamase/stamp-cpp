#include "BloomFilter.hpp"

int main() {
        // エラー率 p = 1 %
        // 挿入される要素数 n = 4096
        // とした時のフィルタサイズ m, ハッシュ関数の数 kを指定
        BloomFilter<uintptr_t> filter(39260, 6);

}