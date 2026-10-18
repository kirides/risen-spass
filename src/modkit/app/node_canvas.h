// A node graph canvas on imgui-node-editor (the Unreal-Blueprints look): draws a Graph of the project with the node types of core/graph.h,
// lets the user move nodes, link pins (only what link_allowed permits), drag a link into empty space to create a node already linked, delete with
// the Delete key, and create nodes from the right-click menu. Positions live in the model (GraphNode::x/y), so undo and a reopened project keep
// the layout. One editor context per graph (by the owner's uid) keeps the view (scroll, zoom) per graph.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/graph.h"

namespace ax::NodeEditor {
struct EditorContext;
}

namespace mk {

class NodeCanvas {
public:
    explicit NodeCanvas(GraphKind kind) : kind_(kind) {}
    ~NodeCanvas();
    NodeCanvas(const NodeCanvas&) = delete;
    NodeCanvas& operator=(const NodeCanvas&) = delete;

    // one line of text per node body (what the node does), computed by the panel
    using Summary = std::function<std::string(const GraphNode&)>;
    // draws the graph `g` (identified by `key`, e.g. the dialogue's uid); returns true when the graph changed
    bool draw(const std::string& key, Graph& g, const Summary& summary);
    const std::string& selected() const { return selected_; }  // the selected node's uid ("" = none)
    void select(const std::string& key, const std::string& node_uid);  // select and centre a node (next frame)
    // nodes whose problems the canvas marks with a red border
    void set_marked(std::map<std::string, std::string> node_messages) { marked_ = std::move(node_messages); }

private:
    struct Ctx {
        ax::NodeEditor::EditorContext* ed = nullptr;
        std::unordered_map<std::string, int> node_ids;  // node uid -> id
        std::unordered_map<int, std::string> node_uids;
        std::unordered_map<std::string, int> link_ids;
        std::unordered_map<int, std::string> link_uids;
        std::unordered_map<std::string, std::pair<double, double>> synced;  // the model position last given to / read from the editor
        int next = 1;
        bool first_frame = true, clear_selection = false;
        std::string focus;
        bool focus_navigate = true;  // false: select only (a node made from the menu stays where it was placed)
        std::vector<std::string> focus_more;  // further nodes to select with `focus` (duplicates)
    };
    Ctx& ctx(const std::string& key);
    int node_id(Ctx& c, const std::string& uid);
    int link_id(Ctx& c, const std::string& uid);
    static uintptr_t pin_id(int node, int pin_index) { return static_cast<uintptr_t>(node) * 64 + static_cast<uintptr_t>(pin_index) + 1; }
    bool decode_pin(Ctx& c, const Graph& g, uintptr_t pin, std::string& node, std::string& pin_name) const;
    bool create_menu(Graph& g, double x, double y, std::string* created);
    // copies the nodes (and the links between them) 40 units down-right; the copies become the selection
    void duplicate_nodes(Ctx& c, Graph& g, const std::vector<std::string>& uids);

    GraphKind kind_;
    std::map<std::string, Ctx> ctxs_;
    std::string selected_;
    std::map<std::string, std::string> marked_;
    // a link dragged into empty space: the node and pin it came from, waiting for the create menu
    std::string pending_node_, pending_pin_;
    std::vector<std::string> menu_nodes_;  // the nodes the node context menu acts on
    bool open_node_menu_ = false;
    double pending_x_ = 0, pending_y_ = 0;
    bool open_menu_ = false;
};

}  // namespace mk
