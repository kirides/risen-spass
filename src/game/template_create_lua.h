#pragma once

// The Lua part of creating new templates (docs/RE.md section 25, docs/LUA_API.md "New templates"): risen.template.create and the
// `new_items`, `new_npcs` and `templates` sections of risen.balance. Pure Lua on top of the native risen.template.clone / created /
// name_problem, the item and NPC balancing sections and risen.loc, so the unit tests run it against mocks.
namespace spass::game {

inline const char* kTemplateCreateChunk = R"LUA(
local risen = ...
local tmpl = risen.template
local balance = risen.balance

local function tr(...) local t = risen.trace if t and t.enabled("template") then t("template", ...) end end

local function sorted_keys(t)
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    return keys
end

-- names the sections of the spec being validated are about to create: a `loot` entry may name an item that does not exist yet
local planned = {}

-- keys of an `overrides` table that only create handles (everything else goes to the item / NPC balancing sections)
local CREATE_KEYS = { base = true, name = true, description = true, mesh = true, icon = true, material = true, check_assets = true }
local CREATE_KEY_LIST = "name, description, mesh, icon, material, check_assets"

local function kind_of(tpl)
    local have = {}
    for _, set in ipairs(tpl:property_sets() or {}) do have[set] = true end
    if have.gCNPC_PS and have.gCSkills_PS then return "npc", have end
    if have.gCItem_PS then return "item", have end
    return "other", have
end

-- `name` / `description`: a string (every language) or { English = "...", German = "...", default = "..." } -> same | nil, problem
-- (`default` is the text of every language the table does not name)
local function text_spec(v, key)
    if v == nil then return nil end
    if type(v) == "string" and v ~= "" then return v end
    if type(v) == "table" then
        local n = 0
        for lang, text in pairs(v) do
            if type(lang) ~= "string" or type(text) ~= "string" then
                return nil, key .. ": expected a string or a table { English = \"...\", German = \"...\" }"
            end
            n = n + 1
        end
        if n > 0 then return v end
    end
    return nil, key .. ": expected a non-empty string or a table { English = \"...\", German = \"...\" }"
end

local function set_text(key, spec)
    if type(spec) == "string" then
        risen.loc.set(key, spec)
    else
        -- `default` is set without a language (every language); the named languages win over it whatever the order
        for lang, text in pairs(spec) do
            if lang == "default" then risen.loc.set(key, text) else risen.loc.set(key, text, lang) end
        end
    end
end

-- the section handlers name templates "items.<name>" / "npcs.<name>"; in create's messages that is the new template
local function relabel(msg, section, base, name)
    local pattern = "^" .. section .. "%." .. base:gsub("%p", "%%%0")
    return (msg:gsub(pattern, name))
end

-- Validates `overrides` for a clone of `base_name` called `name`; nothing is created or written. Returns a plan | nil, problems:
--   plan = { kind, entry = settings for the items / npcs section (properties merged), props = plain property sets, name, description }
local function plan_create(name, base_name, ov, want)
    local problems = {}
    local function bad(msg) problems[#problems + 1] = msg end
    if type(name) ~= "string" then return nil, { "the new template name must be a string" } end
    local why = tmpl.name_problem(name)
    if why then bad(why) end
    if type(base_name) ~= "string" then return nil, { "the base template name must be a string" } end
    if ov == nil then ov = {} end
    if type(ov) ~= "table" then return nil, { "overrides must be a table { value = 100, mesh = \"...\", ... }" } end
    if tmpl.exists(name) and tmpl.created()[name] == nil then
        bad(("a template called '%s' already exists (it is not one this mod layer created): choose a new name"):format(name))
    end
    local made = tmpl.created()[name]
    if made and made ~= base_name then
        bad(("template '%s' was already created from '%s' in this process (not '%s')"):format(name, made, base_name))
    end
    local base, bwhy = tmpl.get(base_name)
    if not base then
        if planned[base_name] then
            bad(("base template '%s' is created by the same spec: clone its own base instead (a clone of a clone cannot be validated beforehand)"):format(base_name))
        else
            bad(("base template: %s (names: docs/TEMPLATES.md, risen.template.list)"):format(tostring(bwhy)))
        end
        return nil, problems
    end
    local kind, have = kind_of(base)
    if want and want ~= kind then
        bad(("base '%s' is %s, expected %s"):format(base_name, kind == "item" and "an item template" or kind == "npc" and "an NPC/monster template" or "neither an item nor an NPC template",
            want == "item" and "an item template" or "an NPC/monster template"))
    end
    if ov.base ~= nil and ov.base ~= base_name then bad("overrides.base differs from the base template argument") end

    local entry = {}
    for k, v in pairs(ov) do if not CREATE_KEYS[k] then entry[k] = v end end
    local props = {}
    local function put(set, prop, value, key)
        if not have[set] then bad(("%s: the base template '%s' has no property set '%s'"):format(key, base_name, set)) return end
        props[set] = props[set] or {}
        if props[set][prop] ~= nil then bad(("%s sets %s.%s twice"):format(key, set, prop)) return end
        props[set][prop] = value
    end
    -- mesh / icon names are looked up in the game archives and the mounted asset directories (docs/LUA_API.md "New meshes, textures and icons");
    -- `check_assets = false` skips that (a name the engine cannot resolve is not an error here, it only fails to show)
    local assets = risen.assets
    local check = ov.check_assets ~= false and assets and assets.check or nil
    if ov.check_assets ~= nil and type(ov.check_assets) ~= "boolean" then bad("check_assets: expected true or false") end
    if ov.mesh ~= nil then
        if type(ov.mesh) ~= "string" or ov.mesh == "" then bad("mesh: expected a mesh/actor file name such as It_Wpn_1H_Club.xmsh or Ani_Wolf_Monster_Wolf.xac")
        else
            local ok, why = true, nil
            if check then ok, why = check(kind == "npc" and "actor" or "mesh", ov.mesh) end
            if not ok then bad("mesh: " .. tostring(why))
            elseif kind == "npc" then put("eCAnimation_PS", "ResourceFilePath", ov.mesh, "mesh")
            else put("eCMesh_PS", "MeshFileName", ov.mesh, "mesh") end
        end
    end
    if ov.icon ~= nil then
        if type(ov.icon) ~= "string" or ov.icon == "" then bad("icon: expected an icon image name such as ic_it_1h_club")
        elseif kind == "npc" then bad("icon: only items have an icon")
        else
            local ok, why = true, nil
            if check then ok, why = check("icon", ov.icon) end
            if not ok then bad("icon: " .. tostring(why)) else put("gCItem_PS", "IconImage", ov.icon, "icon") end
        end
    end
    if ov.material ~= nil then
        if type(ov.material) ~= "number" or ov.material ~= math.floor(ov.material) or ov.material < 0 then bad("material: expected a material switch number >= 0")
        else put(kind == "npc" and "eCAnimation_PS" or "eCMesh_PS", "MaterialSwitch", string.format("%d", ov.material), "material") end
    end
    if ov.properties ~= nil then
        if type(ov.properties) ~= "table" then bad("properties must be a table { PropertySet = { Property = value } }")
        else
            for _, set in ipairs(sorted_keys(ov.properties)) do
                local p = ov.properties[set]
                if type(p) ~= "table" then bad(("properties.%s must be a table"):format(tostring(set)))
                else
                    for _, prop in ipairs(sorted_keys(p)) do
                        if props[set] and props[set][prop] ~= nil then bad(("properties.%s.%s conflicts with mesh/icon/material"):format(tostring(set), tostring(prop)))
                        else
                            props[set] = props[set] or {}
                            props[set][prop] = p[prop]
                        end
                    end
                end
            end
        end
    end
    if next(props) ~= nil then entry.properties = props else entry.properties = nil end

    local texts = {}
    for _, key in ipairs({ "name", "description" }) do
        local spec, problem = text_spec(ov[key], key)
        if problem then bad(problem) end
        texts[key] = spec
    end
    if texts.description ~= nil and not have.gCItem_PS then bad("description: only items have a description") end

    -- the item / NPC settings are validated by the balancing sections, against the base (same property sets as the clone will have)
    if kind == "item" then
        local _, p = balance.sections.items({ [base_name] = entry }, {})
        for _, m in ipairs(p or {}) do bad(relabel(m, "items", base_name, name)) end
    elseif kind == "npc" then
        local check = entry
        if type(entry.loot) == "table" then   -- items this spec creates do not exist yet: validate the rest of the loot
            check = {}
            for k, v in pairs(entry) do check[k] = v end
            check.loot = {}
            for item, amount in pairs(entry.loot) do
                if not (planned[item] and not tmpl.exists(item)) then check.loot[item] = amount end
            end
        end
        local _, p = balance.sections.npcs({ [base_name] = check }, {})
        for _, m in ipairs(p or {}) do bad(relabel(m, "npcs", base_name, name)) end
    else
        for _, key in ipairs(sorted_keys(entry)) do
            if key ~= "properties" then bad(("unknown setting '%s' (a template that is neither item nor NPC accepts: %s, properties)"):format(tostring(key), CREATE_KEY_LIST)) end
        end
        if entry.properties then
            local ok, err = tmpl.patch(base_name, entry.properties, { dry_run = true })
            if not ok then bad(tostring(err)) end
        end
    end
    if #problems > 0 then return nil, problems end
    return { kind = kind, entry = entry, name = texts.name, description = texts.description, base = base }
end

-- writes a validated plan into the (existing) clone
local function apply_plan(name, plan, opts)
    if plan.kind == "item" or plan.kind == "npc" then
        local section = plan.kind == "item" and balance.sections.items or balance.sections.npcs
        local jobs, problems = section({ [name] = plan.entry }, {})
        if #(problems or {}) > 0 then return nil, table.concat(problems, "; ") end
        for _, job in ipairs(jobs) do
            local ok, err = job.run({ instances = opts.instances })
            if not ok then return nil, tostring(err) end
        end
    elseif plan.entry.properties then
        local ok, err = tmpl.patch(name, plan.entry.properties, { instances = opts.instances })
        if not ok then return nil, tostring(err) end
    end
    -- labels: FO_<name> (focus label, inventory, tooltips) and ITEMDESC_<name> (item description). Without `name` / `description` the clone shows the
    -- base's text, unless a mod already supplied the key (loc files, risen.loc.set).
    if plan.name then
        set_text("FO_" .. name, plan.name)
    elseif not risen.loc.exists("FO_" .. name) then
        local label = plan.base:focus_name()
        if label and label ~= "" then risen.loc.set("FO_" .. name, label) end
    end
    if plan.description then
        set_text("ITEMDESC_" .. name, plan.description)
    elseif plan.kind == "item" and not risen.loc.exists("ITEMDESC_" .. name) then
        local text = plan.base:description()
        if text and text ~= "" and text ~= "-" then risen.loc.set("ITEMDESC_" .. name, text) end
    end
    return true
end

-- risen.template.create(name, base, overrides [, { instances = true }]) -> Template, created | nil, reason
--   Clones the template `base` (items, NPCs and monsters; the clone is a full independent copy) under the new unique name `name`. The clone gets the
--   deterministic ID of its name, so savegames that contain items of it resolve again when a mod creates it again (risen.template.id_for(name)).
--   overrides: the settings of the `items` section (value, requires, modifies, damage, use_type, category, mana_cost, properties) for an item base, of the
--   `npcs` section (hp_max, level, skills, guild, loot, ...) for an NPC base, plus name / description (labels), mesh, icon, material.
--   Everything is validated before anything is created. Calling it again for an existing clone of the same base re-applies the overrides (created = false).
function risen.template.create(name, base_name, overrides, opts)
    opts = opts or {}
    local plan, problems = plan_create(name, base_name, overrides, nil)
    if not plan then
        tr("create", tostring(name), "refused:", table.concat(problems, "; "))
        return nil, table.concat(problems, "; ")
    end
    local existed = tmpl.created()[name] ~= nil
    local t, why = tmpl.clone(name, base_name)
    if not t then return nil, why end
    local ok, err = apply_plan(name, plan, opts)
    if not ok then
        tr("create", name, "overrides failed:", tostring(err))
        return nil, ("template '%s' was created, but applying the overrides failed: %s"):format(name, tostring(err))
    end
    tr("create", name, existed and "re-applied overrides" or "created", "from", base_name)
    return t, not existed
end

-- risen.template.is_planned(name) -> bool: true when a spec validated in this process names a template it is about to create (new_items / new_npcs /
-- templates), so later sections (recipes) may refer to it before it exists
function risen.template.is_planned(name) return planned[name] == true end

-- ---- balance sections --------------------------------------------------------------------------------------------------------------
-- new_items = { My_Sword = { base = "It_1H_Sword", name = "...", value = 900, ... } }, new_npcs = { My_Wolf = { base = "Wolf", hp_max = 400, ... } },
-- templates = { ... } (either kind). Validated by apply() before anything is written; applied (idempotently) on the first tick and every session.start.
local function new_templates_section(key, want)
    return function(data, _)
        local jobs, problems = {}, {}
        if type(data) ~= "table" then return jobs, { key .. ": expected a table { NewTemplateName = { base = \"...\", ... } }" } end
        for _, name in ipairs(sorted_keys(data)) do
            local entry = data[name]
            local what = key .. "." .. tostring(name)
            if type(name) ~= "string" or type(entry) ~= "table" then
                problems[#problems + 1] = what .. ": expected a table { base = \"TemplateName\", ... }"
            elseif type(entry.base) ~= "string" then
                problems[#problems + 1] = what .. ": 'base' (the template to clone) is required"
            else
                local ov = {}
                for k, v in pairs(entry) do if k ~= "base" then ov[k] = v end end
                local plan, p = plan_create(name, entry.base, ov, want)
                if not plan then
                    for _, m in ipairs(p) do problems[#problems + 1] = what .. ": " .. m end
                else
                    planned[name] = true
                    local keys = {}
                    for _, k in ipairs(sorted_keys(ov)) do keys[#keys + 1] = tostring(k) end
                    jobs[#jobs + 1] = {
                        label = ("%s (new %s from %s%s)"):format(name, plan.kind, entry.base, #keys > 0 and (": " .. table.concat(keys, ", ")) or ""),
                        run = function(opts)
                            local t, err = risen.template.create(name, entry.base, ov, { instances = opts.instances })
                            if not t then return nil, err end
                            return true, nil, 0
                        end,
                    }
                end
            end
        end
        return jobs, problems
    end
end
risen.balance.register_section("new_items", new_templates_section("new_items", "item"))
risen.balance.register_section("new_npcs", new_templates_section("new_npcs", "npc"))
risen.balance.register_section("templates", new_templates_section("templates", nil))
)LUA";

}  // namespace spass::game
