#include "app/audio.h"

// miniaudio decodes WAV and MP3 itself; Ogg Vorbis through stb_vorbis, which it picks up when the header part is included first
#if defined(_MSC_VER)
#pragma warning(push, 0)  // third-party single-file libraries
#pragma warning(disable : 4701)  // reported at code generation, after the pop
#endif
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>
#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace mk {

struct AudioPlayer::Impl {
    ma_engine engine{};
    bool tried = false, engine_ok = false;
    ma_decoder decoder{};
    ma_sound sound{};
    bool loaded = false;
    std::string data;  // the decoder reads from it while the sound plays
    std::string key;

    void unload() {
        if (!loaded) return;
        ma_sound_uninit(&sound);
        ma_decoder_uninit(&decoder);
        loaded = false;
        key.clear();
        data.clear();
    }
};

AudioPlayer::AudioPlayer() : d_(std::make_unique<Impl>()) {}

AudioPlayer::~AudioPlayer() {
    d_->unload();
    if (d_->engine_ok) ma_engine_uninit(&d_->engine);
}

bool AudioPlayer::play(const std::string& key, std::string audio, std::string& error) {
    stop();
    if (!d_->tried) {
        d_->tried = true;
        d_->engine_ok = ma_engine_init(nullptr, &d_->engine) == MA_SUCCESS;
    }
    if (!d_->engine_ok) {
        error = "no audio output device could be opened";
        return false;
    }
    d_->data = std::move(audio);
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (ma_decoder_init_memory(d_->data.data(), d_->data.size(), &cfg, &d_->decoder) != MA_SUCCESS) {
        d_->data.clear();
        error = "the audio cannot be decoded";
        return false;
    }
    if (ma_sound_init_from_data_source(&d_->engine, &d_->decoder, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &d_->sound) != MA_SUCCESS) {
        ma_decoder_uninit(&d_->decoder);
        d_->data.clear();
        error = "the audio cannot be played";
        return false;
    }
    d_->loaded = true;
    d_->key = key;
    ma_sound_start(&d_->sound);
    return true;
}

void AudioPlayer::stop() { d_->unload(); }

bool AudioPlayer::playing() const { return d_->loaded && ma_sound_is_playing(&d_->sound) && !ma_sound_at_end(&d_->sound); }

const std::string& AudioPlayer::current() const {
    static const std::string none;
    return playing() ? d_->key : none;
}

float AudioPlayer::position() const {
    float s = 0;
    if (d_->loaded) ma_sound_get_cursor_in_seconds(&d_->sound, &s);
    return s;
}

float AudioPlayer::length() const {
    float s = 0;
    if (d_->loaded) ma_sound_get_length_in_seconds(&d_->sound, &s);
    return s;
}

}  // namespace mk
