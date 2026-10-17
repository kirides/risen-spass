#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

#include <spdlog/spdlog.h>
#include <windows.h>

#include "common/asset_names.h"
#include "common/paths.h"
#include "common/sound_file.h"
#include "common/trace.h"
#include "game/abi.h"
#include "game/asset_files.h"
#include "game/bindings.h"
#include "game/lua_entity.h"
#include "game/vfs_mount.h"
#include "hook/hook_manager.h"
#include "lua/runtime.h"

// Custom voice lines (docs/RE.md section 28, docs/LUA_API.md "Voice lines").
//
// How a spoken dialogue line finds its sound: the AI script `_AI_Say` asks Entity::GetSampleTitle(speaker, textKey) for the sample title
// `<textKey>_<NPC voice>_<voice language>_%_<speaker entity>.wav` and starts that sound (eCResource2::GetSound: the compiled file `...._xsnd`, found by its
// bare name in the flat name cache of everything mounted). The sound's length is the length of the line. We make the compiled sounds out of the audio files
// of a mod (probe + container, common/sound_file.h), write them below <spass>/generated/<mod>/voice, and hook GetSampleTitle: for a registered text key the
// title of the variant for the current voice language comes back instead of the engine's own composition.
//
// Natives (shared, risen.voice): language(), normalize(name), get(key), keys(), remove(key), title(key), last(), say(entity, key_or_sound[, opts]),
//   say_svm(entity, svm_id[, listener]), speaking(entity), stop(entity);  risen.assets.sound(name): what the engine and FMOD made of a loaded sound
// Per mod: risen.assets.import_sound(name, source), risen.voice.check/set/load_dir; risen.dialog.create learns `sound` (a Lua chunk below).

