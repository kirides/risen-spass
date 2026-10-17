// The command palette (Ctrl+P): one search box over the project's objects, the game's templates, the panels and the editor's commands.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/app.h"

namespace mk {

class CommandPalette {
public:
    void open() { want_open_ = true; }
    void open_with(const std::string& query) {
        want_open_ = true;
        initial_query_ = query;
    }
    bool is_open() const { return open_; }
    // call once per frame, after the menus
    void draw(App& app);

    // one searchable thing (public for the tests of the matching)
    struct Entry {
        std::string kind;    // "Command", "Panel", "Items", "Shipped item", ...
        std::string label;   // the name
        std::string detail;  // the label text, owner, ...
        std::function<void(App&)> run;
    };
    // the entries for a query, best first; an empty query lists the commands and panels only
    static std::vector<const Entry*> search(const std::vector<Entry>& all, const std::string& query, size_t limit = 40);
    // every entry of the app's current state
    static std::vector<Entry> collect(App& app);
    // the match score of a query against a text (0 = no match; higher is better): every word of the query has to occur, earlier and at word starts is better
    static int score(const std::string& query, const std::string& text);

private:
    bool want_open_ = false, open_ = false, refocus_ = false;
    std::string query_, initial_query_;
    std::vector<Entry> all_;
    std::vector<const Entry*> shown_;
    int selected_ = 0;
    std::string last_query_ = "\x01";
};

}  // namespace mk
