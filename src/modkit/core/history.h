// Undo / redo for every edit of the project, without one command class per edit: the history keeps snapshots (the project file's text) and settles
// once per frame. A panel only calls touch() when it changed the model; while a widget is still being dragged or typed into, the app does not settle,
// so one drag is one undo step.
#pragma once

#include <deque>
#include <string>

#include "core/project.h"

namespace mk {

class History {
public:
    void reset(const Project& p);  // after new / open: forget everything, the project as it is now is "saved"
    void touch() { touched_ = true; }
    // takes the step when something changed since the last settle; returns whether a step was recorded
    bool settle(const Project& p);
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    bool undo(Project& p);
    bool redo(Project& p);
    bool dirty() const { return current_ != saved_; }
    void mark_saved() { saved_ = current_; }
    size_t undo_depth() const { return undo_.size(); }

private:
    static constexpr size_t kMaxSteps = 300;
    std::deque<std::string> undo_, redo_;
    std::string current_, saved_;
    bool touched_ = false;
};

}  // namespace mk