namespace spass::game {
namespace {

struct Entry {
    std::string mod;
    std::map<std::string, std::string> titles;  // language ("English", "default") -> raw sample title ("<base>.wav")
    std::map<std::string, std::string> files;   // language -> source description (path / "(data)")
    std::map<std::string, std::uint64_t> durations;
    int requests = 0;
};

std::mutex g_mutex;
std::map<std::string, Entry> g_entries;  // lower-case text key -> entry
struct Last {
    std::string key, title, language;
    bool valid = false;
} g_last;
int g_titleCalls = 0;

Hook<void* (*)(const void*, void*, void*)> g_sampleTitle;

constexpr std::uint64_t kFixedFileTime = 0x01D830B38AC56280ULL;  // makes the compiled files deterministic (same audio -> same bytes -> no rewrite)

std::string lowerKey(const std::string& s) { return sound::lowerAscii(s); }

std::string currentVoiceLanguage() {
    const Symbols& s = sym();
    if (s.loc_instance == nullptr || s.loc_get_voice_language == nullptr) {
        return {};
    }
    std::string out;
    guarded([&] {
        OutString o;
        s.loc_get_voice_language(s.loc_instance(), o.ptr());
        out = o.take();
    });
    return out;
}

void* detourSampleTitle(const void* self, void* ret, void* key) {
    std::string k;
    const Symbols& s = sym();
    guarded([&] { k = s.bstring_get_text(key); });
    void* r = g_sampleTitle.original(self, ret, key);  // the callee destroys `key`
    ++g_titleCalls;
    if (k.empty()) {
        return r;
    }
    std::string title, lang;
    {
        std::lock_guard lock(g_mutex);
        const auto it = g_entries.find(lowerKey(k));
        if (it != g_entries.end()) {
            lang = sound::pickLanguage(it->second.titles, currentVoiceLanguage());
            const auto t = it->second.titles.find(lang);
            if (t != it->second.titles.end()) {
                title = t->second;
                ++it->second.requests;
                g_last = Last{k, title, lang, true};
            }
        }
    }
    if (!title.empty()) {
        guarded([&] {
            s.bstring_dtor(ret);
            s.bstring_ctor_cstr(ret, title.c_str());
        });
        SPASS_TRACE(trace::Channel::Voice, "sample title: key '{}' -> custom '{}' ({})", k, title, lang);
    } else if (trace::enabled(trace::Channel::Voice)) {
        std::string orig;
        guarded([&] { orig = s.bstring_get_text(ret); });
        trace::emit(trace::Channel::Voice, "sample title: key '" + k + "' -> engine '" + orig + "'");
    }
    return r;
}

bool readFile(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// One audio source of a voice variant: a file of the mod (relative path) or bytes given directly.
struct Source {
    std::string path;  // relative to the mod directory (when data is empty)
    std::string data;
    bool hasData = false;
    std::string describe() const { return hasData ? std::string("(data)") : path; }
};

struct Loaded {
    Source src;
    std::string bytes;
    sound::Info info;
};

// Reads and probes a source; the reason (with the file) on failure.
bool loadSource(const std::filesystem::path& modDir, const Source& src, Loaded& out, std::string& error) {
    out.src = src;
    if (src.hasData) {
        out.bytes = src.data;
    } else {
        const auto clean = assets::cleanRelativePath(src.path);
        if (!clean) {
            error = "'" + src.path + "' is not a relative path inside the mod directory (no '..', no drive)";
            return false;
        }
        if (!readFile(modDir / *clean, out.bytes)) {
            error = "cannot read '" + src.path + "' (a file of the mod directory is expected)";
            return false;
        }
    }
    std::string why;
    const auto info = sound::probe(out.bytes, why);
    if (!info) {
        error = (src.hasData ? std::string("audio data") : "'" + src.path + "'") + ": " + why;
        return false;
    }
    out.info = *info;
    return true;
}

// Compiles `loaded` into <generated>/<mod>/voice/<base>._xsnd unless that file is already there with the same content.
bool installCompiled(const std::string& mod, const std::string& base, const Loaded& loaded, bool& written, std::string& error) {
    const std::string compiled = base + "._xsnd";
    const std::string content = sound::buildXsnd(loaded.bytes, loaded.info.durationMs, kFixedFileTime);
    const std::string rel = "voice/" + compiled;
    const std::filesystem::path target = paths::dataDir() / "generated" / mod / "voice" / compiled;
    std::string existing;
    written = false;
    if (readFile(target, existing) && existing == content) {
        // identical file already on disk (and mounted at startup, or by an earlier import in this process)
        bool mounted = false;
        for (const auto& m : assetMounts()) {
            mounted = mounted || (m.mod == mod && m.kind == "generated");
        }
        if (mounted) {
            return true;
        }
    }
    if (!writeGeneratedAsset(mod, rel, content, error)) {
        return false;
    }
    written = true;
    refreshAssetMounts();
    return true;
}

sol::table infoTable(sol::state_view L, const Loaded& l, const std::string& name, const std::string& compiled, bool written) {
    sol::table t = L.create_table();
    t["name"] = name;
    t["compiled"] = compiled;
    t["format"] = l.info.format;
    t["channels"] = l.info.channels;
    t["sample_rate"] = l.info.sampleRate;
    if (l.info.bitsPerSample > 0) {
        t["bits"] = l.info.bitsPerSample;
    }
    t["duration_ms"] = static_cast<double>(l.info.durationMs);
    t["bytes"] = static_cast<double>(l.bytes.size());
    t["written"] = written;
    return t;
}

using Pair = std::tuple<sol::object, sol::object>;

Pair failure(sol::state_view L, const std::string& why) {
    return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why)};
}

bool hasKey(const sol::table& t, const char* key) {
    const sol::object o = t[key];
    return o.valid() && o.get_type() != sol::type::lua_nil;
}

// A variant value from Lua: "path", { file = "path" } or { data = "bytes" }.
bool parseSource(const sol::object& v, Source& out, std::string& error) {
    if (v.is<std::string>()) {
        out.path = v.as<std::string>();
        return !out.path.empty() || (error = "empty file name", false);
    }
    if (v.is<sol::table>()) {
        const sol::table t = v.as<sol::table>();
        if (sol::optional<std::string> f = t["file"]; f) {
            out.path = *f;
            return true;
        }
        if (sol::optional<std::string> d = t["data"]; d) {
            out.data = *d;
            out.hasData = true;
            return true;
        }
    }
    error = "a sound is a file name (relative to the mod directory) or { data = <bytes> }";
    return false;
}

// `variants`: a source (default language) or { default = src, English = src, de = src, ... }.
bool parseVariants(const sol::object& v, std::map<std::string, Source>& out, std::string& error) {
    Source single;
    if (v.is<std::string>() || (v.is<sol::table>() && (hasKey(v.as<sol::table>(), "file") || hasKey(v.as<sol::table>(), "data")))) {
        if (!parseSource(v, single, error)) {
            return false;
        }
        out["default"] = single;
        return true;
    }
    if (!v.is<sol::table>()) {
        error = "expected a sound file name or a table { en = \"a.ogg\", de = \"b.ogg\", default = \"c.ogg\" }";
        return false;
    }
    for (const auto& kv : v.as<sol::table>()) {
        if (!kv.first.is<std::string>()) {
            error = "the keys of a variants table are language names (\"en\", \"German\", \"default\")";
            return false;
        }
        std::string lang = kv.first.as<std::string>();
        lang = (sound::lowerAscii(lang) == "default" || lang == "*") ? "default" : sound::normalizeLanguage(lang);
        Source src;
        if (!parseSource(kv.second, src, error)) {
            error = "language '" + lang + "': " + error;
            return false;
        }
        out[lang] = src;
    }
    if (out.empty()) {
        error = "no sound given";
        return false;
    }
    return true;
}

// ---- FMOD view of a loaded sound resource (verification aid) ---------------------------------------------------------------------------------

struct FmodApi {
    using GetLength = int (*)(void*, unsigned*, unsigned);
    using GetFormat = int (*)(void*, int*, int*, int*, int*);
    using GetDefaults = int (*)(void*, float*, int*);
    GetLength getLength = nullptr;
    GetFormat getFormat = nullptr;
    GetDefaults getDefaults = nullptr;
    bool tried = false;
};
FmodApi g_fmod;

void loadFmod() {
    if (g_fmod.tried) {
        return;
    }
    g_fmod.tried = true;
    const HMODULE m = GetModuleHandleW(L"fmod.dll");
    if (m == nullptr) {
        return;
    }
    g_fmod.getLength = reinterpret_cast<FmodApi::GetLength>(GetProcAddress(m, "FMOD5_Sound_GetLength"));
    g_fmod.getFormat = reinterpret_cast<FmodApi::GetFormat>(GetProcAddress(m, "FMOD5_Sound_GetFormat"));
    g_fmod.getDefaults = reinterpret_cast<FmodApi::GetDefaults>(GetProcAddress(m, "FMOD5_Sound_GetDefaults"));
}

// eCSoundResource2: Duration (ms) at +0x58, Flags at +0x5c, the eCAudioSound member at +0x70 whose FMOD::Sound* sits at +0x10 of it (RE.md section 28).
constexpr std::ptrdiff_t kSoundDuration = 0x58;
constexpr std::ptrdiff_t kSoundFlags = 0x5c;
constexpr std::ptrdiff_t kSoundFmod = 0x70 + 0x10;

Pair voiceSet(sol::state_view L, const std::string& id, const std::filesystem::path& modDir, const std::string& key, const sol::object& variants) {
    if (key.empty() || key.size() > 120) {
        return failure(L, "risen.voice.set: the key must be a text id of 1..120 characters");
    }
    std::map<std::string, Source> sources;
    std::string error;
    if (!parseVariants(variants, sources, error)) {
        return failure(L, "risen.voice.set: " + error);
    }
    std::map<std::string, Loaded> loaded;
    for (const auto& [lang, src] : sources) {  // validate everything before anything is written
        Loaded l;
        if (!loadSource(modDir, src, l, error)) {
            return failure(L, "risen.voice.set: " + (lang == "default" ? std::string() : lang + ": ") + error);
        }
        loaded[lang] = std::move(l);
    }
    Entry entry;
    entry.mod = id;
    sol::table result = L.create_table();
    sol::table vt = L.create_table();
    for (auto& [lang, l] : loaded) {
        const std::string base = sound::voiceBaseName(id, key, lang);
        bool written = false;
        if (!installCompiled(id, base, l, written, error)) {
            return failure(L, error);
        }
        entry.titles[lang] = base + ".wav";
        entry.files[lang] = l.src.describe();
        entry.durations[lang] = l.info.durationMs;
        vt[lang] = infoTable(L, l, base + ".wav", base + "._xsnd", written);
    }
    {
        std::lock_guard lock(g_mutex);
        Entry& slot = g_entries[lowerKey(key)];
        entry.requests = slot.requests;
        slot = entry;
    }
    result["key"] = key;
    result["variants"] = vt;
    SPASS_TRACE(trace::Channel::Voice, "voice.set '{}' by '{}': {} variant(s)", key, id, entry.titles.size());
    return {sol::make_object(L, result), sol::make_object(L, sol::lua_nil)};
}

void* routinePs(void* entity) {
    const Symbols& s = sym();
    if (entity == nullptr || s.ent_get_property_set == nullptr) {
        return nullptr;
    }
    void* ps = nullptr;
    guarded([&] {
        const BString cls("gCScriptRoutine_PS");
        ps = s.ent_get_property_set(entity, cls.get());
    });
    return ps;
}

}  // namespace

