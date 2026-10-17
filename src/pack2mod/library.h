// Library files that are copied as loose files: the compiled combat table and weather files (docs/RE.md sections 26, 45).
#pragma once

#include <string>
#include <vector>

#include "pack2mod/util.h"

namespace p2m {

struct CombatVariant {
    std::string volume;  // "compiled/library.p01"
    std::string file;    // "variants/combat_p01.bin"
    Bytes data;
    std::vector<std::pair<std::string, std::vector<std::string>>> changed;  // species -> what differs from the baseline
};
struct CombatResult {
    bool present = false;
    CombatVariant changelog, shipped;  // oldest / newest volume that ships the table (equal for a single volume)
};
struct WeatherFile {
    std::string name, volume;
    size_t bytes = 0, bytes_differing = 0;
    Bytes data;
};

}  // namespace p2m
