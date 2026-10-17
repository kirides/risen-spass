#include "pack2mod/tab.h"

namespace p2m {

const std::string* TabTable::cell(const std::string& id, const std::string& column) const {
    const auto it = rows.find(id);
    if (it == rows.end()) return nullptr;
    const int c = column_index(column);
    if (c < 0 || static_cast<size_t>(c) >= it->second.size()) return nullptr;
    return &it->second[static_cast<size_t>(c)];
}

int TabTable::column_index(const std::string& column) const {
    for (size_t i = 1; i < columns.size(); ++i)
        if (columns[i] == column) return static_cast<int>(i - 1);
    return -1;
}

TabTable read_tab(const Bytes& b) {
    if (b.compare(0, 4, "TAB0") != 0) throw Error("not a TAB0 string table");
    size_t p = 16;
    const uint32_t ncols = rd32(b, p);
    p += 4;
    if (ncols == 0 || ncols > 64) throw Error("string table: implausible column count");
    TabTable t;
    std::vector<std::vector<std::string>> cols;
    for (uint32_t c = 0; c < ncols; ++c) {
        p += 3;
        const uint16_t nl = rd16(b, p);
        p += 2;
        if (p + 2u * nl > b.size()) throw Error("string table is cut off");
        t.columns.push_back(utf16le_to_utf8(b.data() + p, nl));
        p += 2u * nl;
        const uint32_t cnt = rd32(b, p);
        p += 4;
        std::vector<std::string> col;
        col.reserve(cnt);
        for (uint32_t i = 0; i < cnt; ++i) {
            const uint16_t n = rd16(b, p);
            p += 2;
            if (p + 2u * n > b.size()) throw Error("string table is cut off");
            col.push_back(utf16le_to_utf8(b.data() + p, n));
            p += 2u * n;
        }
        cols.push_back(std::move(col));
    }
    if (p != b.size()) throw Error("string table: " + std::to_string(b.size() - p) + " trailing bytes");
    for (size_t i = 0; i < cols[0].size(); ++i) {
        const std::string& id = cols[0][i];
        std::vector<std::string> cells;
        for (size_t c = 1; c < cols.size(); ++c) cells.push_back(i < cols[c].size() ? cols[c][i] : std::string());
        if (!t.rows.count(id)) t.ids.push_back(id);
        t.rows[id] = std::move(cells);
    }
    return t;
}

Bytes build_tab(const std::vector<std::string>& language_columns, const std::vector<std::pair<std::string, std::vector<std::string>>>& rows) {
    auto u16 = [](const std::string& s) {
        Bytes o;
        for (uint32_t c : utf8_decode(s)) {
            if (c >= 0x10000) {
                c -= 0x10000;
                wr16(o, static_cast<uint16_t>(0xD800 + (c >> 10)));
                wr16(o, static_cast<uint16_t>(0xDC00 + (c & 0x3FF)));
            } else {
                wr16(o, static_cast<uint16_t>(c));
            }
        }
        return o;
    };
    auto column = [&](const std::string& name, const std::vector<std::string>& cells) {
        Bytes o;
        o += std::string("\x01\x01\x00", 3);
        const Bytes nm = u16(name);
        wr16(o, static_cast<uint16_t>(nm.size() / 2));
        o += nm;
        wr32(o, static_cast<uint32_t>(cells.size()));
        for (const std::string& c : cells) {
            const Bytes u = u16(c);
            wr16(o, static_cast<uint16_t>(u.size() / 2));
            o += u;
        }
        return o;
    };
    Bytes out = "TAB0";
    wr16(out, 1);
    wr16(out, 1);
    out += Bytes(8, '\0');
    wr32(out, static_cast<uint32_t>(language_columns.size() + 1));
    std::vector<std::string> ids;
    for (const auto& r : rows) ids.push_back(r.first);
    out += column("ID", ids);
    for (size_t c = 0; c < language_columns.size(); ++c) {
        std::vector<std::string> cells;
        for (const auto& r : rows) cells.push_back(c < r.second.size() ? r.second[c] : std::string());
        out += column(language_columns[c], cells);
    }
    return out;
}

}  // namespace p2m
