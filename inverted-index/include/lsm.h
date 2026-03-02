#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <utility>
#include <bitset>
#include <optional>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <random>
#include <cstdint>
#include <memory>

#include <roaring/roaring.hh>

template <uint64_t K, size_t N>
class BloomFilter {
private:
    std::bitset<N> arr;

public:
    BloomFilter();

    void Add(const std::string& s);
    bool Contains(const std::string& s) const;

private:
    static uint64_t string_hash(const std::string& s); // djb2
    static uint64_t prng(uint64_t& state); // splitmix64 
};

class SSTable {
private:
    const size_t BLOCK_SIZE;
    std::string path;
    std::ifstream in;

    std::vector<std::pair<std::string, uint64_t>> offsets;
    BloomFilter<7, 1000000> bloom_filter;

public:
    SSTable(const size_t BLOCK_SIZE, const std::string& path, const std::map<std::string, roaring::Roaring>& data);
    SSTable(const size_t BLOCK_SIZE, const std::string& path, std::vector<SSTable>&& prev_level);

    SSTable(const SSTable&) = delete;
    SSTable& operator=(const SSTable&) = delete;
    SSTable(SSTable&&) noexcept = default;
    SSTable& operator=(SSTable&&) noexcept = delete;

    std::vector<std::pair<std::string, roaring::Roaring>> ReadBlock(uint64_t offset);
    std::optional<uint64_t> GetOffset(const std::string& key);
    roaring::Roaring Get(const std::string& key);

    static roaring::Roaring StringToRoaring(const std::string& bitmap_str);
    static std::string RoaringToString(const roaring::Roaring& bitmap);
};

class LSMTree {
private:
    const size_t C; // max size of the memtable
    const size_t R; // max number of sstables on one level
    const size_t BLOCK_SIZE;
    std::map<std::string, roaring::Roaring> memtable;
    std::vector<std::vector<SSTable>> sstables;
    
public:
    LSMTree(const size_t C, const size_t R, const size_t BLOCK_SIZE);
    
    void Add(const std::string& key, uint32_t value);
    roaring::Roaring Get(const std::string& key);
};
