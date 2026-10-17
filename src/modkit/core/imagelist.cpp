#include "core/imagelist.h"

#include <cstring>

namespace mk {

using p2m::Bytes;
using p2m::Error;

std::string ImageEntry::texture_file() const {
    std::string t = texture;
    const size_t dot = t.find_last_of('.');
    if (dot != std::string::npos) t.resize(dot);
    return t + "._ximg";
}

namespace {

struct Reader {
    const Bytes& b;
    size_t p = 0;
    std::vector<std::string> strs;

    uint8_t u8() {
        if (p + 1 > b.size()) throw Error("read past the end of the data");
        return static_cast<uint8_t>(b[p++]);
    }
    uint16_t u16() {
        const uint16_t v = p2m::rd16(b, p);
        p += 2;
        return v;
    }
    uint32_t u32() {
        const uint32_t v = p2m::rd32(b, p);
        p += 4;
        return v;
    }
    const std::string& str() {
        const uint16_t i = u16();
        static const std::string none;
        if (i == 0xFFFF) return none;
        if (i >= strs.size()) throw Error("string index out of range");
        return strs[i];
    }
};

struct Prop {
    std::string name, type;
    std::string text;       // bCString
    int ints[4] = {0, 0, 0, 0};  // bCPoint / bCRect
    uint32_t enum_value = 0;
};

struct Body {
    uint16_t cls = 0;
    std::string cls_name;
    std::vector<Prop> props;
    size_t end = 0;  // where the object ends (its size field)
    const Prop* get(const char* n) const {
        for (auto& p : props)
            if (p.name == n) return &p;
        return nullptr;
    }
};

// accessor header + body of one object (the shape every record of the library files has, tools/g3cs.py read_accessor)
Body read_object(Reader& r) {
    Body o;
    r.u16();                                  // version
    if (r.u8() != 1) throw Error("object header");
    r.u16();
    r.u8();
    o.cls_name = r.str();
    r.u16();
    r.u8();
    r.u16();
    r.u16();                                  // body version (v0)
    const uint32_t size = r.u32();
    o.end = r.p + size;
    if (o.end > r.b.size()) throw Error("object runs past the end of the file");
    r.u16();                                  // v1
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        Prop pr;
        pr.name = r.str();
        pr.type = r.str();
        r.u16();
        const uint32_t sz = r.u32();
        const size_t pend = r.p + sz;
        if (pend > o.end) throw Error("property runs past its object");
        if (pr.type == "bCString") {
            if (sz == 2) {
                pr.text = r.str();
            } else if (sz == 5) {
                r.p += 3;
                pr.text = r.str();
            }
        } else if (pr.type == "bCRect" || pr.type == "bCPoint") {
            const size_t cnt = std::min<size_t>(sz / 4, 4);
            for (size_t k = 0; k < cnt; ++k) pr.ints[k] = static_cast<int32_t>(p2m::rd32(r.b, r.p + k * 4));
        } else if (pr.type.rfind("bTPropertyContainer<enum", 0) == 0 && sz == 6) {
            pr.enum_value = p2m::rd32(r.b, r.p + 2);
        }
        r.p = pend;
        o.props.push_back(std::move(pr));
    }
    return o;
}

std::string lower(const std::string& s) { return p2m::lower(s); }

}  // namespace

ImageCatalog ImageCatalog::parse(const Bytes& file) {
    ImageCatalog cat;
    if (file.size() < 20 || std::memcmp(file.data(), "GENOMFLE", 8) != 0) throw Error("not a GENOMFLE file");
    const uint32_t tbl = p2m::rd32(file, 10);
    if (tbl + 9 > file.size() || p2m::rd32(file, tbl) != 0xDEADBEEF) throw Error("string table not found");
    Reader r{file};
    {
        const uint32_t cnt = p2m::rd32(file, tbl + 5);
        size_t q = tbl + 9;
        for (uint32_t i = 0; i < cnt; ++i) {
            const uint16_t n = p2m::rd16(file, q);
            if (q + 2 + n > file.size()) throw Error("string table is damaged");
            r.strs.emplace_back(file.data() + q + 2, n);
            q += 2 + n;
        }
    }
    r.p = 14;
    if (r.u32() != 0x31303056) throw Error("not an image list archive (V001)");
    r.u8();
    const uint32_t ncol = r.u32();
    if (ncol > 1000) throw Error("implausible collection count");
    for (uint32_t i = 0; i < ncol; ++i) {
        r.u32();
        r.u32();
        r.u16();
    }
    for (uint32_t ci = 0; ci < ncol; ++ci) {
        Body col = read_object(r);
        const Prop* cn = col.get("ImageCollectionName");
        const std::string cname = cn ? cn->text : std::string();
        ++cat.collections_;
        const uint32_t nlists = r.u32();
        for (uint32_t li = 0; li < nlists && r.p < col.end; ++li) {
            Body list = read_object(r);
            const Prop* tn = list.get("TextureName");
            const std::string tex = tn ? tn->text : std::string();
            r.u16();  // version
            const uint32_t nimg = r.u32();
            for (uint32_t k = 0; k < nimg && r.p < list.end; ++k) {
                Body im = read_object(r);
                ImageEntry e;
                e.collection = cname;
                e.texture = tex;
                if (const Prop* n = im.get("ImageName")) e.name = n->text;
                if (const Prop* rc = im.get("RectOnTexture")) {
                    e.left = rc->ints[0];
                    e.top = rc->ints[1];
                    e.right = rc->ints[2];
                    e.bottom = rc->ints[3];
                }
                if (im.cls_name == "eCGuiSplitImage2") e.kind = ImageEntry::Kind::Split;
                else if (im.cls_name == "eCGuiLayeredImage2") e.kind = ImageEntry::Kind::Layered;
                if (e.kind == ImageEntry::Kind::Layered) {
                    if (const Prop* b = im.get("BackgroundImage")) e.background = b->text;
                    if (const Prop* o = im.get("OverlayImage")) e.overlay = o->text;
                }
                r.p = im.end;
                if (e.name.empty()) continue;
                cat.by_name_.emplace(lower(e.name), cat.entries_.size());
                cat.entries_.push_back(std::move(e));
            }
            r.p = list.end;
        }
        r.p = col.end;
    }
    return cat;
}

const ImageEntry* ImageCatalog::find(const std::string& name) const {
    auto it = by_name_.find(lower(name));
    return it == by_name_.end() ? nullptr : &entries_[it->second];
}

}  // namespace mk
