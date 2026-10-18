// Editors of the event graphs' field lists and condition sets, shared by the Events and the Layers panel.
#pragma once

#include <vector>

#include "core/events.h"
#include "core/layers.h"

namespace mk {
class App;
}

namespace mk::ui {

// the fields of one node or check (core/events.h tables); true when one changed
bool event_fields(App& app, const std::vector<EvFieldDef>& fields, json& data);
// a condition set { mode, negate, tests }: the mode, the negate switch and the list of checks with "+ Add a check"
bool event_condition_set(App& app, json& set, bool show_mode = true);
// the game's stock sectors (cached per game data object); empty without game data
const std::vector<StockSector>& event_stock_sectors(App& app);

}  // namespace mk::ui
