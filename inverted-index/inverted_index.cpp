#include <bits/stdc++.h>
#include <roaring/roaring.hh>

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
public:
    std::string operator()(const std::string& token) {
        std::string res_token = token;
        for (char& c : res_token) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return res_token;
    }

    std::vector<std::string> operator()(const std::vector<std::string>& tokens) {
        std::vector<std::string> result_tokens;
        result_tokens.reserve(tokens.size());
        for (const std::string& token : tokens) {
            result_tokens.push_back(this->operator()(token));
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
    std::map<std::string, roaring::Roaring> index;
    roaring::Roaring all_document_set;

public:
    InvertedIndex() : tokenizer(), normalizer(), query_calc(*this), index(), all_document_set() {}

    void AddDocument(uint32_t document_id, const std::string& document) {
        std::vector<std::string> tokens = normalizer(tokenizer(document));
        std::sort(tokens.begin(), tokens.end());
        tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
        for (const std::string& token : tokens) {
            index[token].add(document_id);
        }
        all_document_set.add(document_id);
    }

    roaring::Roaring SearchWord(const std::string& word) {
        auto it = index.find(normalizer(word));
        if (it == index.end()) {
            return roaring::Roaring{};
        }
        return it->second;
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
    InvertedIndex index{};
    index.AddDocument(0, "aaa aaa bbb");
    index.AddDocument(1, "aaa a aa b");
    std::cout << RoaringBitmapToString(index.SearchWord("aaa")) << std::endl;;
    std::cout << RoaringBitmapToString(index.SearchFormula("aaa & bbb")) << std::endl;
    std::cout << RoaringBitmapToString(index.SearchFormula("(a | bbb) & !c & (aa | bbb)")) << std::endl;;
    std::cout << RoaringBitmapToString(index.SearchFormula("(a | bbb) & !c & (aa | bbb) & (w | ww | www | !aaa)")) << std::endl;
}