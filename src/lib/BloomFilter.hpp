#ifndef BLOOM_FILTER_HPP
#define BLOOM_FILTER_HPP

#include <array>
#include <vector>
#include <functional>
#include "MurmurHash3.h"

/* https://postd.cc/how-to-write-a-bloom-filter-cpp/ 
*/

template<class Key = uint8_t, class Hash = std::hash<Key>>
struct BloomFilter {
    BloomFilter(uint64_t size, uint8_t numHashes)
        : bits_(size), numHashes_(numHashes) 
    {}
    void Clear() {
        for(auto it = bits_.begin(); it != bits_.end(); ++it) 
            *it = false;
    }
    void Add(const Key& data, std::size_t len) {
        auto hashValues = hash(&data, len);

        for (int n = 0; n < numHashes_; n++) {
            bits_[nthHash(n, hashValues[0], hashValues[1], bits_.size())] = true;
        }
    }
    bool Contains(const Key& data, std::size_t len) const {
        auto hashValues = hash(&data, len);
        
        for (int n = 0; n < numHashes_; n++) {
            if (!bits_[nthHash(n, hashValues[0], hashValues[1], bits_.size())]) {
                return false;
            }
        }
        
        return true;
    }
    private:
    std::vector<bool> bits_;
    uint8_t numHashes_;

    std::array<uint64_t, 2> hash(const Key* data,
                                std::size_t len) const {
        std::array<uint64_t, 2> hashValue;
        MurmurHash3_x64_128(data, len, 0, hashValue.data());
        
        return hashValue;
    }
    inline uint64_t nthHash(uint8_t n,
                            uint64_t hashA,
                            uint64_t hashB,
                            uint64_t filterSize) const {
        return (hashA + n * hashB) % filterSize;
    }
};

#endif
