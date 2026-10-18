// The strict reader of the engine's entity files: templates (`.tple`, "GENOMETP") and world layers (`.lrent`, "GENOMEDL") (docs/RE.md section 56).
// The grammar is the one the engine's own readers use (eCTemplateEntity::ReadHeader / ReadContent, eCDynamicEntity / eCGeometryEntity / eCEntity::OnRead,
// bCObjectBase::Read, bCAccessorPropertyObject::Read, ...), not a guess from the data: every field is read in order and the object sizes must come out
// exactly, so a file the reader does not understand throws p2m::Error instead of giving a half-recognised result. Replaces the byte-by-byte scanning of
// p2m::parse_template and the record search of the world loader for the editor.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "pack2mod/tple.h"

namespace mk {

// one property set of an entity (eCEntityPropertySet and its subclasses: gCNPC_PS, eCMesh_PS, ...)
struct GSet {
    uint16_t version = 0;
    std::string cls;
    size_t begin = 0, end = 0;     // file offsets of the set's record
    std::vector<p2m::Prop> props;  // array members as "Array[].Field", offsets are those of the property records
    size_t tail_begin = 0;         // where the data behind the properties starts (class specific: the inventory's stacks), up to the object's end
    size_t tail_end = 0;
    // property objects found inside the tail (a collision shape, a mover's path, the slots of gCInteraction_PS ...): each is an accessor object read with the
    // same grammar; what else the tail holds is class specific and stays unread
    std::vector<GSet> nested;
    // gCInventory_PS: the stacks and the worn items of the tail were read (their properties are appended to `props`: Amount, Template, ... per stack,
    // Slot, Item, CachedItem per worn item); false = the tail has another shape and stays unread
    bool inventory_read = false;
};

struct GEntity {
    uint16_t version = 0;
    std::string id;       // "@" + 32 hex: the entity's own GUID
    std::string creator;  // "@" + 32 hex: the template this entity was made from (a layer entity), "" = none
    std::string ref;      // a template entity that only refers to another template: its GUID, "" = none (such an entity has no property sets)
    std::string name;
    std::array<float, 16> matrix{};  // layer entities: world matrix, row-major, position = row 3
    std::array<float, 3> box_min{}, box_max{};
    size_t begin = 0, end = 0;  // file offsets of the entity's record
    std::vector<GSet> sets;
};

struct GFile {
    bool layer = false;  // .lrent (GENOMEDL) instead of .tple (GENOMETP)
    uint16_t file_version = 0;
    std::vector<std::string> strings;
    std::vector<GEntity> entities;
    std::vector<std::pair<uint32_t, uint32_t>> deps;  // parent, child (indices into `entities`): the entity tree
    size_t table_offset = 0;
};

// throws p2m::Error when the data is not a file the grammar covers completely
GFile parse_genome(const p2m::Bytes& file);

// the same content as the scanner's p2m::Template (name, strings, GUIDs, flat property list), for code that reads templates that way
p2m::Template genome_template(const GFile& f);

// the properties of one entity as the scanner's flat list (sets in order, nested objects behind their set)
p2m::Template genome_entity_props(const GEntity& e);

}  // namespace mk
