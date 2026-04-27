#include <bits/stdc++.h>
#include <oleander/english_stem.h>

bool IsSeparator(unsigned char c) {
    return c == ' ' || c == '\n' || c == '\t' || c == '.' || c == ',' || c == ';' || c == ':';
}

class Tokenizer {
public:
    std::vector<std::string> operator()(const std::string& document) const {
        std::vector<std::string> tokens;
        std::string current_token;
        for (unsigned char c : document) {
            if (IsSeparator(c)) {
                if (!current_token.empty()) {
                    tokens.push_back(current_token);
                    current_token.clear();
                }
            } else {
                current_token += c;
            }
        }
        if (!current_token.empty()) {
            tokens.push_back(current_token);
        }
        return tokens;
    }
};

class TokenNormalizer {
private:
    stemming::english_stem<> english_stemmer;

public:
    std::string operator()(const std::string& token) {
        std::string lower_token = token;
        for (char& c : lower_token) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        std::wstring token_stemming;
        for (unsigned char c : lower_token) {
            token_stemming += static_cast<wchar_t>(c);
        }
        english_stemmer(token_stemming);
        std::string res_token;
        for (wchar_t wc : token_stemming) {
            res_token += static_cast<char>(wc);
        }
        return res_token;
    }

    std::vector<std::string> operator()(const std::vector<std::string>& tokens) {
        std::vector<std::string> result_tokens;
        result_tokens.reserve(tokens.size());
        for (const std::string& token : tokens) {
            std::string normalized_token = this->operator()(token);
            if (!normalized_token.empty()) {
                result_tokens.push_back(normalized_token);
            }
        }
        return result_tokens;
    }
};

struct PhraseSearchResult {
    uint32_t document_id;
    std::vector<uint32_t> start_positions;
};

class InvertedIndex {
public:
    using PostingList = std::map<uint32_t, std::vector<uint32_t>>;

private:
    Tokenizer tokenizer;
    TokenNormalizer normalizer;
    std::unordered_map<std::string, PostingList> index;

public:
    InvertedIndex() = default;
    ~InvertedIndex() = default;

    void AddDocument(uint32_t document_id, const std::string& document) {
        std::vector<std::string> tokens = normalizer(tokenizer(document));
        for (size_t i = 0; i < tokens.size(); i++) {
            index[tokens[i]][document_id].push_back(i);
        }
    }

    std::vector<uint32_t> SearchWord(const std::string& word) {
        std::string token = normalizer(word);
        if (token.empty()) {
            return {};
        }
        auto it = index.find(token);
        if (it == index.end()) {
            return {};
        }
        std::vector<uint32_t> res;
        for (const auto& [document_id, positions] : it->second) {
            res.push_back(document_id);
        }
        return res;
    }

