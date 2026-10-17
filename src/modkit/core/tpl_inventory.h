// The starting inventory of an NPC template, read offline (docs/RE.md section 52): the gCInventory_PS record is not reflected, the template stores its
// item stacks as objects after the set's two flags. The editor shows what a template carries ("shipped loot") so loot can be edited knowingly.
#pragma once

#include <string>
#include <vector>

#include "pack2mod/util.h"

namespace mk {

// one stack as stored: the item is the entity GUID of an item template
struct RawStack {
    std::string guid;   // "@" + 32 hex
    int amount = 1;
    int selection = 0, select_limit = 0, quick_slot = -1;
    int type = 0;        // gEStackType (0 = normal, 1 = seen on a few quest weapons)
    int equip_slot = 0;  // gEEquipSlot
};
// the second list of the record: what the NPC wears, keyed by an equipment slot number (11 = armour, 12 = helmet; 10 seen with an unknown item)
struct RawWorn {
    int slot = 0;
    std::string guid;
};
struct RawInventory {
    bool found = false;  // the template has a readable gCInventory_PS record
    std::vector<RawStack> stacks;
    std::vector<RawWorn> worn;
};

// `strings`: the template's string table (UTF-8). Never throws: a record that does not look as expected gives found = false.
RawInventory decode_inventory(const p2m::Bytes& file, const std::vector<std::string>& strings);

}  // namespace mk
