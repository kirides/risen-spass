#include "core/package.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

#include "core/log.h"
#include "core/paths.h"
#include "core/validate.h"
#include "pack2mod/util.h"

namespace mk {

namespace {

void put16(std::string& s, uint32_t v) {
    s.push_back(static_cast<char>(v & 0xff));
    s.push_back(static_cast<char>((v >> 8) & 0xff));
}
void put32(std::string& s, uint32_t v) {
    put16(s, v & 0xffff);
    put16(s, v >> 16);
}
uint32_t get16(const std::string& s, size_t p) {
    if (p + 2 > s.size()) throw std::runtime_error("the zip ends early");
    return static_cast<uint8_t>(s[p]) | (static_cast<uint32_t>(static_cast<uint8_t>(s[p + 1])) << 8);
}
uint32_t get32(const std::string& s, size_t p) { return get16(s, p) | (get16(s, p + 2) << 16); }

bool deflate_raw(const std::string& in, std::string& out) {
    z_stream z{};
    if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
    out.resize(deflateBound(&z, static_cast<uLong>(in.size())) + 16);
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    z.avail_in = static_cast<uInt>(in.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = static_cast<uInt>(out.size());
    const int rc = deflate(&z, Z_FINISH);
    out.resize(z.total_out);
    deflateEnd(&z);
    return rc == Z_STREAM_END;
}

}  // namespace

std::string zip_files(const ZipFiles& files) {
    std::string out, central;
    uint32_t count = 0;
    for (const auto& [name, data] : files) {
        std::string packed;
        uint32_t method = 8;
        if (data.empty() || !deflate_raw(data, packed) || packed.size() >= data.size()) {
            method = 0;
            packed = data;
        }
        const uint32_t crc = static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size())));
        const uint32_t offset = static_cast<uint32_t>(out.size());
        // local header: signature, version 2.0, flags (bit 11: UTF-8 names), method, time 00:00, date 1980-01-01, crc, sizes, name length, extra length
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0x0800);
        put16(out, method);
        put16(out, 0);
        put16(out, 0x0021);
        put32(out, crc);
        put32(out, static_cast<uint32_t>(packed.size()));
        put32(out, static_cast<uint32_t>(data.size()));
        put16(out, static_cast<uint32_t>(name.size()));
        put16(out, 0);
        out += name;
        out += packed;
        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, method);
        put16(central, 0);
        put16(central, 0x0021);
        put32(central, crc);
        put32(central, static_cast<uint32_t>(packed.size()));
        put32(central, static_cast<uint32_t>(data.size()));
        put16(central, static_cast<uint32_t>(name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        central += name;
        ++count;
    }
    const uint32_t cd_offset = static_cast<uint32_t>(out.size());
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, count);
    put16(out, count);
    put32(out, static_cast<uint32_t>(central.size()));
    put32(out, cd_offset);
    put16(out, 0);
    return out;
}

ZipFiles unzip_files(const std::string& zip) {
    // the end of central directory record: the last 22+ bytes
    if (zip.size() < 22) throw std::runtime_error("not a zip file");
    size_t eocd = std::string::npos;
    for (size_t p = zip.size() - 22 + 1; p-- > 0 && zip.size() - p <= 22 + 65535;)
        if (get32(zip, p) == 0x06054b50) {
            eocd = p;
            break;
        }
    if (eocd == std::string::npos) throw std::runtime_error("not a zip file");
    const uint32_t count = get16(zip, eocd + 10), cd_offset = get32(zip, eocd + 16);
    ZipFiles out;
    size_t p = cd_offset;
    for (uint32_t i = 0; i < count; ++i) {
        if (get32(zip, p) != 0x02014b50) throw std::runtime_error("the zip's directory is damaged");
        const uint32_t method = get16(zip, p + 10), crc = get32(zip, p + 16), csize = get32(zip, p + 20), usize = get32(zip, p + 24);
        const uint32_t nlen = get16(zip, p + 28), elen = get16(zip, p + 30), clen = get16(zip, p + 32), lho = get32(zip, p + 42);
        if (p + 46 + nlen > zip.size()) throw std::runtime_error("the zip's directory is damaged");
        const std::string name = zip.substr(p + 46, nlen);
        p += 46 + nlen + elen + clen;
        if (!name.empty() && name.back() == '/') continue;  // a folder entry
        if (name.empty() || name[0] == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos || name.find("..") != std::string::npos)
            throw std::runtime_error("the zip has an unsafe path: " + name);
        if (get32(zip, lho) != 0x04034b50) throw std::runtime_error("the zip's local header is damaged");
        const size_t data_at = lho + 30 + get16(zip, lho + 26) + get16(zip, lho + 28);
        if (data_at + csize > zip.size()) throw std::runtime_error("the zip ends early");
        std::string data;
        if (method == 0) {
            data = zip.substr(data_at, csize);
        } else if (method == 8) {
            if (usize > (1u << 30)) throw std::runtime_error("an entry of the zip is too large");
            data.resize(usize);
            z_stream z{};
            if (inflateInit2(&z, -15) != Z_OK) throw std::runtime_error("zlib refused to start");
            z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(zip.data() + data_at));
            z.avail_in = csize;
            z.next_out = reinterpret_cast<Bytef*>(data.data());
            z.avail_out = usize;
            const int rc = inflate(&z, Z_FINISH);
            const bool whole = rc == Z_STREAM_END && z.total_out == usize;
            inflateEnd(&z);
            if (!whole) throw std::runtime_error("an entry of the zip is damaged: " + name);
        } else {
            throw std::runtime_error("the zip uses a compression the editor does not read");
        }
        if (static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()))) != crc) throw std::runtime_error("an entry of the zip is damaged (checksum): " + name);
        out.emplace_back(name, std::move(data));
    }
    return out;
}

