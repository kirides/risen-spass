// Tests of the mesh edits (core/mesh_edit.h). Included by modkit_tests.cpp.

#include "core/mesh_edit.h"

TEST(mesh_edit_rewrites_shipped_meshes) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    size_t n = 0, writable = 0, identical = 0, bad = 0;
    std::string failures;
    for (const auto& a : g->assets("_xmsh")) {
        ++n;
        try {
            const p2m::Bytes raw = g->read(a.name);
            mk::MeshInfo info;
            try {
                info = mk::mesh_info(raw);
            } catch (const std::exception&) {
                continue;  // the SpeedTree billboard: the editor cannot read it at all (docs/RE.md section 26)
            }
            if (!info.writable) continue;
            ++writable;
            mk::MeshEdit e;
            e.mesh = a.name;
            p2m::Bytes out;
            std::string why;
            if (!mk::apply_mesh_edit(raw, e, out, why)) throw p2m::Error(why);
            // the same geometry again: vertices and indices as decoded
            const MeshData x = decode_mesh(raw), y = decode_mesh(out);
            if (x.vertex_count != y.vertex_count || x.index_count != y.index_count || x.submeshes.size() != y.submeshes.size()) throw p2m::Error("counts differ");
            for (size_t i = 0; i < x.submeshes.size(); ++i)
                if (x.submeshes[i].material != y.submeshes[i].material) throw p2m::Error("material differs");
            if (x.vertices != y.vertices) throw p2m::Error("vertices differ");
            ++identical;
        } catch (const std::exception& ex) {
            if (++bad < 8) failures += "\n    " + a.name + ": " + ex.what();
        }
    }
    std::printf("  %zu meshes, %zu writable, %zu rewritten with the same geometry\n", n, writable, identical);
    if (bad) std::printf("  failures: %zu%s\n", bad, failures.c_str());
    CHECK(writable > 500 && bad == 0);
}

TEST(mesh_edit_changes_material_and_scale) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    const p2m::Bytes raw = g->read("Obj_Nat_Stone_Small_Flat_01._xmsh");
    const mk::MeshInfo before = mk::mesh_info(raw);
    CHECK(before.writable && before.subs.size() >= 1);
    mk::MeshEdit e;
    e.mesh = "Obj_Nat_Stone_Small_Flat_01._xmsh";
    e.materials[0] = "A_Much_Longer_Material_Name_01._xmat";
    e.scale = {2, 2, 2};
    p2m::Bytes out;
    std::string why;
    CHECK(mk::apply_mesh_edit(raw, e, out, why));
    const mk::MeshInfo after = mk::mesh_info(out);
    CHECK(after.subs.size() == before.subs.size() && after.subs[0].material == "A_Much_Longer_Material_Name_01._xmat");
    CHECK(after.subs[0].triangles == before.subs[0].triangles);
    CHECK(std::fabs(after.box_max[0] - 2 * before.box_max[0]) < 0.01f * (1 + std::fabs(before.box_max[0])));
    // refused: a sub-mesh that does not exist, a scale of zero
    mk::MeshEdit bad = e;
    bad.materials[99] = "X._xmat";
    CHECK(!mk::apply_mesh_edit(raw, bad, out, why) && !why.empty());
    bad = e;
    bad.scale = {0, 1, 1};
    CHECK(!mk::apply_mesh_edit(raw, bad, out, why));
    // the project keeps it, validation and export see it
    Project p = load_showcase();
    e.uid = new_uid();
    p.meshes.push_back(e);
    const Project q = project_from_json(to_json(p));
    CHECK(q.meshes.size() == 1 && q.meshes[0].materials.at(0) == e.materials.at(0) && q.meshes[0].scale[2] == 2);
    size_t errors = 0;
    for (auto& pr : validate(q, g.get()))
        if (pr.area == "Meshes" && pr.severity == Severity::Error) ++errors;
    CHECK(errors == 0);
    const fs::path outdir = temp_dir("meshexport") / "mods" / "m";
    ExportOptions o;
    o.project_dir = showcase_dir();
    o.game = g.get();
    CHECK(export_mod(q, u8str(outdir), o).ok);
    CHECK(fs::exists(outdir / "assets" / "Obj_Nat_Stone_Small_Flat_01._xmsh"));
}
