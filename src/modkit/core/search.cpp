#include "core/search.h"

#include <algorithm>
#include <cctype>

namespace mk {

namespace {
std::string lower_ascii(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
}  // namespace

int search_score(const std::string& query, const std::string& text) {
    const std::string t = lower_ascii(text);
    int total = 0;
    size_t i = 0;
    bool any = false;
    while (i < query.size()) {
        while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i]))) ++i;
        if (i >= query.size()) break;
        size_t end = i;
        while (end < query.size() && !std::isspace(static_cast<unsigned char>(query[end]))) ++end;
        const std::string word = lower_ascii(query.substr(i, end - i));
        i = end;
        any = true;
        const size_t at = t.find(word);
        if (at == std::string::npos) return 0;
        int s = 100 - static_cast<int>(std::min<size_t>(at, 60));
        if (at == 0) s += 60;                                                        // the text starts with it
        else if (t[at - 1] == '_' || t[at - 1] == ' ' || t[at - 1] == '/') s += 30;  // a part of the name starts with it
        s += static_cast<int>(std::min<size_t>(word.size(), 20));
        total += s;
    }
    return any ? total : 0;
}

}  // namespace mk
