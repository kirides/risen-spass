#pragma once

#include <string>

#include "game/abi.h"

// RAII owner of a Script.dll `Template` (derives from eCTemplateEntityProxy; docs/RE.md §9). Shared by the item
// helpers (spawn, inventory) and the template editing API. The destructor is skipped once ownership was handed to a
// by-value parameter (giveAway()).
namespace spass::game {

// Generous upper bound for sizeof(Template) (eCTemplateEntityProxy plus a few members).
struct alignas(16) TemplateStorage {
    unsigned char bytes[128] = {};
};

class TemplateObject {
public:
    explicit TemplateObject(const std::string& name) { sym().tpl_ctor_cstr(storage_.bytes, name.c_str()); }
    TemplateObject(const TemplateObject&) = delete;
    TemplateObject& operator=(const TemplateObject&) = delete;
    ~TemplateObject() {
        if (owned_) {
            sym().tpl_dtor(storage_.bytes);
        }
    }
    bool valid() const { return sym().tpl_is_valid(storage_.bytes); }
    const void* get() const { return storage_.bytes; }
    void* giveAway() {
        owned_ = false;
        return storage_.bytes;
    }

private:
    TemplateStorage storage_;
    bool owned_ = true;
};

}  // namespace spass::game