bool valid_mod_id(const std::string& id) {
    if (id.empty() || id.size() > 64 || !std::isalpha(static_cast<unsigned char>(id[0]))) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
}

std::string install_text(const Project& p) {
    const ModInfo& m = p.mod;
    std::string s;
    s += m.name + " " + m.version + (m.author.empty() ? std::string() : " by " + m.author) + "\n";
    s += std::string(s.size() - 1, '=') + "\n\n";
    if (!m.description.empty()) s += m.description + "\n\n";
    s += "A mod for Risen Enhanced (x64) with the risen-spass modding layer (sentry.dll).\n\n";
    s += "Install: unpack this zip into <Risen>\\bin\\spass\\mods so that the folder\n    <Risen>\\bin\\spass\\mods\\" + m.id + "\\mod.toml\nexists. Start the game; the mod shows in the Mods tab of the game's menu (ESC).\n";
    s += "Remove: delete that folder. Savegames made with the mod may miss its items, NPCs and quests afterwards: keep a copy of them before you try.\n";
    if (!m.requires_mods.empty()) {
        s += "\nRequires these mods (install them too):\n";
        for (auto& r : m.requires_mods) s += "  - " + r + "\n";
    }
    if (!m.load_after.empty()) {
        s += "\nLoads after (when installed):\n";
        for (auto& r : m.load_after) s += "  - " + r + "\n";
    }
    s += "\nmodkit/project.json is the editor project of this mod: open it with risen-modkit to change the mod.\n";
    return s;
}

