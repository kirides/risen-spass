#pragma once

#include <string>

namespace spass::game {

// Makes the named single-instance mutex of the engine ("GenomeMutex", created by SharedBase's g_CreateMutex with
// CreateMutexA) unique per instance: "GenomeMutex" -> "GenomeMutex_<id>". Lets several Risen.exe from separate test
// copies run at the same time (config.toml [debug] instance). Call before enableAll().
bool installInstanceMutex(const std::string& id);

}  // namespace spass::game
