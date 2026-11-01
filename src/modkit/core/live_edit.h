// The decision of the editor's live edit (stage E35c): when does a change of the project go to the running game? A short pause after the last change, only
// while a game is connected and no reload is still on its way; changes made while nothing listens are not sent (they go with the next Play).
#pragma once

#include <cstddef>

namespace mk {

class LiveEditGate {
public:
    static constexpr double kPause = 0.8;  // seconds of quiet after the last change

    // Called every frame with the history's revision; true = export and reload now.
    bool step(bool enabled, bool connected, bool busy, size_t revision, double now) {
        if (!enabled || !connected) {
            seen_ = revision;
            pending_ = -1;
            return false;
        }
        if (revision != seen_) {
            seen_ = revision;
            pending_ = now;  // every further change moves the moment back: one reload after a burst of edits
        }
        if (pending_ < 0 || now - pending_ < kPause || busy) return false;
        pending_ = -1;
        return true;
    }
    bool pending() const { return pending_ >= 0; }

private:
    size_t seen_ = 0;
    double pending_ = -1;
};

}  // namespace mk
