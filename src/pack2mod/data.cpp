// Archive layers, discovery of a mod's archives, loading of the baseline and the templates.
#include <algorithm>
#include <filesystem>

#include "pack2mod/internal.h"

namespace fs = std::filesystem;

namespace p2m {

void Layered::add(const std::string& label, Pak* pak) {
    for (const PakEntry& e : pak->entries()) {
        const std::string key = lower(bare_name(e.path));
        LFile f{label, pak, &e};
        if (!files.count(key)) order.push_back(key);
        files[key] = f;
        history[key].push_back(f);
    }
}

std::vector<std::string> Layered::sorted_keys() const {
    std::vector<std::string> k = order;
    std::sort(k.begin(), k.end());
    return k;
}

std::string ArcRef::label() const {
    char buf[16];
    if (number < 0) return kind + "/" + base + ".pak";
    std::snprintf(buf, sizeof buf, ".p%02d", number);
    return kind + "/" + base + buf;
}

std::vector<std::string> Data::mod_sorted() const {
    std::vector<std::string> v = mod_order;
    std::sort(v.begin(), v.end());
    return v;
}
std::vector<std::string> Data::van_sorted() const {
    std::vector<std::string> v = van_order;
    std::sort(v.begin(), v.end());
    return v;
}

std::unordered_map<std::string, std::string> known_names(const Data& d) {
    std::unordered_map<std::string, std::string> k;
    for (const std::string& n : d.van_order) k[lower(n)] = n;
    for (const std::string& n : d.mod_order) k[lower(n)] = n;
    return k;
}

}  // namespace p2m
