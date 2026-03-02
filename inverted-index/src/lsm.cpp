#include "lsm.h"
#include <filesystem>
#include <roaring/roaring.hh>
#include <sstream>
#include <string>

template <uint64_t K, size_t N>
BloomFilter<K, N>::BloomFilter() : arr(0) {}

template <uint64_t K, size_t N>
void BloomFilter<K, N>::Add(const std::string& s) {
    uint64_t state = string_hash(s);
    for (size_t i = 0; i < K; i++) {
        arr[prng(state) % N] = 1;
    }
}

template <uint64_t K, size_t N>
bool BloomFilter<K, N>::Contains(const std::string& s) const {
    uint64_t state = string_hash(s);
    for (size_t i = 0; i < K; i++) {
        if (!arr[prng(state) % N]) {
            return false;
        }
    }
    return true;
}

template <uint64_t K, size_t N>
uint64_t BloomFilter<K, N>::string_hash(const std::string& s) {
    uint64_t hash = 5381;
    for (unsigned char c : s) {
        hash = ((hash << 5) + hash) + static_cast<uint64_t>(c);
    }
    return hash;
}

template <uint64_t K, size_t N>
uint64_t BloomFilter<K, N>::prng(uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

SSTable::SSTable(const size_t BLOCK_SIZE, const std::string& path, const std::map<std::string, roaring::Roaring>& data)
    : BLOCK_SIZE(BLOCK_SIZE), path(path), offsets(), bloom_filter() {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("bad path: " + path);
    }
    int ind = 0;
    for (const auto& [key, value] : data) {
        if (ind % BLOCK_SIZE == 0) {
            auto res = out.tellp();
            if (res == std::streampos(-1)) {
                throw std::runtime_error("tellp failed :(");
            }
            uint64_t offset = static_cast<uint64_t>(res);
            offsets.emplace_back(key, offset);
        }
        out << key << '\n' << RoaringToString(value) << '\n';
        ind++;
        bloom_filter.Add(key);
    }
    out.close();
    in = std::ifstream(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("bad path: " + path);
    }
}

SSTable::SSTable(const size_t BLOCK_SIZE, const std::string& path, std::vector<SSTable>&& prev_level)
    : BLOCK_SIZE(BLOCK_SIZE), path(path), offsets(), bloom_filter() {
    size_t R = prev_level.size();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("bad path: " + path);
    }
    std::vector<bool> ended(R, false);
    std::vector<std::pair<std::string, std::string>> cur_val(R);
    std::vector<std::ifstream> fin(R);
    for (size_t i = 0; i < R; i++) {
        fin[i].open(prev_level[i].path, std::ios::binary);
        if (!fin[i]) {
            throw std::runtime_error("bad path: " + prev_level[i].path);
        }
    }
    for (size_t i = 0; i < R; i++) {
        if (!std::getline(fin[i], cur_val[i].first) || !std::getline(fin[i], cur_val[i].second)) {
            ended[i] = true;
        }
    }
    size_t ind = 0;
    while (true) {
        std::string min_key;
        bool first = true;
        for (size_t i = 0; i < R; i++) {
            if (!ended[i]) {
                if (first) {
                    min_key = cur_val[i].first;
                    first = false;
                } else if (cur_val[i].first < min_key) {
                    min_key = cur_val[i].first;
                }
            }
        }
        if (first) {
            break;
        }
        if (ind % BLOCK_SIZE == 0) {
            auto res = out.tellp();
            if (res == std::streampos(-1)) {
                throw std::runtime_error("tellp failed :(");
            }
            uint64_t offset = static_cast<uint64_t>(res);
            offsets.emplace_back(min_key, offset);
        }
        ind++;
        out << min_key << '\n';
        bloom_filter.Add(min_key);
        roaring::Roaring merged_bitmap{};
        for (size_t i = 0; i < R; i++) {
            while (!ended[i] && cur_val[i].first == min_key) {
                merged_bitmap |= StringToRoaring(cur_val[i].second);
                if (!std::getline(fin[i], cur_val[i].first) || !std::getline(fin[i], cur_val[i].second)) {
                    ended[i] = true;
                }
            }
        }
        out << RoaringToString(merged_bitmap) << '\n';
    }
    out.close();
    in = std::ifstream(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("bad path: " + path);
    }
}

