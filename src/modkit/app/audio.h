// Sound preview (docs/editor/REQUIREMENTS.md DLG-9, AST-2): plays one audio file at a time (Ogg Vorbis, WAV, MP3; core/assets.h unpacks the
// engine's ._xsnd) through miniaudio on the default output device. The device is opened on the first play, so screenshots and machines
// without audio never touch it.
#pragma once

#include <memory>
#include <string>

namespace mk {

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // starts `audio` (a whole file in memory) and stops what played before; `key` names it for current(). false + `error` when it cannot.
    bool play(const std::string& key, std::string audio, std::string& error);
    void stop();
    bool playing() const;
    const std::string& current() const;  // the key of what plays ("" when nothing does)
    float position() const;              // seconds
    float length() const;                // seconds

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mk
