// risen-material: look at and change the game's compiled materials from the command line (docs/RE.md section 60). Used by the live checks of the
// material editor (scripts, mods/matprobe) to make a loose `._xmat` from a shipped one without storing any game data in the repository.
//   risen-material <Risen>\data dump  <Name._xmat>
//   risen-material <Risen>\data mesh  <Name._xmsh> <out file> [--scale s] [--material 0=Other._xmat] ...
//   risen-material <Risen>\data patch <Name._xmat> <out file> [--slot Normal=new.tga] [--set Class.Prop=value] ...
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "core/gamedata.h"
#include "core/material.h"
#include "core/mesh_edit.h"

using namespace mk;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: risen-material <data dir> dump <name> | patch <name> <out> [--slot Slot=image] [--set Class.Prop=value]...\n");
        return 2;
    }
    try {
        auto g = GameData::load(argv[1]);
        const std::string cmd = argv[2];
        if (cmd == "mesh" && argc >= 5) {
            MeshEdit e;
            e.mesh = argv[3];
            for (int i = 5; i + 1 < argc; i += 2) {
                const std::string flag = argv[i], arg = argv[i + 1];
                if (flag == "--scale") e.scale = {std::stof(arg), std::stof(arg), std::stof(arg)};
                else if (flag == "--material") e.materials[std::atoi(arg.c_str())] = arg.substr(arg.find('=') + 1);
                else throw std::runtime_error("unknown option " + flag);
            }
            std::string out, why;
            if (!apply_mesh_edit(g->read(argv[3]), e, out, why)) throw std::runtime_error(why);
            std::ofstream f(argv[4], std::ios::binary);
            f.write(out.data(), static_cast<std::streamsize>(out.size()));
            std::printf("wrote %s (%zu bytes)\n", argv[4], out.size());
            return 0;
        }
        Material m = Material::parse(g->read(argv[3]));
        if (cmd == "dump") {
            for (const auto& o : m.objects) {
                std::printf("%s\n", o.cls.c_str());
                for (const auto& p : o.props) {
                    std::string v;
                    if (o.get_string(p.name, &v)) std::printf("    %-28s \"%s\"\n", p.name.c_str(), v.c_str());
                    else {
                        int32_t i = 0;
                        std::printf("    %-28s %s\n", p.name.c_str(), o.get_int(p.name, &i) ? std::to_string(i).c_str() : "(raw)");
                    }
                }
            }
            for (const auto& s : m.slots())
                if (s.linked) std::printf("slot %-17s -> %-22s comp %u mod %u image %s\n", s.name.c_str(), m.objects[static_cast<size_t>(s.node)].cls.c_str(), s.component, s.modifier, m.slot_image(s).c_str());
            return 0;
        }
        if (cmd == "patch" && argc >= 5) {
            for (int i = 5; i + 1 < argc; i += 2) {
                const std::string flag = argv[i], arg = argv[i + 1], key = arg.substr(0, arg.find('=')), val = arg.substr(arg.find('=') + 1);
                if (arg.find('=') == std::string::npos) throw std::runtime_error("expected key=value: " + arg);
                if (flag == "--slot") {
                    bool done = false;
                    for (const auto& s : m.slots())
                        if (s.name == key) {
                            const int n = m.slot_sampler(s);
                            if (n >= 0) done = m.objects[static_cast<size_t>(n)].set_string("ImageFilePath", val);
                        }
                    if (!done) throw std::runtime_error("no sampler behind slot " + key);
                } else if (flag == "--set") {
                    const auto dot = key.find('.');
                    bool done = false;
                    for (auto& o : m.objects)
                        if (o.cls == key.substr(0, dot)) {
                            const std::string pn = key.substr(dot + 1);
                            const MatProp* pr = o.find(pn);
                            if (pr && pr->payload.size() == 1) done = o.set_bool(pn, val != "0") || done;
                            else done = o.set_string(pn, val) || o.set_int(pn, std::atoi(val.c_str())) || done;
                        }
                    if (!done) throw std::runtime_error("cannot set " + key);
                } else throw std::runtime_error("unknown option " + flag);
            }
            const std::string out = m.write();
            std::ofstream f(argv[4], std::ios::binary);
            f.write(out.data(), static_cast<std::streamsize>(out.size()));
            std::printf("wrote %s (%zu bytes)\n", argv[4], out.size());
            return 0;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 2;
}