void installVoiceHooks() {
    const Symbols& s = sym();
    if (s.ent_get_sample_title == nullptr) {
        spdlog::warn("voice hook 'Entity::GetSampleTitle' unavailable: custom voice lines for Say commands will not play");
        return;
    }
    if (!g_sampleTitle.install("Entity::GetSampleTitle", s.ent_get_sample_title, &detourSampleTitle)) {
        spdlog::warn("voice hook 'Entity::GetSampleTitle' could not be installed");
    }
}

void bindVoiceNatives(sol::state_view lua, sol::table risen) {
    const Symbols& s = sym();
    sol::table voice = lua.create_table();

    voice.set_function("language", [] { return currentVoiceLanguage(); });
    voice.set_function("normalize", [](const std::string& name) { return sound::normalizeLanguage(name); });

    voice.set_function("get", [](sol::this_state ts, const std::string& key) -> sol::object {
        sol::state_view L(ts);
        std::lock_guard lock(g_mutex);
        const auto it = g_entries.find(lowerKey(key));
        if (it == g_entries.end()) {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::table t = L.create_table();
        t["key"] = key;
        t["mod"] = it->second.mod;
        t["requests"] = it->second.requests;
        sol::table variants = L.create_table();
        for (const auto& [lang, title] : it->second.titles) {
            sol::table v = L.create_table();
            v["title"] = title;
            v["compiled"] = title.substr(0, title.size() - 4) + "._xsnd";
            v["file"] = it->second.files[lang];
            v["duration_ms"] = static_cast<double>(it->second.durations[lang]);
            variants[lang] = v;
        }
        t["variants"] = variants;
        return sol::make_object(L, t);
    });

    voice.set_function("keys", [](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table t = L.create_table();
        std::lock_guard lock(g_mutex);
        int i = 1;
        for (const auto& [k, e] : g_entries) {
            t[i++] = k;
        }
        return t;
    });

    voice.set_function("remove", [](const std::string& key) {
        std::lock_guard lock(g_mutex);
        return g_entries.erase(lowerKey(key)) > 0;
    });

    // title(key) -> the raw sample title (`<name>.wav`) of the variant for the current voice language: what the engine is given instead of its own title.
    voice.set_function("title", [](sol::this_state ts, const std::string& key) -> sol::object {
        sol::state_view L(ts);
        std::lock_guard lock(g_mutex);
        const auto it = g_entries.find(lowerKey(key));
        if (it == g_entries.end()) {
            return sol::make_object(L, sol::lua_nil);
        }
        const std::string lang = sound::pickLanguage(it->second.titles, currentVoiceLanguage());
        const auto t = it->second.titles.find(lang);
        return t == it->second.titles.end() ? sol::make_object(L, sol::lua_nil) : sol::make_object(L, t->second);
    });

    // last() -> { key, title, language } of the last registered key the engine asked a sample title for (the proof that a spoken line used our sound).
    voice.set_function("last", [](sol::this_state ts) -> sol::object {
        sol::state_view L(ts);
        std::lock_guard lock(g_mutex);
        if (!g_last.valid) {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::table t = L.create_table();
        t["key"] = g_last.key;
        t["title"] = g_last.title;
        t["language"] = g_last.language;
        return sol::make_object(L, t);
    });
    voice.set_function("title_calls", [] { return g_titleCalls; });

    // say(entity, key_or_sound[, { type = n }]) -> bool: starts a sound as the entity's speech output (a "bark"): a registered voice key (current language) or the
    // raw name of any sound the engine can find (`Name.wav`). type: 0 NPC voice (default), 2 the player's voice, 3 comment/sound command.
    voice.set_function("say", [&s](const LuaEntity& e, const std::string& what, sol::optional<sol::table> opts) {
        if (s.rout_start_output == nullptr || what.empty()) {
            return false;
        }
        std::string name = what;
        {
            std::lock_guard lock(g_mutex);
            const auto it = g_entries.find(lowerKey(what));
            if (it != g_entries.end()) {
                const std::string lang = sound::pickLanguage(it->second.titles, currentVoiceLanguage());
                const auto t = it->second.titles.find(lang);
                if (t != it->second.titles.end()) {
                    name = t->second;
                    ++it->second.requests;
                }
            }
        }
        void* ent = ptr(e);
        void* ps = routinePs(ent);
        if (ps == nullptr) {
            return false;
        }
        int type = 0;
        if (opts) {
            type = opts->get_or("type", 0);
        }
        bool ok = false;
        guarded([&] {
            const BString n(name);
            ok = s.rout_start_output(ps, n.get(), type);
        });
        SPASS_TRACE(trace::Channel::Voice, "say: '{}' on '{}' -> {}", name, entityNameOf(ent), ok);
        return ok;
    });
    // say_svm(entity, svm_id[, listener]) -> bool: the engine's `Entity::StartSaySVM` (what the script helper behind `GamestartLoot` & co. calls): the speaker says
    // the standard voice message `svm_id` in its own voice (the `Voice` property of its gCNPC_PS; the sample `SVM_<voice>_<id>` of the voice language, the
    // plain "NPC" voice when that voice has none). The listener defaults to the speaker.
    voice.set_function("say_svm", [&s](const LuaEntity& e, const std::string& id, sol::optional<LuaEntity> listener) {
        if (s.ent_start_say_svm == nullptr || id.empty() || id.size() > 128) {
            return false;
        }
        void* ent = ptr(e);
        void* lis = listener ? ptr(*listener) : ent;
        if (ent == nullptr || lis == nullptr) {
            return false;
        }
        const bool ok = guarded([&] {
            EntityHolder self{ent};
            EntityHolder other{lis};
            ByValueString n(id);  // by value: the callee destroys it
            s.ent_start_say_svm(&self, &other, n.ptr(), false, false);
        });
        SPASS_TRACE(trace::Channel::Voice, "say_svm: '{}' on '{}' -> {}", id, entityNameOf(ent), ok);
        return ok;
    });
    voice.set_function("speaking", [&s](const LuaEntity& e) {
        bool r = false;
        if (s.rout_is_output_playing != nullptr) {
            void* ps = routinePs(ptr(e));
            if (ps != nullptr) {
                guarded([&] { r = s.rout_is_output_playing(ps); });
            }
        }
        return r;
    });
    voice.set_function("stop", [&s](const LuaEntity& e) {
        bool r = false;
        if (s.rout_stop_output != nullptr) {
            void* ps = routinePs(ptr(e));
            if (ps != nullptr) {
                guarded([&] { r = s.rout_stop_output(ps); });
            }
        }
        return r;
    });
    risen["voice"] = voice;

    // risen.assets.sound(name) -> { state, duration_ms, flags, fmod, fmod_length_ms, channels, bits, type } | nil
    // `state` is the resource state (3 = loaded); `fmod` is true when FMOD created a sound from the file (the audio can be played); the lengths compare
    // our Duration with what FMOD measured.
    sol::table assetsTable = risen["assets"];
    assetsTable.set_function("sound", [&s](sol::this_state ts, const std::string& name) -> sol::object {
        sol::state_view L(ts);
        loadFmod();
        refreshAssetMounts();
        if (s.res_get == nullptr || s.res_state == nullptr || name.empty()) {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::table t = L.create_table();
        bool have = false;
        guarded([&] {
            const BString n(name);
            void* res = s.res_get(n.get());
            if (res == nullptr) {
                return;
            }
            have = true;
            const int state = s.res_state(res);
            t["state"] = state;
            if (state == 3) {
                const auto* base = static_cast<const char*>(res);
                t["duration_ms"] = static_cast<double>(*reinterpret_cast<const std::uint32_t*>(base + kSoundDuration));
                t["flags"] = static_cast<int>(*reinterpret_cast<const std::uint8_t*>(base + kSoundFlags));
                void* snd = *reinterpret_cast<void* const*>(base + kSoundFmod);
                t["fmod"] = snd != nullptr;
                if (snd != nullptr && g_fmod.getLength != nullptr) {
                    unsigned ms = 0;
                    if (g_fmod.getLength(snd, &ms, 1 /*FMOD_TIMEUNIT_MS*/) == 0) {
                        t["fmod_length_ms"] = static_cast<double>(ms);
                    }
                    int type = 0, format = 0, channels = 0, bits = 0;
                    if (g_fmod.getFormat != nullptr && g_fmod.getFormat(snd, &type, &format, &channels, &bits) == 0) {
                        t["type"] = type;
                        t["format"] = format;
                        t["channels"] = channels;
                        t["bits"] = bits;
                    }
                    float freq = 0;
                    int prio = 0;
                    if (g_fmod.getDefaults != nullptr && g_fmod.getDefaults(snd, &freq, &prio) == 0) {
                        t["sample_rate"] = freq;
                    }
                }
            }
        });
        return have ? sol::make_object(L, t) : sol::make_object(L, sol::lua_nil);
    });
}

// Per-mod functions: they need the mod's id (generated directory) and directory (source files).
void bindVoiceModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod) {
    const std::string id = mod.id;
    const std::filesystem::path modDir = mod.dir;

    // ---- risen.assets.import_sound(name, source) ---------------------------------------------------------------------------------------------
    {
        sol::table shared = api["assets"];
        sol::table own = lua.create_table();
        sol::table meta = lua.create_table();
        meta["__index"] = shared;
        own[sol::metatable_key] = meta;
        own.set_function("import_sound", [id, modDir](sol::this_state ts, const std::string& name, const sol::object& sourceArg) -> Pair {
            sol::state_view L(ts);
            const auto compiled = assets::compiledName(name.find('.') == std::string::npos ? name + ".wav" : name);
            if (!compiled || assets::kindOf(name.find('.') == std::string::npos ? name + ".wav" : name) != assets::Kind::Sound) {
                return failure(L, "risen.assets.import_sound: '" + name + "' is not a sound name (a bare file name such as Spass_Hello.wav; the extension is only a label, the file is stored as ._xsnd)");
            }
            if (compiled->find_first_of("/\\") != std::string::npos) {
                return failure(L, "risen.assets.import_sound: the name must not contain a path");
            }
            Source src;
            std::string error;
            if (!parseSource(sourceArg, src, error)) {
                return failure(L, "risen.assets.import_sound: " + error);
            }
            Loaded loaded;
            if (!loadSource(modDir, src, loaded, error)) {
                return failure(L, "risen.assets.import_sound: " + error);
            }
            bool written = false;
            const std::string base = compiled->substr(0, compiled->size() - std::string("._xsnd").size());
            if (!installCompiled(id, base, loaded, written, error)) {
                return failure(L, error);
            }
            return {sol::make_object(L, infoTable(L, loaded, base + ".wav", *compiled, written)), sol::make_object(L, sol::lua_nil)};
        });
        // `shared` is the per-mod assets table bindAssetModApi made (write, mount, ...): it stays reachable through the metatable
        api["assets"] = own;
    }

    // ---- risen.voice.check / set / load_dir -------------------------------------------------------------------------------------------------
    sol::table shared = api["voice"];
    sol::table own = lua.create_table();
    sol::table meta = lua.create_table();
    meta["__index"] = shared;
    own[sol::metatable_key] = meta;

    // check(variants) -> true | nil, reason: reads and probes every file, changes nothing.
    own.set_function("check", [modDir](sol::this_state ts, const sol::object& variants) -> Pair {
        sol::state_view L(ts);
        std::map<std::string, Source> sources;
        std::string error;
        if (!parseVariants(variants, sources, error)) {
            return failure(L, "risen.voice.check: " + error);
        }
        for (const auto& [lang, src] : sources) {
            Loaded l;
            if (!loadSource(modDir, src, l, error)) {
                return failure(L, "risen.voice.check: " + (lang == "default" ? std::string() : lang + ": ") + error);
            }
        }
        return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
    });

    // set(key, variants) -> { key, variants = { [language] = info } } | nil, reason: compiles the sounds and answers `key` (a dialogue text key) with them.
    own.set_function("set", [id, modDir](sol::this_state ts, const std::string& key, const sol::object& variants) -> Pair {
        return voiceSet(sol::state_view(ts), id, modDir, key, variants);
    });

    // load_dir(dir) -> keys, files | nil, reason: dir/<language>/<key>.<ogg|wav|mp3> are the variants of <key>; dir/<key>.<ext> is the default.
    own.set_function("load_dir", [id, modDir](sol::this_state ts, const std::string& dir) -> Pair {
        sol::state_view L(ts);
        const auto clean = assets::cleanRelativePath(dir);
        if (!clean) {
            return failure(L, "risen.voice.load_dir: '" + dir + "' is not a relative directory of the mod");
        }
        const std::filesystem::path root = modDir / *clean;
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) {
            return failure(L, "risen.voice.load_dir: " + root.string() + " is not a directory");
        }
        const auto isAudio = [](const std::filesystem::path& p) {
            const std::string e = sound::lowerAscii(p.extension().string());
            return e == ".ogg" || e == ".wav" || e == ".mp3";
        };
        std::map<std::string, std::map<std::string, std::string>> byKey;  // key -> language -> relative path
        int files = 0;
        const auto add = [&](const std::filesystem::path& f, const std::string& lang, const std::string& relPrefix) {
            byKey[f.stem().string()][lang] = *clean + "/" + relPrefix + f.filename().string();
            ++files;
        };
        for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
            if (e.is_regular_file() && isAudio(e.path())) {
                add(e.path(), "default", "");
            } else if (e.is_directory()) {
                const std::string lang = sound::normalizeLanguage(e.path().filename().string());
                for (const auto& f : std::filesystem::directory_iterator(e.path(), ec)) {
                    if (f.is_regular_file() && isAudio(f.path())) {
                        add(f.path(), lang, e.path().filename().string() + "/");
                    }
                }
            }
        }
        int keys = 0;
        for (const auto& [key, variants] : byKey) {
            sol::table t = L.create_table();
            for (const auto& [lang, path] : variants) {
                t[lang] = path;
            }
            auto [res, why] = voiceSet(L, id, modDir, key, t);
            if (res.get_type() == sol::type::lua_nil) {
                return failure(L, why.is<std::string>() ? why.as<std::string>() : "risen.voice.load_dir: cannot register '" + key + "'");
            }
            ++keys;
        }
        return {sol::make_object(L, keys), sol::make_object(L, files)};
    });
    api["voice"] = own;

    // ---- risen.dialog.create learns `sound` (and per-language `text`) ----------------------------------------------------------------------------
    const char* chunk = R"LUA(
local risen, sharedDialog = ...
local own = setmetatable({}, { __index = sharedDialog })
risen.dialog = own

local function copy(t) local c = {} for k, v in pairs(t) do c[k] = v end return c end

-- the text of a language table: default, else English, else any
local function default_text(t)
    if t.default then return t.default end
    local en = t.English or t.en or t.EN
    if en then return en end
    local keys = {} for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys)
    return t[keys[1]]
