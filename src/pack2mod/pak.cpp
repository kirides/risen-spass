#include "pack2mod/pak.h"

#include <zlib.h>

#include <filesystem>
#include <map>

namespace p2m {

namespace {
constexpr size_t kHeader = 0x30;
}

Pak::~Pak() {
    if (f_) std::fclose(f_);
}

std::unique_ptr<Pak> Pak::open(const std::string& path) {
    std::unique_ptr<Pak> pk(new Pak());
    pk->path_ = path;
    pk->f_ = _wfopen(std::filesystem::u8path(path).c_str(), L"rb");
    if (!pk->f_) throw Error("cannot open " + path);
    Bytes head(kHeader, '\0');
    if (std::fread(head.data(), 1, kHeader, pk->f_) != kHeader) throw Error(path + ": not a G3V0 archive (too short)");
    if (head.compare(4, 4, "G3V0") != 0) throw Error(path + ": not a G3V0 archive");
    const uint64_t toc_off = rd64(head, 0x20), total = rd64(head, 0x28);
    _fseeki64(pk->f_, 0, SEEK_END);
    const uint64_t file_size = static_cast<uint64_t>(_ftelli64(pk->f_));
    if (toc_off < kHeader || toc_off > file_size) throw Error(path + ": table of contents outside the file");
    Bytes toc(static_cast<size_t>(file_size - toc_off), '\0');
    _fseeki64(pk->f_, static_cast<long long>(toc_off), SEEK_SET);
    if (!toc.empty() && std::fread(toc.data(), 1, toc.size(), pk->f_) != toc.size()) throw Error(path + ": cannot read the table of contents");
    pk->parse_toc(toc, total - toc_off);
    for (size_t i = 0; i < pk->entries_.size(); ++i) pk->by_path_[lower(pk->entries_[i].path)] = i;
    return pk;
}

void Pak::parse_toc(const Bytes& t, uint64_t total) {
    size_t p = 0;
    const uint32_t nl = rd32(t, p);
    p += 4 + (nl ? nl + 1 : 0);
    p += 24;  // FILETIME x3
    const uint32_t count = rd32(t, p + 4);
    p += 8;
    p = dir(t, "", count, p);
    if (p != total && p != t.size()) throw Error(path_ + ": table of contents ends at an unexpected offset");
}

size_t Pak::dir(const Bytes& t, std::string prefix, uint32_t count, size_t p) {
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t attr = rd32(t, p), nl = rd32(t, p + 4);
        p += 8;
        if (p + nl + 1 > t.size()) throw Error(path_ + ": name outside the table of contents");
        const std::string name = t.substr(p, nl);
        p += nl + 1;
        const std::string full = prefix + name;
        if (attr & 0x10) {
            p += 24;
            const uint32_t cnt = rd32(t, p + 4);
            p += 8;
            p = dir(t, full + "/", cnt, p);
        } else {
            PakEntry e;
            e.path = full;
            e.offset = rd64(t, p);
            p += 8;
            e.mtime = rd64(t, p + 16);
            p += 24;
            e.stored = rd32(t, p + 12);
            e.size = rd32(t, p + 16);
            p += 20;
            entries_.push_back(std::move(e));
        }
    }
    return p;
}

const PakEntry* Pak::find(const std::string& path) const {
    const auto it = by_path_.find(lower(path));
    return it == by_path_.end() ? nullptr : &entries_[it->second];
}

Bytes Pak::read(const PakEntry& e) const {
    Bytes raw(e.stored, '\0');
    _fseeki64(f_, static_cast<long long>(e.offset), SEEK_SET);
    if (e.stored && std::fread(raw.data(), 1, e.stored, f_) != e.stored) throw Error(path_ + ": cannot read " + e.path);
    if (!e.compressed()) return raw;
    Bytes out(e.size, '\0');
    uLongf n = e.size;
    const int rc = uncompress(reinterpret_cast<Bytef*>(out.data()), &n, reinterpret_cast<const Bytef*>(raw.data()), e.stored);
    if (rc != Z_OK || n != e.size) throw Error(path_ + ": " + e.path + ": inflate failed or the size differs from the table of contents");
    return out;
}

