#pragma once

namespace spass::game {

// Creates (but does not enable) all engine hooks. Requires resolveSymbols() to have run.
bool installHooks();

}  // namespace spass::game