PackageResult build_package(const Project& p, const ExportOptions& opts, const std::string& zip_path) {
    PackageResult r;
    if (!valid_mod_id(p.mod.id)) {
        r.errors.push_back("the mod id '" + p.mod.id + "' cannot be a folder name: use letters, digits, '_' and '-', starting with a letter");
        return r;
    }
    if (p.mod.version.empty() || !std::any_of(p.mod.version.begin(), p.mod.version.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        r.warnings.push_back("the mod's version '" + p.mod.version + "' has no number: players cannot tell versions apart");
    if (p.mod.author.empty()) r.warnings.push_back("the mod has no author");
    if (p.mod.description.empty()) r.warnings.push_back("the mod has no description (the Mods tab of the game shows it)");
    const fs::path tmp = fs::temp_directory_path() / ("mkpkg_" + new_uid());
    const fs::path dir = tmp / u8(p.mod.id);
    std::error_code ec;
    fs::create_directories(dir, ec);
    const ExportResult er = export_mod(p, u8str(dir), opts);
    if (!er.ok) {
        r.errors = er.errors;
        fs::remove_all(tmp, ec);
        return r;
    }
    ZipFiles files;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        std::ifstream in(it->path(), std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string rel = u8str(fs::relative(it->path(), tmp));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        files.emplace_back(std::move(rel), std::move(data));
    }
    fs::remove_all(tmp, ec);
    if (files.empty()) {
        r.errors.push_back("the export wrote no files");
        return r;
    }
    files.emplace_back(p.mod.id + "/INSTALL.txt", install_text(p));
    std::sort(files.begin(), files.end());
    const std::string zip = zip_files(files);
    const fs::path out = u8(zip_path), part = u8(zip_path + ".part");
    if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
    {
        std::ofstream f(part, std::ios::binary | std::ios::trunc);
        f.write(zip.data(), static_cast<std::streamsize>(zip.size()));
        if (!f) {
            r.errors.push_back("could not write " + zip_path);
            return r;
        }
    }
    fs::rename(part, out, ec);
    if (ec) {
        fs::remove(part, ec);
        r.errors.push_back("could not move the package into place: " + zip_path);
        return r;
    }
    r.ok = true;
    r.files = files.size();
    r.bytes = zip.size();
    return r;
}

// ---- conflicts -----------------------------------------------------------------------------------------------------------------------------------

std::vector<Touch> project_touches(const Project& p) {
    std::vector<Touch> t;
    auto add = [&](const char* kind, const std::string& name, bool creates) {
        if (!name.empty()) t.push_back({kind, name, creates});
    };
    for (auto& i : p.items) add("item", i.name, i.mode == Mode::New);
    for (auto& i : p.recipes) add("recipe", i.name, i.mode == Mode::New);
    for (auto& i : p.spells) add("spell", i.name, i.mode == Mode::New);
    for (auto& i : p.npc_templates) add("NPC template", i.name, i.mode == Mode::New);
    for (auto& i : p.npcs) add("placed NPC", i.name, true);
    for (auto& i : p.quests) add("quest", i.name, true);
    for (auto& i : p.dialog_edits) add("dialogue info", i.info, false);
    for (auto& i : p.looks) add("object look", i.name, true);
    for (auto& i : p.patches)
        for (auto& [set, props] : i.properties)
            for (auto& [prop, v] : props) {
                (void)v;
                add("template property", i.template_name + "." + set + "." + prop, false);
            }
    for (auto& i : p.world.removals) add("world entity", i.entity_name.empty() ? i.guid : i.entity_name + " (" + i.guid + ")", false);
    for (auto& i : p.world.moves) add("world entity", i.entity_name.empty() ? i.guid : i.entity_name + " (" + i.guid + ")", false);
    for (auto& i : p.world.containers) add("container", i.entity_name.empty() ? i.guid : i.entity_name + " (" + i.guid + ")", false);
    for (auto& i : p.teachers) add("teacher", i.entity_name + " " + i.skill, false);
    for (auto& i : p.strings) add("text row", i.id, false);
    return t;
}

std::vector<Conflict> find_conflicts(const Project& a, const Project& b) {
    std::map<std::pair<std::string, std::string>, Touch> mine;
    for (auto& t : project_touches(a)) {
        auto key = std::make_pair(t.kind, p2m::lower(t.name));
        auto it = mine.find(key);
        if (it == mine.end()) mine.emplace(key, t);
        else it->second.creates = it->second.creates || t.creates;
    }
    std::vector<Conflict> out;
    std::set<std::pair<std::string, std::string>> seen;
    for (auto& t : project_touches(b)) {
        auto key = std::make_pair(t.kind, p2m::lower(t.name));
        auto it = mine.find(key);
        if (it == mine.end() || !seen.insert(key).second) continue;
        Conflict c;
        c.kind = t.kind;
        c.name = t.name;
        if (it->second.creates && t.creates) {
            c.error = true;
            c.detail = "both mods create it: only one of them can work";
        } else if (it->second.creates || t.creates) {
            c.detail = "one mod creates it and the other changes it: the creator must load first (load_after)";
        } else {
            c.detail = "both mods change it: the one that loads later wins where they set the same value";
        }
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<InstalledMod> read_installed_mods(const std::string& mods_dir, const std::string& skip_id) {
    std::vector<InstalledMod> out;
    std::error_code ec;
    if (mods_dir.empty() || !fs::is_directory(u8(mods_dir), ec)) return out;
    std::vector<fs::path> dirs;
    for (auto& e : fs::directory_iterator(u8(mods_dir), ec))
        if (e.is_directory()) dirs.push_back(e.path());
    std::sort(dirs.begin(), dirs.end());
    for (auto& d : dirs) {
        InstalledMod m;
        m.id = u8str(d.filename());
        m.dir = u8str(d);
        if (m.id == skip_id) continue;
        const fs::path proj = d / "modkit" / "project.json";
        if (fs::is_regular_file(proj, ec)) {
            try {
                m.project = load_project_file(u8str(proj));
                m.has_project = true;
                if (!m.project.mod.id.empty()) m.id = m.project.mod.id;
                if (m.id == skip_id) continue;
            } catch (const std::exception& e) {
                m.problem = e.what();
            }
        }
        out.push_back(std::move(m));
    }
    return out;
}

}  // namespace mk