std::vector<std::pair<std::string, roaring::Roaring>> SSTable::ReadBlock(uint64_t offset) {
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    std::vector<std::pair<std::string, roaring::Roaring>> data;
    data.reserve(BLOCK_SIZE);
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        std::string key, bitmap_str;
        if (!std::getline(in, key) || !std::getline(in, bitmap_str)) {
            break;
        }
        data.emplace_back(key, StringToRoaring(bitmap_str));
    }
    return data;
}

std::optional<uint64_t> SSTable::GetOffset(const std::string& key) {
    if (offsets.empty()) {
        return std::nullopt;
    }
    size_t l = 0;
    size_t r = offsets.size();
    while (r - l > 1) {
        size_t mid = (l + r) / 2;
        if (key < offsets[mid].first) {
            r = mid;
        } else {
            l = mid;
        }
    }
    return {offsets[l].second};
}

roaring::Roaring SSTable::Get(const std::string& key) {
    if (!bloom_filter.Contains(key)) {
        return roaring::Roaring{};
    }
    std::optional<uint64_t> offset = GetOffset(key);
    if (!offset.has_value()) {
        return roaring::Roaring{};
    }
    std::vector<std::pair<std::string, roaring::Roaring>> block = ReadBlock(offset.value());
    if (block.empty()) {
        return roaring::Roaring{};
    }
    size_t l = 0;
    size_t r = block.size();
    while (r - l > 1) {
        size_t mid = (l + r) / 2;
        if (key < block[mid].first) {
            r = mid;
        } else {
            l = mid;
        }
    }
    if (block[l].first == key) {
        return {block[l].second};
    } else {
        return roaring::Roaring{};
    }
}

roaring::Roaring SSTable::StringToRoaring(const std::string& bitmap_str) {
    roaring::Roaring res{};
    std::stringstream s(bitmap_str);
    uint32_t x;
    while (s >> x) {
        res.add(x);
    }
    return res;
}

std::string SSTable::RoaringToString(const roaring::Roaring& bitmap) {
    std::string res;
    bool first = true;
    for (uint32_t x : bitmap) {
        if (!first) {
            res += ' ';
        }
        first = false;
        res += std::to_string(x);
    }
    return res;
}

LSMTree::LSMTree(const size_t C, const size_t R, const size_t BLOCK_SIZE) : C(C), R(R), BLOCK_SIZE(BLOCK_SIZE) {
    std::filesystem::create_directories("./lsm-data");
}

void LSMTree::Add(const std::string& key, uint32_t value) {
    memtable[key].add(value);
    if (memtable.size() >= C) {
        if (sstables.empty()) {
            sstables.emplace_back();
        }
        sstables[0].emplace_back(BLOCK_SIZE, "./lsm-data/0_" + std::to_string(sstables[0].size()) + ".txt", memtable);
        size_t level = 0;
        while (sstables[level].size() >= R) {
            if (sstables.size() <= level + 1) {
                sstables.emplace_back();
            }
            std::string path =
                "./lsm-data/" + std::to_string(level + 1) + "_" + std::to_string(sstables[level + 1].size()) + ".txt";
            sstables[level + 1].emplace_back(BLOCK_SIZE, path, std::move(sstables[level]));
            sstables[level].clear();
            level++;
        }
        memtable.clear();
    }
}

roaring::Roaring LSMTree::Get(const std::string& key) {
    roaring::Roaring res{};
    auto it = memtable.find(key);
    if (it != memtable.end()) {
        res = it->second;
    }
    for (size_t level = 0; level < sstables.size(); level++) {
        for (size_t ind = sstables[level].size(); ind-- > 0;) {
            res |= sstables[level][ind].Get(key);
        }
    }
    return res;
}