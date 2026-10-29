// The game's compiled materials (`._xmat`, "GR01" "SM01") as an editable object list (docs/RE.md section 60, tools/g3mat.py is the offline prototype).
// A material is a 0x28-byte header and a flat sequence of property objects: eCMaterialResource2, the shader (eCShaderDefault, ...) and the nodes it uses
// (eCColorSrcSampler = a texture, eCColorSrcConstant, eCColorSrcCombiner, eCTexCoordSrc*). An object is the engine's accessor-object grammar with inline
// strings; behind its properties it carries a class specific tail (node links, own GUID, editor position) that is kept as bytes. The shader's size field
// also covers the nodes behind it, which is why the writer recomputes sizes from the object boundaries. Reading then writing gives the file back byte for byte.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "pack2mod/util.h"

namespace mk {

struct MatProp {
    std::string name, type;
    std::string payload;  // the value bytes as stored (a string is u16 length + characters, a bool one byte, an enum u16 0xc9 + u32 ...)
};

struct MatObject {
    std::string cls;            // "eCColorSrcSampler"
    uint16_t version = 0;       // the class record's version (the u16 behind "01 00 00")
    std::vector<MatProp> props;
    std::string tail;           // bytes between the last property and the next object
    size_t covers = 0;          // how many of the following objects lie inside this object's declared body (the shader: all nodes)

    MatProp* find(const std::string& name);
    const MatProp* find(const std::string& name) const;
    // typed access; the getters return false / the fallback when the property is missing or has another size
    bool get_string(const std::string& name, std::string* out) const;
    bool set_string(const std::string& name, const std::string& value);  // false when missing; any length
    bool get_bool(const std::string& name, bool fallback = false) const;
    bool set_bool(const std::string& name, bool v);
    bool get_int(const std::string& name, int32_t* out) const;  // int, long, float-free 4 byte values; enums give their u32
    bool set_int(const std::string& name, int32_t v);
    bool get_float(const std::string& name, float* out) const;
    bool set_float(const std::string& name, float v);
};

// One of the shader's eight colour inputs (eCShaderDefault tail, docs/RE.md section 60): a link to a node by GUID plus a channel and a modifier.
struct MatSlot {
    std::string name;      // Diffuse, Opacity, SelfIllumination, Specular, SpecularPower, Normal, Distortion, LightEmission
    bool linked = false;   // the proxy's validity byte
    std::string guid;      // 32 hex digits of the target node
    uint32_t component = 0;  // 0 full, 1 .rgb, 2 .r, 3 .g, 4 .b, 5 .a
    uint32_t modifier = 0;   // 0 none, 1 -, 2 1-, 3 saturate, 4 ceil, 5 floor, 6 abs, 7 frac
    int node = -1;         // index into Material::objects of the linked node, -1 when none / not found
};

// A change of a shipped material (the project's `materials` section): the image behind named shader slots and shader / resource properties.
// Exported as a loose copy of the material under the same name (assets/<name>), which shadows the packed one (docs/RE.md sections 26 and 60).
struct MaterialEdit {
    std::string uid;
    std::string material;                        // "ItWpn_SwordMisc_01_Diffuse_01._xmat"
    std::map<std::string, std::string> slots;    // slot name ("Diffuse", "Normal", ...) -> the image name as a sampler writes it ("X.tga")
    std::map<std::string, int> props;            // "eCShaderDefault.EnableSpecular" -> value (bool 0/1, int, enum number)
    std::string notes;
};

struct Material {
    std::string header;  // the 0x28 bytes; the three sizes at 12, 16 and 20 are rewritten by write()
    std::vector<MatObject> objects;

    // throws p2m::Error when the data is not a material the grammar covers completely
    static Material parse(const p2m::Bytes& file);
    p2m::Bytes write() const;
    std::vector<MatObject*> of_class(const std::string& cls);
    // the image names of all samplers in file order, raw as written ("X_Diffuse_01.tga")
    std::vector<std::string> sampler_images() const;
    // the shader's colour inputs (empty when the material has no eCShaderDefault tail of the known shape)
    std::vector<MatSlot> slots() const;
    // the node's own GUID as 32 hex digits ("" when its tail has no element suffix)
    std::string guid_of(size_t object) const;
    // the image a slot ends up at: follows combiners through their first input that reaches a sampler; "" when none
    // the sampler object a slot ends up at (same walk), -1 when none
    int slot_sampler(const MatSlot& slot, int depth = 0) const;
    std::string slot_image(const MatSlot& slot, int depth = 0) const;
};

// the edit applied to a parsed material; false with `error` set when a slot has no sampler behind it or a property does not exist / has another type
bool apply_material_edit(Material& m, const MaterialEdit& e, std::string& error);

}  // namespace mk
