#include "app/dragdrop.h"
#include "core/i18n.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace mk::ui {

namespace {

bool accepts(const DragTemplate& d, const TplKind* kb, const TplKind* ke, const std::vector<std::string>& extra) {
    if (std::find(extra.begin(), extra.end(), d.name) != extra.end()) return true;
    if (d.entity) return false;
    return kb == ke || std::find(kb, ke, d.kind) != ke;
}

const DragTemplate* drop_impl(const TplKind* kb, const TplKind* ke, const std::vector<std::string>& extra) {
    const DragTemplate* d = dragging();
    if (!d || !accepts(*d, kb, ke, extra)) return nullptr;  // no highlight for what the field does not take
    if (!ImGui::BeginDragDropTarget()) return nullptr;
    static DragTemplate dropped;
    const DragTemplate* out = nullptr;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kDragTemplate)) {
        std::memcpy(&dropped, p->Data, sizeof dropped);
        out = &dropped;
    }
    ImGui::EndDragDropTarget();
    return out;
}

}  // namespace

void drag_source(const std::string& name, TplKind kind, bool entity, bool project, const std::string& label) {
    if (name.empty() || !ImGui::BeginDragDropSource()) return;
    DragTemplate d;
    std::strncpy(d.name, name.c_str(), sizeof d.name - 1);
    d.kind = kind;
    d.entity = entity;
    d.project = project;
    ImGui::SetDragDropPayload(kDragTemplate, &d, sizeof d);
    ImGui::TextUnformatted(name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", entity ? tr("placed NPC") : kind_label(kind), project ? tr(" (mod)") : "");
    if (!label.empty()) ImGui::TextDisabled("%s", label.c_str());
    ImGui::EndDragDropSource();
}

const DragTemplate* dragging() {
    const ImGuiPayload* p = ImGui::GetDragDropPayload();
    if (!p || !p->IsDataType(kDragTemplate) || p->DataSize != static_cast<int>(sizeof(DragTemplate))) return nullptr;
    return static_cast<const DragTemplate*>(p->Data);
}

const DragTemplate* drop_target(std::initializer_list<TplKind> kinds, const std::vector<std::string>& extra) {
    return drop_impl(kinds.begin(), kinds.end(), extra);
}

const DragTemplate* drop_target(const std::vector<TplKind>& kinds, const std::vector<std::string>& extra) {
    return drop_impl(kinds.data(), kinds.data() + kinds.size(), extra);
}

const DragTemplate* drop_entity() {
    const DragTemplate* d = dragging();
    if (!d || !d->entity || !ImGui::BeginDragDropTarget()) return nullptr;
    static DragTemplate dropped;
    const DragTemplate* out = nullptr;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kDragTemplate)) {
        std::memcpy(&dropped, p->Data, sizeof dropped);
        out = &dropped;
    }
    ImGui::EndDragDropTarget();
    return out;
}

void drag_behavior_source(const std::string& name) {
    if (name.empty() || !ImGui::BeginDragDropSource()) return;
    char buf[128] = {};
    std::strncpy(buf, name.c_str(), sizeof buf - 1);
    ImGui::SetDragDropPayload(kDragBehavior, buf, sizeof buf);
    ImGui::Text(tr("behaviour %s"), name.c_str());
    ImGui::TextDisabled("%s", tr("drop it on an NPC's Behaviours section"));
    ImGui::EndDragDropSource();
}

std::string drop_behavior() {
    std::string out;
    if (!ImGui::BeginDragDropTarget()) return out;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kDragBehavior)) out = static_cast<const char*>(p->Data);
    ImGui::EndDragDropTarget();
    return out;
}

}  // namespace mk::ui