// ---------------------------------------------------------------------------------------------- writer

namespace {

struct Stored {
    uint64_t offset;
    Bytes bytes;
    uint32_t size;
};

struct Tree {
    std::vector<std::string> names;  // insertion order of children
    std::vector<bool> is_dir;
    std::vector<Tree> sub;       // parallel to names (empty for files)
    std::vector<size_t> file_ix; // parallel (index into Stored list for files)
};

void put_child(Tree& t, const std::vector<std::string>& parts, size_t i, size_t file_ix) {
    const std::string& n = parts[i];
    size_t k = 0;
    for (; k < t.names.size(); ++k)
        if (t.names[k] == n) break;
    if (i + 1 == parts.size()) {
        t.names.push_back(n);
        t.is_dir.push_back(false);
        t.sub.emplace_back();
        t.file_ix.push_back(file_ix);
        return;
    }
    if (k == t.names.size()) {
        t.names.push_back(n);
        t.is_dir.push_back(true);
        t.sub.emplace_back();
        t.file_ix.push_back(0);
    }
    put_child(t.sub[k], parts, i + 1, file_ix);
}

void emit_tree(Bytes& o, const Tree& t, const std::vector<Stored>& st) {
    wr32(o, static_cast<uint32_t>(t.names.size()));
    const Bytes ft(24, '\0');
    for (size_t i = 0; i < t.names.size(); ++i) {
        const std::string& nm = t.names[i];
        if (t.is_dir[i]) {
            wr32(o, 0x20010);
            wr32(o, static_cast<uint32_t>(nm.size()));
            o += nm;
            o.push_back('\0');
            o += ft;
            wr32(o, 0x20010);
            emit_tree(o, t.sub[i], st);
        } else {
            const Stored& s = st[t.file_ix[i]];
            wr32(o, 0x20020);
            wr32(o, static_cast<uint32_t>(nm.size()));
            o += nm;
            o.push_back('\0');
            wr64(o, s.offset);
            o += ft;
            wr32(o, 0x20020);
            wr64(o, s.bytes.size() != s.size ? 0x200000000ull : 0);
            wr32(o, static_cast<uint32_t>(s.bytes.size()));
            wr32(o, s.size);
        }
    }
}

}  // namespace

Bytes build_pak(const std::vector<PakFile>& files) {
    Bytes body;
    std::vector<Stored> st;
    for (const PakFile& f : files) {
        Stored s;
        s.offset = kHeader + body.size();
        s.size = static_cast<uint32_t>(f.data.size());
        if (f.compress && !f.data.empty()) {
            uLongf n = compressBound(static_cast<uLong>(f.data.size()));
            s.bytes.resize(n);
            compress(reinterpret_cast<Bytef*>(s.bytes.data()), &n, reinterpret_cast<const Bytef*>(f.data.data()), static_cast<uLong>(f.data.size()));
            s.bytes.resize(n);
        } else {
            s.bytes = f.data;
        }
        body += s.bytes;
        st.push_back(std::move(s));
    }
    Tree root;
    for (size_t i = 0; i < files.size(); ++i) {
        std::vector<std::string> parts;
        size_t a = 0;
        const std::string& p = files[i].path;
        for (size_t k = 0; k <= p.size(); ++k)
            if (k == p.size() || p[k] == '/') {
                parts.push_back(p.substr(a, k - a));
                a = k + 1;
            }
        put_child(root, parts, 0, i);
    }
    Bytes toc;
    wr32(toc, 0);  // root name length
    toc += Bytes(24, '\0');
    wr32(toc, 0x20010);
    emit_tree(toc, root, st);
    const uint64_t toc_off = kHeader + body.size();
    Bytes out;
    wr32(out, 1);
    out += "G3V0";
    wr64(out, 0);
    wr64(out, 1);
    wr64(out, kHeader);
    wr64(out, toc_off);
    wr64(out, toc_off + toc.size());
    return out + body + toc;
}

}  // namespace p2m
