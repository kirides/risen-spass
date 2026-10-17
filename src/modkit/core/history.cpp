#include "core/history.h"

namespace mk {

void History::reset(const Project& p) {
    undo_.clear();
    redo_.clear();
    current_ = saved_ = dump_project(p);
    touched_ = false;
}

bool History::settle(const Project& p) {
    if (!touched_) return false;
    touched_ = false;
    std::string now = dump_project(p);
    if (now == current_) return false;
    undo_.push_back(std::move(current_));
    if (undo_.size() > kMaxSteps) undo_.pop_front();
    redo_.clear();
    current_ = std::move(now);
    return true;
}

bool History::undo(Project& p) {
    if (undo_.empty()) return false;
    std::string prev = std::move(undo_.back());
    undo_.pop_back();
    p = project_from_json(json::parse(prev));
    redo_.push_back(std::move(current_));
    current_ = std::move(prev);
    touched_ = false;
    return true;
}

bool History::redo(Project& p) {
    if (redo_.empty()) return false;
    std::string next = std::move(redo_.back());
    redo_.pop_back();
    p = project_from_json(json::parse(next));
    undo_.push_back(std::move(current_));
    current_ = std::move(next);
    touched_ = false;
    return true;
}

}  // namespace mk
