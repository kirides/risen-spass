#pragma once

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string>
#include <type_traits>

#include "game/symbols.h"

namespace spass::game {

// Owning handle for the engine's bCString (verified: sizeof == 8, a single pointer to a ref-counted
// buffer, constructed/destroyed through SharedBase.dll exports). We never touch its internals.
class BString {
public:
    BString() { sym().bstring_ctor_cstr(storage_, ""); }
    explicit BString(const char* s) { sym().bstring_ctor_cstr(storage_, s); }
    explicit BString(const std::string& s) : BString(s.c_str()) {}
    BString(const BString&) = delete;
    BString& operator=(const BString&) = delete;
    ~BString() { sym().bstring_dtor(storage_); }

    const char* c_str() const { return sym().bstring_get_text(storage_); }
    std::string str() const { return c_str(); }

    // For passing as `const bCString&` / `bCString*` to engine functions, or as the hidden return slot.
    const void* get() const { return storage_; }
    void* raw() { return storage_; }

private:
    alignas(8) unsigned char storage_[8] = {};
};

// For a by-value bCString parameter: the callee destroys it, so this object deliberately has no
// destructor. Use ptr() as the argument and do not touch the string afterwards.
class ByValueString {
public:
    explicit ByValueString(const std::string& s) { sym().bstring_ctor_cstr(slot_, s.c_str()); }
    ByValueString(const ByValueString&) = delete;
    ByValueString& operator=(const ByValueString&) = delete;
    void* ptr() { return slot_; }

private:
    alignas(8) unsigned char slot_[8] = {};
};

// Storage for a bCString that the engine constructs for us (hidden return pointer). Pass ptr() as the
// return slot; call take() only after the call succeeded (it destroys the engine-made string).
class OutString {
public:
    void* ptr() { return slot_; }
    std::string take() {
        std::string s = sym().bstring_get_text(slot_);
        sym().bstring_dtor(slot_);
        return s;
    }

private:
    alignas(8) unsigned char slot_[8] = {};
};

// Same for bCUnicodeString.
class OutUString;

// Owning bCUnicodeString (UTF-16, `wchar_t` on Windows). Construct from UTF-8 with fromUtf8().
class UString {
public:
    UString() { sym().ustring_ctor_wide(storage_, L""); }
    explicit UString(const wchar_t* w) { sym().ustring_ctor_wide(storage_, w); }
    UString(const UString&) = delete;
    UString& operator=(const UString&) = delete;
    ~UString() { sym().ustring_dtor(storage_); }

    static std::wstring widen(const std::string& utf8);
    static std::string narrow(const wchar_t* w);

    explicit UString(const std::string& utf8) : UString(widen(utf8).c_str()) {}

    const wchar_t* c_str() const { return sym().ustring_get_text(storage_); }
    std::string utf8() const { return narrow(c_str()); }
    const void* get() const { return storage_; }
    void* raw() { return storage_; }

private:
    alignas(8) unsigned char storage_[8] = {};
};

class OutUString {
public:
    void* ptr() { return slot_; }
    std::string take() {
        std::string s = UString::narrow(sym().ustring_get_text(slot_));
        sym().ustring_dtor(slot_);
        return s;
    }

private:
    alignas(8) unsigned char slot_[8] = {};
};

static_assert(sizeof(BString) == 8, "bCString is a single pointer");

// `Entity` (Script.dll) is a single eCEntity* (verified: Entity::Entity() zeroes one qword,
// World::GetPlayer writes one pointer through the hidden return pointer). Member functions take the
// address of this holder as `this`.
struct EntityHolder {
    void* entity = nullptr;
};
static_assert(sizeof(EntityHolder) == 8);

// Runs `fn(ctx)` under SEH. Returns false if it raised an access violation or other hardware
// exception, which we prefer over taking the whole game down when a script API is called before the
// engine singletons exist or with a dead entity.
bool invokeGuarded(void (*fn)(void*), void* ctx, const char* file = nullptr, int line = 0);

// The call site (file:line) is noted in the crash breadcrumbs and in the log line of a fault caught here (src/common/crash.h).
template <class F>
bool guarded(F&& f, std::source_location where = std::source_location::current()) {
    return invokeGuarded([](void* p) { (*static_cast<std::remove_reference_t<F>*>(p))(); }, &f, where.file_name(), static_cast<int>(where.line()));
}

}  // namespace spass::game
