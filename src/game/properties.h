#pragma once

#include <string>

#include <sol/sol.hpp>

namespace spass::game {

// Generic property access by name on any engine bCObjectBase, through bCAccessorProperty -- the mechanism the
// game's own editor uses. Values use the engine's text format: enums as "gEInfoCondType_General", numbers as
// "5", bools as "true"/"false", strings and localization keys as-is.
//
// propertySet returns false if the object has no property of that name. Both calls are SEH-guarded.
bool propertySet(void* object, const std::string& name, const std::string& value);
sol::optional<std::string> propertyGet(void* object, const std::string& name);

// A loc-string member (gCInfoLocString, gCQuestLocString, ...) derives from CFXLocString: { cached item*, key }.
// If the derived class has its own vtable first, the CFXLocString base subobject starts at +8. A first qword that
// points into a loaded module is a vtable (a cached item* is a heap pointer or null).
void* locStringBase(void* text);

}  // namespace spass::game