end

-- risen.dialog.create(spec): like the shared function; a line may carry
--   sound = "voice/hello.ogg" | { en = "...", de = "..." , default = "..." } | { data = bytes }   (a Say line: spoken with subtitles; the
--           audio length is the length of the line; a SaySound line: SoundName from the audio, language chosen when the dialog is created)
--   text  = "plain" | { en = "...", de = "...", default = "..." }   (the subtitle per language)
function own.create(spec)
    if type(spec) ~= "table" or type(spec.lines) ~= "table" or type(spec.name) ~= "string" then
        return sharedDialog.create(spec)  -- the shared function reports what is wrong
    end
    local special = false
    for _, line in ipairs(spec.lines) do
        if type(line) == "table" and (line.sound ~= nil or type(line.text) == "table") then special = true end
    end
    if not special then return sharedDialog.create(spec) end

    -- 1. check our own fields before anything is created
    local lines, voices, texts = {}, {}, {}
    for n, line in ipairs(spec.lines) do
        local l = line
        if type(line) == "table" and (line.sound ~= nil or type(line.text) == "table") then
            l = copy(line)
            local key = ("%s_%d"):format(spec.name, n)
            if line.sound ~= nil then
                if line.script or line.log or (line.command and line.command ~= "Say" and line.command ~= "SaySound") then
                    local msg = ("dialog.create: line %d: 'sound' belongs on a Say or SaySound line"):format(n)
                    risen.log.warn(msg)
                    return nil, msg
                end
                local ok, why = risen.voice.check(line.sound)
                if not ok then
                    local msg = ("dialog.create: line %d: %s"):format(n, tostring(why))
                    risen.log.warn(msg)
                    return nil, msg
                end
                voices[n] = { key = key, sound = line.sound, say_sound = line.command == "SaySound" }
                l.sound = nil
            end
            if type(line.text) == "table" then
                local t = default_text(line.text)
                if type(t) ~= "string" then
                    local msg = ("dialog.create: line %d: 'text' needs a string per language"):format(n)
                    risen.log.warn(msg)
                    return nil, msg
                end
                texts[n] = { key = key, texts = line.text }
                l.text = t
            end
        end
        lines[n] = l
    end
    local copied = copy(spec)
    copied.lines = lines

    -- 2. the voices must be registered before the info exists (a SaySound line needs its SoundName at creation)
    for n, v in pairs(voices) do
        local res, why = risen.voice.set(v.key, v.sound)
        if not res then
            local msg = ("dialog.create: line %d: %s"):format(n, tostring(why))
            risen.log.warn(msg)
            return nil, msg
        end
        if v.say_sound then
            lines[n].SoundName = risen.voice.title(v.key)
            if lines[n].Speaker == nil then lines[n].Speaker = lines[n].speaker or "npc" end
            lines[n].speaker = nil
        end
    end

    local info, err = sharedDialog.create(copied)
    if not info then return nil, err end

    -- 3. subtitles per language
    for n, tx in pairs(texts) do
        for lang, text in pairs(tx.texts) do
            if lang ~= "default" and type(text) == "string" then
                risen.loc.set(tx.key, text, risen.voice.normalize(lang))
            end
        end
    end
    return info
end
)LUA";
    sol::load_result loaded = lua.load(chunk, "=risen.dialog(voice)");
    if (!loaded.valid()) {
        spdlog::error("lua: dialog voice wrapper failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result r = fn(api, api["dialog"]);
    if (!r.valid()) {
        spdlog::error("lua: dialog voice wrapper failed to run: {}", r.get<sol::error>().what());
    }
}

}  // namespace spass::game
