#include <bits/stdc++.h>

template <uint64_t K, size_t N>
class BloomFilter {
private:
    std::bitset<N> arr;

public:
    BloomFilter() : arr(0) {}

    void Add(const std::string& s) {
        uint64_t state = string_hash(s);
        for (size_t i = 0; i < K; i++) {
            arr[prng(state) % N] = 1;
        }
    }

    bool Contains(const std::string& s) const {
        uint64_t state = string_hash(s);
        for (size_t i = 0; i < K; i++) {
            if (!arr[prng(state) % N]) {
                return false;
            }
        }
        return true;
    }

private:
    static uint64_t string_hash(const std::string& s) { // djb2
        uint64_t hash = 5381;
        for (unsigned char c : s) {
            hash = ((hash << 5) + hash) + static_cast<uint64_t>(c);
        }
        return hash;
    }

    static uint64_t prng(uint64_t& state) { // splitmix64 
        state += 0x9e3779b97f4a7c15ULL;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    } 
};

class SSTable {
private:
    const size_t BLOCK_SIZE;
    std::string path;
    std::ifstream in;

    std::vector<std::pair<std::string, uint64_t>> offsets;
    BloomFilter<7, 1000000> bloom_filter;

public:
    SSTable(const size_t BLOCK_SIZE, const std::string& path, const std::map<std::string, std::string>& data) :
        BLOCK_SIZE(BLOCK_SIZE),
        path(path),
        offsets(),
        bloom_filter() {
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
            out << key << '\n' << value << '\n';
            ind++;
            bloom_filter.Add(key);
        }
        out.close();
        in = std::ifstream(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("bad path: " + path);
        }
    }

    SSTable(const size_t BLOCK_SIZE, const std::string& path, std::vector<SSTable>&& prev_level) :
        BLOCK_SIZE(BLOCK_SIZE),
        path(path),
        offsets(),
        bloom_filter() {
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
            bool first = true;
            size_t mn_ind = 0;
            std::pair<std::string, std::string> mn{};
            for (size_t i = R; i-- > 0;) {
                if (!ended[i]) {
                    if (first) {
                        mn = cur_val[i];
                        mn_ind = i;
                        first = false;
                    } else if (cur_val[i].first < mn.first) {
                        mn = cur_val[i];
                        mn_ind = i;
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
                offsets.emplace_back(mn.first, offset);
            }
            ind++;
            out << mn.first << '\n' << mn.second << '\n';
            bloom_filter.Add(mn.first);
            for (size_t i = 0; i < R; i++) {
                while (!ended[i] && cur_val[i].first == mn.first) {
                    if (!std::getline(fin[i], cur_val[i].first) || !std::getline(fin[i], cur_val[i].second)) {
                        ended[i] = true;
                    }
                }
            }
        }
        out.close();
        in = std::ifstream(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("bad path: " + path);
        }
    }

    SSTable(const SSTable&) = delete;
    SSTable& operator=(const SSTable&) = delete;
    SSTable(SSTable&&) noexcept = default;
    SSTable& operator=(SSTable&&) noexcept = delete;

    std::vector<std::pair<std::string, std::string>> ReadBlock(uint64_t offset) {
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        std::vector<std::pair<std::string, std::string>> data;
        data.reserve(BLOCK_SIZE);
        for (size_t i = 0; i < BLOCK_SIZE; i++) {
            std::pair<std::string, std::string> new_pair;
            if (!std::getline(in, new_pair.first) || !std::getline(in, new_pair.second)) {
                break;
            }
            data.push_back(new_pair);
        }
        return data;
    }

    std::optional<uint64_t> GetOffset(const std::string& key) {
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

    std::optional<std::string> Get(const std::string& key) {
        if (!bloom_filter.Contains(key)) {
            return std::nullopt;
        }
        std::optional<uint64_t> offset = GetOffset(key);
        if (!offset.has_value()) {
            return std::nullopt;
        }
        std::vector<std::pair<std::string, std::string>> block = ReadBlock(offset.value());
        if (block.empty()) {
            return std::nullopt;
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
            return std::nullopt;
        }
    }
};


class LSMTree {
private:
    const size_t C; // max size of the memtable
    const size_t R; // max number of sstables on one level
    const size_t BLOCK_SIZE;
    std::map<std::string, std::string> memtable;
    std::vector<std::vector<SSTable>> sstables;

public:
    LSMTree(const size_t C, const size_t R, const size_t BLOCK_SIZE) :
        C(C),
        R(R),
        BLOCK_SIZE(BLOCK_SIZE) {}
    
    void Add(const std::string& key, const std::string& value) {
        memtable[key] = value;
        if (memtable.size() >= C) {
            if (sstables.empty()) {
                sstables.emplace_back();
            }
            sstables[0].emplace_back(BLOCK_SIZE, "./0_" + std::to_string(sstables[0].size()) + ".txt", memtable);
            size_t level = 0;
            while (sstables[level].size() >= R) {
                if (sstables.size() <= level + 1) {
                    sstables.emplace_back();
                }
                std::string path = "./" + std::to_string(level + 1) + "_" + std::to_string(sstables[level + 1].size()) + ".txt";
                sstables[level + 1].emplace_back(BLOCK_SIZE, path, std::move(sstables[level]));
                sstables[level].clear();
                level++;
            }
            memtable.clear();
        }
    }

    std::optional<std::string> Get(const std::string& key) {
        auto it = memtable.find(key);
        if (it != memtable.end()) {
            return it->second;
        }
        for (size_t level = 0; level < sstables.size(); level++) {
            for (size_t ind = sstables[level].size(); ind-- > 0;) {
                std::optional<std::string> res = sstables[level][ind].Get(key);
                if (res.has_value()) {
                    return res;
                }
            }
        }
        return std::nullopt;
    }
};

std::mt19937 rnd(97);

std::string GenString(size_t length) {
    std::string s;
    for (size_t i = 0; i < length; i++) {
        s.push_back('a' + rnd() % 26);
    }
    return s;
}


int main() {
    LSMTree lsm(100, 3, 50);
    std::mt19937 rnd(97);

    const size_t STRING_LENGTH = 10;
    std::vector<std::pair<std::string, std::string>> queries;
    for (size_t i = 0; i < 10000; i++) {
        queries.emplace_back(GenString(STRING_LENGTH), GenString(STRING_LENGTH));
    }
    for (size_t i = 0; i < 1000; i++) {
        lsm.Add(queries[i].first, queries[i].second);
    }
    for (size_t i = 0; i < 1000; i++) {
        auto res = lsm.Get(queries[i].first);
        if (!res.has_value() && res.value() != queries[i].second) {
            std::cout << "NOOOOO... -- 1000 add + 1000 get" << std::endl;
        }
    }
    for (int i = 1000; i < 2000; i++) {
        if (lsm.Get(queries[i].first).has_value()) {
            std::cout << "NOOOOO... -- get non-existed key" << std::endl;
        }
    }
    for (int i = 1000; i < 2000; i++) {
        lsm.Add(queries[i].first, queries[i].second);
        size_t query_ind = rnd() % (i + 1);
        auto res = lsm.Get(queries[query_ind].first);
        if (!res.has_value() && res.value() != queries[query_ind].second) {
            std::cout << "NOOOOO... -- add + random get" << std::endl;
        }
    }
}