// Minimal JSON reader (profiles). Objects keep the order of the file.
#pragma once

#include <string>

#include "pack2mod/lua_emit.h"

namespace p2m {

// throws Error with a position on malformed input
LVal parse_json(const std::string& text);

}  // namespace p2m
