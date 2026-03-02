#include <bits/stdc++.h>
#include <roaring/roaring.hh>
#include <oleander/english_stem.h>
#include <lsm.h>

std::string RoaringBitmapToString(const roaring::Roaring& bitmap) {
    std::string res;
    bool first = true;
    for (uint32_t x : bitmap) {
        if (!first) {
            res += " ";
        }
        first = false;
        res += std::to_string(x);
    }
    return res;
}

bool IsFormulaBlankSymbol(unsigned char c) {
    return c == ' ' || c == '\n' || c == '\t';
}

bool IsSeparator(unsigned char c) {
    return c == ' ' || c == '\n' || c == '\t' || c == '.' || c == ',' || c == ';' || c == ':';
}

class Tokenizer {
public:
    std::vector<std::string> operator()(const std::string& document) {
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
    const std::unordered_set<std::string> stop_words = {
        "a", "an", "the", "is", "are", "was", "were",
        "in", "on", "at", "of", "for", "to", "by"
    };

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
        if (stop_words.find(lower_token) != stop_words.end()) {
            return "";
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

enum class QueryTokenType {
    WORD, AND, OR, NOT, LBRACKET, RBRACKET, END
};

struct QueryToken {
    QueryTokenType type;
    std::string word;
};

class InvertedIndex;

class QueryCalculator {
private:
    InvertedIndex& index;
    std::string formula;
    size_t pos;

public:
    QueryCalculator(InvertedIndex& index) : index(index), formula(), pos(0) {}
    roaring::Roaring operator()(const std::string& formula);

private:
    QueryToken GetToken();
    roaring::Roaring CalcOR();
    roaring::Roaring CalcAND();
    roaring::Roaring CalcNOT();
    roaring::Roaring CalcBRACKET();

    static bool IsSpecialFormulaSymbol(unsigned char c) {
        return c == '(' || c == ')' || c == '&' || c == '|' || c == '!';
    }
};

class InvertedIndex {
private:
    Tokenizer tokenizer;
    TokenNormalizer normalizer;
    QueryCalculator query_calc;
    LSMTree index;
    roaring::Roaring all_document_set;

public:
    InvertedIndex() : tokenizer(), normalizer(), query_calc(*this), index(10, 3, 5), all_document_set() {}

    void AddDocument(uint32_t document_id, const std::string& document) {
        std::vector<std::string> tokens = normalizer(tokenizer(document));
        std::sort(tokens.begin(), tokens.end());
        tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
        for (const std::string& token : tokens) {
            index.Add(token, document_id);
        }
        all_document_set.add(document_id);
    }

    roaring::Roaring SearchWord(const std::string& word) {
        std::string normalized_token = normalizer(word);
        if (normalized_token.empty()) {
            return roaring::Roaring{};
        }
        return index.Get(normalized_token);
    }

    roaring::Roaring SearchFormula(const std::string& formula) {
        return query_calc(formula);
    }

    const roaring::Roaring& GetAllDocumentSet() const {
        return all_document_set;
    }
};

roaring::Roaring QueryCalculator::operator()(const std::string& formula) {
    this->formula = formula;
    pos = 0;
    roaring::Roaring res = CalcOR();
    if (GetToken().type == QueryTokenType::END) {
        return res;
    }
    throw std::runtime_error("bad formula");
}

QueryToken QueryCalculator::GetToken() {
    while (pos < formula.size() && IsFormulaBlankSymbol(formula[pos])) {
        pos++;
    }
    if (pos == formula.size()) {
        return {QueryTokenType::END, ""};
    }
    if (formula[pos] == '(') {
        pos++;
        return {QueryTokenType::LBRACKET, ""};
    } else if (formula[pos] == ')') {
        pos++;
        return {QueryTokenType::RBRACKET, ""};
    } else if (formula[pos] == '&') {
        pos++;
        return {QueryTokenType::AND, ""};
    } else if (formula[pos] == '|') {
        pos++;
        return {QueryTokenType::OR, ""};
    } else if (formula[pos] == '!') {
        pos++;
        return {QueryTokenType::NOT, ""};
    } else {
        std::string word;
        while (pos < formula.size() && !IsFormulaBlankSymbol(formula[pos]) && !IsSpecialFormulaSymbol(formula[pos])) {
            word += formula[pos];
            pos++;
        }
        return {QueryTokenType::WORD, word};
    }
}

roaring::Roaring QueryCalculator::CalcOR() {
    roaring::Roaring res = CalcAND();
    size_t start_pos = pos;
    while (GetToken().type == QueryTokenType::OR) {
        roaring::Roaring operand = CalcAND();
        res |= operand;
        start_pos = pos;
    }
    pos = start_pos;
    return res;
}

roaring::Roaring QueryCalculator::CalcAND() {
    roaring::Roaring res = CalcNOT();
    size_t start_pos = pos;
    while (GetToken().type == QueryTokenType::AND) {
        roaring::Roaring operand = CalcNOT();
        res &= operand;
        start_pos = pos;
    }
    pos = start_pos;
    return res;
}

roaring::Roaring QueryCalculator::CalcNOT() {
    size_t start_pos = pos;
    QueryToken token = GetToken();
    if (token.type == QueryTokenType::NOT) {
        const roaring::Roaring& all_document_set = index.GetAllDocumentSet();
        roaring::Roaring res = CalcNOT();
        return all_document_set - res;
    }
    pos = start_pos;
    return CalcBRACKET();
}

roaring::Roaring QueryCalculator::CalcBRACKET() {
    QueryToken token = GetToken();
    if (token.type == QueryTokenType::LBRACKET) {
        roaring::Roaring res = CalcOR();
        if (GetToken().type != QueryTokenType::RBRACKET) {
            throw std::runtime_error("bad formula: no )");
        }
        return res;
    }
    if (token.type == QueryTokenType::WORD) {
        return index.SearchWord(token.word);
    }
    throw std::runtime_error("bad formula");
}

int main() {
    {
        std::cout << "TEST INVERTED INDEX 1" << std::endl;
        InvertedIndex index{};
        index.AddDocument(0, "mother father cow banana");
        index.AddDocument(1, "cow black daddy");
        index.AddDocument(2, "and or to red green white run rabbit queue terms cow");
        index.AddDocument(3, "water melon watermelon horse pig big rabbit frog dog cats runnners");
        std::cout << RoaringBitmapToString(index.SearchFormula("((cow & cow & !water) | daddy) & !horse & (black | mother) & (father | black | green | !cow)")) << std::endl;
    }
    {
        std::cout << "TEST INVERTED INDEX 2" << std::endl;
        InvertedIndex index{};
        index.AddDocument(0, "apple orange banana");
        index.AddDocument(1, "banana apple");
        index.AddDocument(2, "grape orange apple");
        index.AddDocument(3, "mango banana orange");
        std::cout << RoaringBitmapToString(index.SearchFormula("apple & !banana")) << std::endl;
    }
    {
        std::cout << "TEST NORMALIZER" << std::endl;
        TokenNormalizer normalizer;
        const std::vector<std::string> words = {
            "run",
            "running",
            "runnner",
            "ran",
            "cow",
            "cows",
            "cowboy",
            "bad",
            "worse"
        };
        for (const std::string& word : words) {
            std::cout << word << ' ' << normalizer(word) << std::endl;
        }
    }
}