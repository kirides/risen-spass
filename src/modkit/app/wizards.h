// The wizard dialogs (Wizards menu): forms over core/wizards.h that generate project objects.
#pragma once

#include <string>

#include "app/app.h"
#include "core/wizards.h"

namespace mk {

class WizardUi {
public:
    void open(WizardKind kind) { want_ = kind; }
    // draws the open wizard as a modal window (call once per frame, after the menus)
    void draw(App& app);
    static const char* title(WizardKind k);

private:
    void start(App& app, WizardKind kind);
    bool fetch_form(App& app);
    bool weapon_form(App& app);
    bool trader_form(App& app);
    void finish(App& app, const WizardResult& r);

    WizardKind want_ = WizardKind::None, kind_ = WizardKind::None;
    std::string error_;
    FetchQuestParams fetch_;
    WeaponParams weapon_;
    TraderParams trader_;
    bool quest_id_touched_ = false;
};

}  // namespace mk
