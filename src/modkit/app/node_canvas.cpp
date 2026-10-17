#include "app/node_canvas.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

#include <algorithm>
#include <cmath>

namespace ed = ax::NodeEditor;

namespace mk {

namespace {

ImU32 kind_color(PinKind k) {
    switch (k) {
        case PinKind::Flow: return IM_COL32(230, 230, 230, 255);
        case PinKind::Choice: return IM_COL32(110, 170, 255, 255);
        case PinKind::Cond: return IM_COL32(80, 210, 200, 255);
    }
    return IM_COL32(255, 255, 255, 255);
}

ImVec4 to_vec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// node type colours are written 0xAARRGGBB; ImGui packs ABGR
ImU32 from_argb(unsigned c) { return IM_COL32((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, (c >> 24) & 0xff); }

// a pin icon: a circle (filled when linked) in the pin kind's colour
void pin_icon(PinKind kind, bool linked) {
    const float s = ImGui::GetTextLineHeight();
    ImGui::Dummy(ImVec2(s, s));
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (kind == PinKind::Flow) {
        // a triangle: the direction of the sequence
        const float r = s * 0.32f;
        if (linked) dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x - r, c.y + r), kind_color(kind));
        else dl->AddTriangle(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x - r, c.y + r), kind_color(kind), 1.5f);
    } else if (linked) {
        dl->AddCircleFilled(c, s * 0.28f, kind_color(kind));
    } else {
        dl->AddCircle(c, s * 0.28f, kind_color(kind), 0, 1.5f);
    }
}

}  // namespace

NodeCanvas::~NodeCanvas() {
    for (auto& [k, c] : ctxs_)
        if (c.ed) ed::DestroyEditor(c.ed);
}

NodeCanvas::Ctx& NodeCanvas::ctx(const std::string& key) {
    Ctx& c = ctxs_[key];
    if (!c.ed) {
        ed::Config cfg;
        cfg.SettingsFile = nullptr;  // positions are in the project, the view is per session
        c.ed = ed::CreateEditor(&cfg);
    }
    return c;
}

int NodeCanvas::node_id(Ctx& c, const std::string& uid) {
    auto it = c.node_ids.find(uid);
    if (it != c.node_ids.end()) return it->second;
    const int id = c.next++;
    c.node_ids[uid] = id;
    c.node_uids[id] = uid;
    return id;
}

int NodeCanvas::link_id(Ctx& c, const std::string& uid) {
    auto it = c.link_ids.find(uid);
    if (it != c.link_ids.end()) return it->second;
    const int id = 1000000 + c.next++;
    c.link_ids[uid] = id;
    c.link_uids[id] = uid;
    return id;
}

bool NodeCanvas::decode_pin(Ctx& c, const Graph& g, uintptr_t pin, std::string& node, std::string& pin_name) const {
    if (!pin) return false;
    const int nid = static_cast<int>((pin - 1) / 64);
    const size_t idx = static_cast<size_t>((pin - 1) % 64);
    auto it = c.node_uids.find(nid);
    if (it == c.node_uids.end()) return false;
    const GraphNode* n = g.find(it->second);
    const NodeTypeDef* t = n ? node_type(kind_, n->type) : nullptr;
    if (!t || idx >= t->pins.size()) return false;
    node = n->uid;
    pin_name = t->pins[idx].name;
    return true;
}

void NodeCanvas::select(const std::string& key, const std::string& node_uid) {
    ctx(key).focus = node_uid;
    selected_ = node_uid;
}

bool NodeCanvas::create_menu(Graph& g, double x, double y, std::string* created) {
    bool made = false;
    // with a dragged pin only the node types that can take the link are offered
    const GraphNode* from = pending_node_.empty() ? nullptr : g.find(pending_node_);
    const PinDef* fp = from ? node_type(kind_, from->type)->pin(pending_pin_) : nullptr;
    ImGui::TextDisabled(fp ? "Create and link" : "Create");
    ImGui::Separator();
    for (auto& t : node_types(kind_)) {
        if (fp) {
            bool fits = false;
            for (auto& p : t.pins) fits |= p.kind == fp->kind && p.input != fp->input;
            if (!fits) continue;
        }
        if (ImGui::MenuItem(t.title)) {
            GraphNode n = make_node(kind_, t.type, x, y);
            *created = n.uid;
            g.nodes.push_back(n);
            made = true;
            if (fp) {
                for (auto& p : t.pins) {
                    if (p.kind != fp->kind || p.input == fp->input) continue;
                    if (fp->input) add_link(kind_, g, n.uid, p.name, pending_node_, pending_pin_);
                    else add_link(kind_, g, pending_node_, pending_pin_, n.uid, p.name);
                    break;
                }
            }
        }
        if (ImGui::IsItemHovered() && t.help) ImGui::SetTooltip("%s", t.help);
    }
    return made;
}

