#include "pack2mod/text.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "pack2mod/util.h"

namespace p2m {

std::string norm_text(const std::string& in) {
    std::string s = py_strip(in);
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') s = replace_all(s.substr(1, s.size() - 2), "''", "'");
    s = replace_all(s, "`", "'");
    s = replace_all(s, "\xC2\xB4", "'");
    return py_strip(s);
}

std::optional<std::string> norm_cell(const std::string& in) {
    std::string s = norm_text(in);
    if (s.find("''") != std::string::npos) {  // a lost leading quote, the rest doubled: "Aric''s sword'" for "Aric's sword"
        s = replace_all(s, "''", "'");
        if (!s.empty() && s.back() == '\'' && s.front() != '\'') s.pop_back();
    } else if (!s.empty() && s.front() == '\'' && std::count(s.begin(), s.end(), '\'') == 1) {
        s.erase(0, 1);
    }
    static const std::set<std::string> placeholders = {"", "-", "\xE2\x80\xA6", "\xEF\xBF\xBD", "\xC2\x85", "...", "?"};
    if (placeholders.count(s)) return std::nullopt;
    return s;
}

std::string csv_field(const std::string& s) {
    bool quote = s.empty() || s != py_strip(s);
    for (char c : s)
        if (c == ';' || c == '"' || c == '\r' || c == '\n' || c == '#') quote = true;
    if (!quote) return s;
    return "\"" + replace_all(s, "\"", "\"\"") + "\"";
}

// the part after the last "._" when it is "x" plus at least one letter a-z (case as given), else npos
static size_t xext_pos(const std::string& s, bool any_case) {
    const size_t pos = s.rfind("._");
    if (pos == std::string::npos) return std::string::npos;
    const std::string seg = s.substr(pos + 2);
    if (seg.size() < 2) return std::string::npos;
    if (!(seg[0] == 'x' || (any_case && seg[0] == 'X'))) return std::string::npos;
    for (size_t i = 1; i < seg.size(); ++i) {
        const char c = seg[i];
        if (!((c >= 'a' && c <= 'z') || (any_case && c >= 'A' && c <= 'Z'))) return std::string::npos;
    }
    return pos;
}

std::string norm_mesh(const std::string& s) {
    const size_t pos = xext_pos(s, false);
    if (pos == std::string::npos) return s;
    return s.substr(0, pos) + "." + s.substr(pos + 2);
}

std::string vec_text(const std::vector<double>& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) o += (i ? "; " : "") + fmt_g(v[i]);
    return o;
}

bool is_asset_name(const std::string& bare) { return xext_pos(bare, true) != std::string::npos; }

std::string make_mod_id(const std::string& folder_name) {
    std::string o;
    for (char c : folder_name) {
        if (std::isalnum(static_cast<unsigned char>(c))) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!o.empty() && o.back() != '_') o += '_';
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    if (o.empty() || std::isdigit(static_cast<unsigned char>(o[0]))) o = "mod_" + o;
    return o;
}

}  // namespace p2m