    std::vector<PhraseSearchResult> SearchPhrase(const std::string& phrase) {
        std::vector<PhraseSearchResult> res;
        std::vector<std::string> tokens = normalizer(tokenizer(phrase));
        if (tokens.empty()) {
            return {};
        }
        std::vector<const PostingList*> postings(tokens.size());
        for (size_t i = 0; i < tokens.size(); i++) {
            auto it = index.find(tokens[i]);
            if (it == index.end()) {
                return {};
            }
            postings[i] = &it->second;
        }
        std::vector<PostingList::const_iterator> document_iterators(tokens.size());
        bool finished = false;
        for (size_t i = 0; i < tokens.size(); i++) {
            document_iterators[i] = postings[i]->begin();
            finished |= document_iterators[i] == postings[i]->end();
        }
        while (!finished) {
            uint32_t mx = 0;
            for (size_t i = 0; i < tokens.size(); i++) {
                if (document_iterators[i]->first > mx) {
                    mx = document_iterators[i]->first;
                }
            }
            bool is_mx = true;
            for (size_t i = 0; i < tokens.size(); i++) {
                while (document_iterators[i] != postings[i]->end() && document_iterators[i]->first < mx) {
                    document_iterators[i]++;
                }
                if (document_iterators[i] == postings[i]->end()) {
                    finished = true;
                    break;
                }
                is_mx &= document_iterators[i] != postings[i]->end() && document_iterators[i]->first == mx;
            }
            if (finished) {
                break;
            }
            if (!is_mx) {
                continue;
            }
            PhraseSearchResult document_res = SearchInDocument(mx, tokens);
            if (!document_res.start_positions.empty()) {
                res.push_back(document_res);
            }
            for (size_t i = 0; i < tokens.size(); i++) {
                document_iterators[i]++;
                if (document_iterators[i] == postings[i]->end()) {
                    finished = true;
                    break;
                }
            }
        }
        return res;
    }

private:
    PhraseSearchResult SearchInDocument(uint32_t document_id, const std::vector<std::string>& tokens) {
        PhraseSearchResult res{document_id, {}};
        std::vector<const std::vector<uint32_t>*> positions(tokens.size());
        for (size_t i = 0; i < tokens.size(); i++) {
            auto term_it = index.find(tokens[i]);
            if (term_it == index.end()) {
                return res;
            }
            auto doc_it = term_it->second.find(document_id);
            if (doc_it == term_it->second.end()) {
                return res;
            }
            if (doc_it->second.empty()) {
                return res;
            }
            positions[i] = &doc_it->second;
        }
        std::vector<uint32_t> ind(tokens.size());
        bool finished = false;
        while (!finished) {
            uint32_t mx = 0;
            for (size_t i = 0; i < tokens.size(); i++) {
                if ((*positions[i])[ind[i]] + (tokens.size() - i - 1) > mx) {
                    mx = (*positions[i])[ind[i]] + (tokens.size() - i - 1);
                }
            }
            bool is_mx = true;
            for (size_t i = 0; i < tokens.size(); i++) {
                while (ind[i] < positions[i]->size() && (*positions[i])[ind[i]] + (tokens.size() - i - 1) < mx) {
                    ind[i]++;
                }
                if (ind[i] == positions[i]->size()) {
                    finished = true;
                    break;
                }
                is_mx &= (*positions[i])[ind[i]] + (tokens.size() - i - 1) == mx;
            }
            if (finished) {
                break;
            }
            if (!is_mx) {
                continue;
            }
            if (mx + 1 >= tokens.size()) {
                res.start_positions.push_back(mx - (tokens.size() - 1));
            }
            for (size_t i = 0; i < tokens.size(); i++) {
                ind[i]++;
                if (ind[i] == positions[i]->size()) {
                    finished = true;
                    break;
                }
            }
        }
        return res;
    }
};

namespace io {
    void PrintWordSearchResult(const std::vector<uint32_t>& documents) {
        if (documents.empty()) {
            std::cout << "not found" << std::endl;
            return;
        }
        for (uint32_t document_id : documents) {
            std::cout << document_id << " ";
        }
        std::cout << std::endl;
    }

    void PrintPhraseSearchResult(const std::vector<PhraseSearchResult>& results) {
        if (results.empty()) {
            std::cout << "not found" << std::endl;
            return;
        }
        for (const PhraseSearchResult& result : results) {
            std::cout << "document_id =" << result.document_id << ",  start_positions = [";
            for (size_t i = 0; i < result.start_positions.size(); i++) {
                if (i > 0) {
                    std::cout << ", ";
                }
                std::cout << result.start_positions[i];
            }
            std::cout << "]" << std::endl;
        }
    }
};

int main() {
    InvertedIndex index;
    index.AddDocument(0, "cow is very nice animal");
    index.AddDocument(1, "cow eats banana");
    index.AddDocument(2, "monkey eats banana");
    index.AddDocument(3, "Does Cow Eat Bananas");
    io::PrintWordSearchResult(index.SearchWord("cow"));
    io::PrintPhraseSearchResult(index.SearchPhrase("cow eats"));
    io::PrintPhraseSearchResult(index.SearchPhrase("eats banana"));
}