bool NodeCanvas::draw(const std::string& key, Graph& g, const Summary& summary) {
    Ctx& c = ctx(key);
    ed::SetCurrentEditor(c.ed);
    bool changed = false;
    ed::Begin("##canvas");
    const ImVec2 mouse_canvas = ed::ScreenToCanvas(ImGui::GetMousePos());

    // positions the model changed (new nodes, undo) go into the editor
    for (auto& n : g.nodes) {
        const int id = node_id(c, n.uid);
        auto it = c.synced.find(n.uid);
        if (it == c.synced.end() || std::fabs(it->second.first - n.x) > 0.01 || std::fabs(it->second.second - n.y) > 0.01) {
            ed::SetNodePosition(id, ImVec2(static_cast<float>(n.x), static_cast<float>(n.y)));
            c.synced[n.uid] = {n.x, n.y};
        }
    }

    // nodes
    for (auto& n : g.nodes) {
        const NodeTypeDef* t = node_type(kind_, n.type);
        if (!t) continue;
        const int id = node_id(c, n.uid);
        const bool marked = marked_.count(n.uid) != 0;
        if (marked) ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(1, 0.35f, 0.3f, 1));
        ed::BeginNode(id);
        ImGui::PushID(n.uid.c_str());
        ImGui::TextUnformatted(t->title);
        const float header_bottom = ImGui::GetItemRectMax().y;
        // inputs | body | outputs
        ImGui::BeginGroup();
        for (size_t i = 0; i < t->pins.size(); ++i) {
            const PinDef& p = t->pins[i];
            if (!p.input) continue;
            ed::BeginPin(pin_id(id, static_cast<int>(i)), ed::PinKind::Input);
            ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
            pin_icon(p.kind, !links_into(g, n.uid, p.name).empty());
            if (*p.label) {
                ImGui::SameLine(0, 4);
                ImGui::TextUnformatted(p.label);
            }
            ed::EndPin();
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 10);
        ImGui::BeginGroup();
        const std::string body = summary ? summary(n) : std::string();
        const float wrap = 210.0f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        if (!body.empty()) ImGui::TextWrapped("%s", body.c_str());
        else ImGui::Dummy(ImVec2(60, 1));
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::SameLine(0, 10);
        ImGui::BeginGroup();
        for (size_t i = 0; i < t->pins.size(); ++i) {
            const PinDef& p = t->pins[i];
            if (p.input) continue;
            ed::BeginPin(pin_id(id, static_cast<int>(i)), ed::PinKind::Output);
            ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
            if (*p.label) {
                ImGui::TextUnformatted(p.label);
                ImGui::SameLine(0, 4);
            }
            pin_icon(p.kind, !links_from(g, n.uid, p.name).empty());
            ed::EndPin();
        }
        ImGui::EndGroup();
        ImGui::PopID();
        ed::EndNode();
        if (marked) ed::PopStyleColor();
        // the coloured header
        const ImVec2 np = ed::GetNodePosition(id), ns = ed::GetNodeSize(id);
        if (ns.x > 0) {
            ImDrawList* dl = ed::GetNodeBackgroundDrawList(id);
            const float r = ed::GetStyle().NodeRounding;
            dl->AddRectFilled(ImVec2(np.x + 1, np.y + 1), ImVec2(np.x + ns.x - 1, header_bottom + 3), from_argb(t->color), r, ImDrawFlags_RoundCornersTop);
        }
    }

    // links
    for (auto& l : g.links) {
        const GraphNode* a = g.find(l.from_node);
        const GraphNode* b = g.find(l.to_node);
        const NodeTypeDef* ta = a ? node_type(kind_, a->type) : nullptr;
        const NodeTypeDef* tb = b ? node_type(kind_, b->type) : nullptr;
        if (!ta || !tb) continue;
        int ia = -1, ib = -1;
        for (size_t i = 0; i < ta->pins.size(); ++i)
            if (l.from_pin == ta->pins[i].name) ia = static_cast<int>(i);
        for (size_t i = 0; i < tb->pins.size(); ++i)
            if (l.to_pin == tb->pins[i].name) ib = static_cast<int>(i);
        if (ia < 0 || ib < 0) continue;
        ed::Link(link_id(c, l.uid), pin_id(node_id(c, a->uid), ia), pin_id(node_id(c, b->uid), ib), to_vec4(kind_color(ta->pins[static_cast<size_t>(ia)].kind)), 2.0f);
    }

    // new links, or a link dragged into empty space (-> the create menu)
    if (ed::BeginCreate(ImVec4(1, 1, 1, 1), 2.0f)) {
        ed::PinId a, b;
        if (ed::QueryNewLink(&a, &b) && a && b) {
            std::string na, pa, nb, pb;
            if (decode_pin(c, g, a.Get(), na, pa) && decode_pin(c, g, b.Get(), nb, pb)) {
                // the drag may start at the input: links always go output -> input
                const PinDef* da = node_type(kind_, g.find(na)->type)->pin(pa);
                if (da && da->input) {
                    std::swap(na, nb);
                    std::swap(pa, pb);
                }
                std::string why;
                if (link_allowed(kind_, g, na, pa, nb, pb, &why)) {
                    if (ed::AcceptNewItem(ImVec4(0.5f, 1, 0.5f, 1), 3.0f)) {
                        add_link(kind_, g, na, pa, nb, pb);
                        changed = true;
                    }
                } else {
                    ed::Suspend();
                    ImGui::SetTooltip("%s", why.c_str());
                    ed::Resume();
                    ed::RejectNewItem(ImVec4(1, 0.4f, 0.4f, 1), 2.0f);
                }
            }
        }
        ed::PinId p;
        if (ed::QueryNewNode(&p) && p) {
            std::string pn, pp;
            if (decode_pin(c, g, p.Get(), pn, pp) && ed::AcceptNewItem()) {
                pending_node_ = pn;
                pending_pin_ = pp;
                pending_x_ = mouse_canvas.x;
                pending_y_ = mouse_canvas.y;
                open_menu_ = true;
            }
        }
    }
    ed::EndCreate();

    // deletion (Delete key, or the editor's own cut)
    if (ed::BeginDelete()) {
        ed::LinkId l;
        while (ed::QueryDeletedLink(&l)) {
            if (ed::AcceptDeletedItem()) {
                auto it = c.link_uids.find(static_cast<int>(l.Get()));
                if (it != c.link_uids.end()) {
                    const std::string uid = it->second;
                    g.links.erase(std::remove_if(g.links.begin(), g.links.end(), [&](const GraphLink& x) { return x.uid == uid; }), g.links.end());
                    changed = true;
                }
            }
        }
        ed::NodeId n;
        while (ed::QueryDeletedNode(&n)) {
            if (ed::AcceptDeletedItem()) {
                auto it = c.node_uids.find(static_cast<int>(n.Get()));
                if (it != c.node_uids.end()) {
                    remove_node(g, it->second);
                    c.synced.erase(it->second);
                    changed = true;
                }
            }
        }
    }
    ed::EndDelete();

    // the create menu: right click on the background, or a link dropped into empty space
    ed::Suspend();
    if (ed::ShowBackgroundContextMenu()) {
        pending_node_.clear();
        pending_pin_.clear();
        pending_x_ = mouse_canvas.x;
        pending_y_ = mouse_canvas.y;
        open_menu_ = true;
    }
    if (open_menu_) {
        ImGui::OpenPopup("##create");
        open_menu_ = false;
    }
    if (ImGui::BeginPopup("##create")) {
        std::string created;
        if (create_menu(g, pending_x_, pending_y_, &created)) {
            changed = true;
            c.focus = created;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        pending_node_.clear();
    }
    ed::Resume();

    if (!c.focus.empty()) {
        auto it = c.node_ids.find(c.focus);
        if (it != c.node_ids.end()) {
            ed::SelectNode(it->second);
            ed::NavigateToSelection(false, c.first_frame ? 0.0f : 0.2f);
        }
        c.focus.clear();
    } else if (c.first_frame && !g.nodes.empty()) {
        // open at zoom 1 on the top-left node (fitting a large graph would make the text unreadably small)
        const GraphNode* first = &g.nodes.front();
        for (auto& n : g.nodes)
            if (n.y < first->y - 1 || (std::fabs(n.y - first->y) <= 1 && n.x < first->x)) first = &n;
        ed::SelectNode(node_id(c, first->uid));
        ed::NavigateToSelection(false, 0.0f);
        c.clear_selection = true;
    } else if (c.clear_selection) {
        ed::ClearSelection();
        c.clear_selection = false;
    }
    c.first_frame = false;
    ed::End();

    // positions back into the model once the mouse is up: one drag is one undo step
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        for (auto& n : g.nodes) {
            auto it = c.node_ids.find(n.uid);
            if (it == c.node_ids.end()) continue;
            const ImVec2 p = ed::GetNodePosition(it->second);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x > 1e8f) continue;
            if (std::fabs(p.x - n.x) > 0.5 || std::fabs(p.y - n.y) > 0.5) {
                n.x = std::round(p.x);
                n.y = std::round(p.y);
                c.synced[n.uid] = {n.x, n.y};
                changed = true;
            }
        }
    }
    ed::NodeId sel[1];
    const int count = ed::GetSelectedNodes(sel, 1);
    if (count > 0) {
        auto it = c.node_uids.find(static_cast<int>(sel[0].Get()));
        selected_ = it == c.node_uids.end() ? std::string() : it->second;
    } else {
        selected_.clear();
    }
    ed::SetCurrentEditor(nullptr);
    return changed;
}

}  // namespace mk
