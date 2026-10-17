// Reader for Risen's compiled string tables (compiled/strings/*.tab, "TAB0"; docs/RE.md section 43). Port of tools/g3tab.py.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "pack2mod/util.h"

namespace p2m {

struct TabTable {
    std::vector<std::string> columns;                      // [0] is "ID" (or "Key"), the others the language columns, e.g. English_Text
    std::vector<std::string> ids;                          // row ids in file order (a repeated id keeps its first position)
    std::unordered_map<std::string, std::vector<std::string>> rows;  // id -> cell per column[1..]

    bool has(const std::string& id) const { return rows.count(id) != 0; }
    // the cell of a row in a named column, or nullptr
    const std::string* cell(const std::string& id, const std::string& column) const;
    int column_index(const std::string& column) const;  // index into a row's cells, -1 if absent
};

TabTable read_tab(const Bytes& b);  // throws Error on a damaged table

// builds a table (tests): columns without the ID column, rows as id + cells
Bytes build_tab(const std::vector<std::string>& language_columns, const std::vector<std::pair<std::string, std::vector<std::string>>>& rows);

}  // namespace p2m
