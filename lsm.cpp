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

template <size_t BLOCK_SIZE, size_t R>
class SSTable {
private:
    std::string path;
    std::ifstream in;

    std::vector<std::pair<std::string, uint64_t>> offsets;
    BloomFilter<7, 1000000> bloom_filter;

public:
    SSTable(const std::string& path, const std::map<std::string, std::string>& data) :
        path(path),
        offsets(),
        bloom_filter() {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        int ind = 0;
        for (const auto& [key, value] : data) {
            if (ind % BLOCK_SIZE == 0) {
                uint64_t offset = out.tellp();
                offsets.emplace_back(key, offset);
            }
            out << key << '\n' << value << '\n';
            ind++;
            bloom_filter.Add(key);
        }
        out.close();
        in = std::ifstream(path, std::ios::binary);
    }

    ~SSTable() {
        in.close();
    }

    SSTable(const std::string& path, std::array<SSTable, R>&& prev_level) :
        path(path),
        offsets(),
        bloom_filter() {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        std::array<bool, R> ended{};
        std::array<std::pair<std::string, std::string>, R> cur_val;
        std::array<std::ifstream, R> fin;
        for (size_t i = 0; i < R; i++) {
            fin[i].open(prev_level[i].path, std::ios::binary);
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
                uint64_t offset = out.tellp();
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
    }

    SSTable(const SSTable&) = delete;
    SSTable& operator=(const SSTable&) = delete;
    SSTable(SSTable&&) noexcept = default;
    SSTable& operator=(SSTable&&) noexcept = default;

    std::vector<std::pair<std::string, std::string>> ReadBlock(uint64_t offset) {
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        std::vector<std::pair<std::string, std::string>> data(BLOCK_SIZE);
        for (size_t i = 0; i < BLOCK_SIZE; i++) {
            in >> data[i].first >> data[i].second;
        }
        return data;
    }

    uint64_t GetOffset(const std::string& key) {
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
        return offsets[l].second;
    }

    std::string Get(const std::string& key) {
        std::vector<std::pair<std::string, std::string>> block = ReadBlock(GetOffset(key));
        size_t l = 0;
        size_t r = BLOCK_SIZE;
        while (r - l > 1) {
            size_t mid = (l + r) / 2;
            if (key < block[mid].first) {
                r = mid;
            } else {
                l = mid;
            }
        }
        return block[l].second;
    }
};

class LSMTree {

};


int main() {

}