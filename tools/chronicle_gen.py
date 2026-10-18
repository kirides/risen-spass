#!/usr/bin/env python3
"""Generator of examples/modkit/chronicle.rmk.json, the complete example project of risen-modkit ("The Ashen Chronicle").

The project is plain JSON (see src/modkit/core/project.cpp); writing 14 event graphs, 4 dialogues and 8 quest flows by hand
would be thousands of lines of ids, so this script builds them with small helpers (deterministic uids: the same input gives the
same file). The result is then normalised by the editor itself: `risen-modkit chronicle.rmk.json --export mods/chronicle_example`
writes modkit/project.json in the editor's own format. Nothing of the exported mod is ever written by hand.

    python tools/chronicle_gen.py --assets      # also (re)create the icons, sounds and the OBJ model under examples/modkit/chronicle/
    python tools/chronicle_gen.py               # only the project file (examples/modkit/chronicle.rmk.json)

docs/editor/CHRONICLE.md describes the story and the features.
"""
import hashlib
import json
import math
import os
import struct
import sys
import wave

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(ROOT, "examples", "modkit")
ASSETS = os.path.join(OUT, "chronicle")


def U(label):
    return hashlib.md5(("chronicle/" + label).encode("utf-8")).hexdigest()


def T(en, de=None):
    return {"English": en, "German": de if de is not None else en}


# ---------------------------------------------------------------------------------------------------------------------------------
# graphs
# ---------------------------------------------------------------------------------------------------------------------------------
class Graph:
    def __init__(self, ns):
        self.ns = ns
        self.nodes = []
        self.links = []
        self.ids = {}

    def add(self, key, typ, data=None, col=0, row=0, x=None, y=None):
        uid = U("%s/%s" % (self.ns, key))
        assert key not in self.ids, "duplicate node key " + key
        n = {"uid": uid, "type": typ, "pos": [float(col * 340 if x is None else x), float(row * 170 if y is None else y)]}
        if data:
            n["data"] = data
        self.nodes.append(n)
        self.ids[key] = uid
        return key

    def link(self, a, pin, b, bpin="in"):
        ua, ub = self.ids[a], self.ids[b]
        self.links.append({"uid": U("%s/%s.%s>%s.%s" % (self.ns, a, pin, b, bpin)), "from": [ua, pin], "to": [ub, bpin]})

    def chain(self, *keys, pin="next"):
        for a, b in zip(keys, keys[1:]):
            self.link(a, pin, b)

    def json(self):
        return {"nodes": self.nodes, "links": self.links}


def vtest(name, op, value, scope="game"):
    return {"kind": "var", "scope": scope, "name": name, "op": op, "value": value}


def cset(*tests, mode="all", negate=False):
    return {"mode": mode, "negate": negate, "tests": list(tests)}


# ---------------------------------------------------------------------------------------------------------------------------------
# positions (game units: 100 = 1 m).  The new-game beach: the stone circle (Obj_Nat_Stonehenge_01) at -31956 639 -3757, the start at -31900 600 -4800
# ---------------------------------------------------------------------------------------------------------------------------------
CIRCLE = (-31956, 639, -3757)
CAMP = (-35080, 420, -6646)
MAREN = (-30050, 700, -6350)   # the loot cluster of the start area in the south-east (ground item It_Gold at -30093 654 -6407): 25 m from the start, 32 m from the stone circle

MOD_ID = "chronicle_example"
project = {
    "format": "risen-modkit-project",
    "format_version": 1,
    "mod": {
        "id": MOD_ID,
        "name": "The Ashen Chronicle",
        "version": "1.0.0",
        "author": "risen-spass",
        "description": "The complete example project of risen-modkit (examples/modkit/chronicle.rmk.json, docs/editor/CHRONICLE.md): something new in every chapter 1-4, made with every feature of the editor.",
        "languages": ["English", "German"],
    },
}

# ---------------------------------------------------------------------------------------------------------------------------------
# items
# ---------------------------------------------------------------------------------------------------------------------------------
project["items"] = [
    {"uid": U("item/blade"), "mode": "new", "name": "MKC_Chronicle_Blade", "base": "It_1H_Sword",
     "label": T("Chronicler's Blade", "Klinge des Chronisten"),
     "description": T("A sword forged from the ink of an old story.", "Ein Schwert, geschmiedet aus der Tinte einer alten Geschichte."),
     "value": 420, "damage": 36, "requires": {"Strength": 20}, "icon": "chronicle_blade.png"},
    {"uid": U("item/tide_potion"), "mode": "new", "name": "MKC_Tide_Potion", "base": "It_Po_Health_02",
     "label": T("Tide Draught", "Gezeitentrank"), "description": T("Tastes of salt and ash.", "Schmeckt nach Salz und Asche."),
     "value": 90, "modifies": {"HitPoints": 160}},
    {"uid": U("item/axe"), "mode": "new", "name": "MKC_Warlord_Axe", "base": "It_Axe_Waraxe",
     "label": T("Ash Warlord's Axe", "Axt des Aschekriegsherrn"), "description": T("Still warm from the camp fire.", "Noch warm vom Lagerfeuer."),
     "value": 650, "damage": 52, "requires": {"Strength": 35}},
    {"uid": U("item/key"), "mode": "new", "name": "MKC_Tide_Key", "base": "It_Stoneplate",
     "label": T("Key of the Tide", "Schluessel der Gezeiten"), "description": T("It hums when the tide rises.", "Er summt, wenn die Flut steigt."), "value": 0},
    {"uid": U("item/tablet"), "mode": "new", "name": "MKC_Chronicle_Tablet", "base": "It_Stoneplate", "mesh": "MKC_Tablet.xmsh",
     "label": T("Chronicle Tablet", "Chronikplatte"), "description": T("A fragment of the Ashen Chronicle.", "Ein Bruchstueck der Aschechronik."), "value": 25},
    {"uid": U("item/amulet"), "mode": "new", "name": "MKC_Tide_Amulet", "base": "It_Am_HP",
     "label": T("Amulet of the Ashen Tide", "Amulett der Aschegezeiten"), "description": T("The Chronicle's last page, worn around the neck.", "Die letzte Seite der Chronik, um den Hals getragen."),
     "value": 1500, "modifies": {"HitPoints": 150}, "icon": "chronicle_amulet.png"},
    {"uid": U("item/rusty"), "mode": "edit", "name": "It_1H_Sword_Rusty",
     "label": T("Salt-eaten sword", "Salzzerfressenes Schwert"), "value": 9, "damage": 11},
    {"uid": U("item/apple"), "mode": "edit", "name": "It_Apple", "value": 2},
]
project["item_groups"] = [
    {"uid": U("group/potions"), "group": "Items/Potions", "value_mult": 0.75},
    {"uid": U("group/axes"), "group": "Items/Weapons_Axes", "exclude": "Titan", "damage_mult": 1.1},
    {"uid": U("group/pearls"), "names": ["It_Pearl", "It_BlackPearl"], "value_mult": 1.5},
]

# ---------------------------------------------------------------------------------------------------------------------------------
# spells (docs/SPELL_PROPERTIES.md)
# ---------------------------------------------------------------------------------------------------------------------------------
project["spells"] = [
    {"uid": U("spell/light"), "mode": "edit", "name": "Light", "mana_cost": 7, "scroll_mana_cost": 3},
    {"uid": U("spell/frost"), "mode": "edit", "name": "Frost", "damage_mult": 1.5},
    {"uid": U("spell/ember"), "mode": "new", "name": "MKC_Ember_Whisper", "base": "Fireball", "forms": ["crystal", "scroll"],
     "label": T("Ember Whisper", "Glutfluestern"), "description": T("A quiet fireball that burns longer.", "Ein leiser Feuerball, der laenger brennt."),
     "value": 300, "mana_cost": 16, "requires": {"Fireball": 1}, "damage_mult": 1.5, "speed_mult": 1.3, "radius_mult": 1.2, "damage_type": "Fire"},
]

# ---------------------------------------------------------------------------------------------------------------------------------
# recipes
# ---------------------------------------------------------------------------------------------------------------------------------
project["recipes"] = [
    {"uid": U("recipe/blade"), "mode": "new", "name": "MKC_Recipe_Chronicle_Blade", "station": "anvil",
     "ingredients": [["It_1H_Steel_Sharp", 1], ["It_IronOre", 2], ["It_Pearl", 1]], "skills": {"Smith": 1}, "result": "MKC_Chronicle_Blade",
     "label": T("Smithing plan: Chronicler's Blade", "Schmiedeplan: Klinge des Chronisten"),
     "description": T("Maren's plan: a blade that remembers.", "Marens Plan: eine Klinge, die sich erinnert.")},
    {"uid": U("recipe/potion"), "mode": "new", "name": "MKC_Recipe_Tide_Potion", "station": "alchemy",
     "ingredients": [["It_Plant_Health_01", 2], ["It_Plant_Mint", 1]], "skills": {"Alchemy": 1}, "result": "MKC_Tide_Potion",
     "label": T("Alchemy plan: Tide Draught", "Alchemieplan: Gezeitentrank"), "learn": True},
    {"uid": U("recipe/hot"), "mode": "edit", "name": "It_1H_Steel_Hot", "skills": {"Smith": 1}},
]

# ---------------------------------------------------------------------------------------------------------------------------------
# NPC and monster templates, groups
# ---------------------------------------------------------------------------------------------------------------------------------
project["npc_templates"] = [
    {"uid": U("tpl/maren"), "mode": "new", "name": "MKC_Maren_T", "base": "Peasant_Female_1_01", "label": T("Maren the Chronicler", "Maren die Chronistin"),
     "hp_max": 220, "level": 4, "guild": "Cit", "behaviors": [{"behavior": "chronicler"}]},
    {"uid": U("tpl/smuggler"), "mode": "new", "name": "MKC_Smuggler_T", "base": "Peasant_1_03", "label": T("Smuggler", "Schmuggler"),
     "hp_max": 260, "level": 8, "guild": "Cit", "loot": {"It_Gold": 400}, "properties": {"gCDialog_PS": {"TradeEnabled": True}}},
    {"uid": U("tpl/raider"), "mode": "new", "name": "MKC_Ash_Raider_T", "base": "Skeleton", "label": T("Ash Raider", "Aschepluenderer"),
     "hp_max": 260, "level": 11, "damage_mult": 1.2, "loot": {"It_Gold": 25}},
    {"uid": U("tpl/warlord"), "mode": "new", "name": "MKC_Ash_Warlord_T", "base": "Skeleton_BDDon_Boss", "label": T("Ash Warlord", "Aschekriegsherr"),
     "hp_max": 1400, "level": 20, "damage_mult": 1.6, "loot": {"MKC_Warlord_Axe": 1, "MKC_Tide_Potion": 2, "It_Gold": 300},
     "behaviors": [{"behavior": "camp_watch", "params": {"taunt": "Who dares to walk into the Ash Camp?"}}]},
    {"uid": U("tpl/lord"), "mode": "new", "name": "MKC_Tide_Lord_T", "base": "LizardWarrior_EliteBoss_01", "label": T("Lord of the Tide", "Herr der Gezeiten"),
     "hp_max": 3200, "level": 28, "damage_mult": 1.4, "loot": {"It_Gold": 800, "MKC_Tide_Potion": 5}},
    {"uid": U("tpl/lizard"), "mode": "edit", "name": "LizardWarrior", "hp_mult": 1.3, "skills": {"Strength": 95}},
    {"uid": U("tpl/wolfblack"), "mode": "edit", "name": "Wolf_Black", "level_add": 2},
]
project["npc_groups"] = [
    {"uid": U("grp/wolves"), "match": "^Wolf", "exclude": "Young", "hp_mult": 1.2, "damage_mult": 1.1,
     "notes": "Chapter 1: the island's wolves have been eating ash."},
    {"uid": U("grp/ch3"), "group": "Monster/Kapitel 3", "level_add": 1, "damage_mult": 1.15, "skills_mult": {"ProtEdge": 1.1},
     "notes": "Chapter 3: the monsters of the chapter hit harder and take more."},
]

# ---------------------------------------------------------------------------------------------------------------------------------
# placed NPCs
# ---------------------------------------------------------------------------------------------------------------------------------
def inv(*rows):
    return [list(r) for r in rows]


project["npcs"] = [
    {"uid": U("npc/maren"), "name": "MKC_Maren", "template": "MKC_Maren_T", "position": [float(MAREN[0]), float(MAREN[1]), float(MAREN[2])], "label": T("Maren", "Maren"),
     "peaceful": True, "version": 2, "items": inv(["It_Quill", 1], ["It_Parchment_Empty", 5], ["It_Apple", 3]),
     "routine": {"at": {"dx": 60, "dy": 0, "dz": 0, "kind": "lookat", "yaw": 90}}},
    {"uid": U("npc/scout"), "name": "MKC_Scout", "template": "Peasant_1_01", "position": [MAREN[0] - 100.0, MAREN[1], MAREN[2] + 50.0], "label": T("Wounded scout", "Verwundeter Kundschafter"),
     "peaceful": True, "guild": "Cit", "level": 3, "hp_max": 120, "items": inv(["It_Po_Health_01", 2], ["It_Bread", 1]),
     "appear_when": ["quest", "Q_MKC_Camp", "Running"], "leave_when": ["quest", "Q_MKC_Camp", "Success"]},
    {"uid": U("npc/warlord"), "name": "MKC_Ash_Warlord", "template": "MKC_Ash_Warlord_T", "position": [CAMP[0], CAMP[1], CAMP[2]], "label": T("Ash Warlord", "Aschekriegsherr"),
     "peaceful": False, "items": inv(["MKC_Warlord_Axe", 1, True], ["It_Gold", 150])},
    {"uid": U("npc/raider1"), "name": "MKC_Ash_Raider_1", "template": "MKC_Ash_Raider_T", "position": [CAMP[0] + 250.0, CAMP[1], CAMP[2] + 100.0], "peaceful": False},
    {"uid": U("npc/raider2"), "name": "MKC_Ash_Raider_2", "template": "MKC_Ash_Raider_T", "position": [CAMP[0] - 250.0, CAMP[1], CAMP[2] + 150.0], "peaceful": False,
     "routine": {"patrol": {"points": [{"dx": 0, "dy": 0, "dz": 0}, {"dx": 300, "dy": 0, "dz": 200}, {"dx": 300, "dy": 0, "dz": -200}], "kind": "guard"}}},
    {"uid": U("npc/raider3"), "name": "MKC_Ash_Raider_3", "template": "MKC_Ash_Raider_T", "position": [CAMP[0], CAMP[1], CAMP[2] - 300.0], "peaceful": False, "level": 13},
    {"uid": U("npc/smuggler"), "name": "MKC_Smuggler", "template": "MKC_Smuggler_T", "position": [MAREN[0] + 200.0, MAREN[1], MAREN[2] - 100.0], "label": T("Smuggler Joss", "Schmuggler Joss"),
     "peaceful": True, "items": inv(["MKC_Tide_Potion", 6], ["MKC_Recipe_Tide_Potion", 1], ["MKC_Ember_Whisper_Scroll", 2], ["It_Pearl", 4], ["It_IronOre", 5], ["It_1H_Steel_Sharp", 2]),
     "routine": {"work": {"dx": 150, "dy": 0, "dz": 0, "kind": "guard"}, "relax": {"dx": 0, "dy": 0, "dz": 150, "kind": "lookat"}, "sleep": {"dx": -100, "dy": 0, "dz": 100, "kind": "sleep"}}},
    {"uid": U("npc/lord"), "name": "MKC_Tide_Lord", "template": "MKC_Tide_Lord_T", "position": [CIRCLE[0] + 200.0, CIRCLE[1] + 60.0, CIRCLE[2] + 300.0], "label": T("Lord of the Tide", "Herr der Gezeiten"),
     "peaceful": False, "level": 30},
    {"uid": U("npc/guard1"), "name": "MKC_Tide_Guard_1", "template": "MKC_Ash_Raider_T", "position": [CIRCLE[0] - 300.0, CIRCLE[1] + 60.0, CIRCLE[2] + 400.0], "peaceful": False},
    {"uid": U("npc/guard2"), "name": "MKC_Tide_Guard_2", "template": "MKC_Ash_Raider_T", "position": [CIRCLE[0] + 500.0, CIRCLE[1] + 60.0, CIRCLE[2] + 400.0], "peaceful": False},
]

# ---------------------------------------------------------------------------------------------------------------------------------
# dialogues
# ---------------------------------------------------------------------------------------------------------------------------------
def maren_dialog():
    g = Graph("dlg/maren")
    row = [0]

    def option(key, text, info, permanent=False, typ="Info", y=None):
        r = row[0]
        row[0] += 1
        g.add(key, "option", {"text": text, "type": typ, "permanent": permanent, "info": info}, x=0, y=(r * 230 if y is None else y))
        return key

    def line(key, text, speaker="npc", col=1, y=None, sound=None, base=None):
        yy = g.nodes[[n["uid"] for n in g.nodes].index(g.ids[base])]["pos"][1] if base else y
        d = {"speaker": speaker, "text": text}
        if sound:
            d["sound"] = sound
        g.add(key, "line", d, x=330 * col, y=yy)
        return key

    def at(base_key, col):
        return [330.0 * col, g.nodes[[n["uid"] for n in g.nodes].index(g.ids[base_key])]["pos"][1]]

    def node(key, typ, data, base, col):
        x, y = at(base, col)
        g.add(key, typ, data, x=x, y=y)
        return key

    def cond(key, kind, args, base, dy=0):
        x, y = at(base, 0)
        g.add(key, "cond", {"kind": kind, "args": args}, x=-420.0, y=y + dy)
        return key

    # who are you (voiced: the sound is an Ogg/WAV for every voice language, another one for German)
    option("o_who", T("Who are you?", "Wer bist du?"), "MKC_Maren_Who", permanent=True)
    node("l_who", "line", {"speaker": "npc", "text": T("I am Maren. I write down what the island forgets.", "Ich bin Maren. Ich schreibe auf, was die Insel vergisst."),
                           "sound": {"default": "maren_who.wav", "German": "maren_who_de.wav"}}, "o_who", 1)
    g.link("o_who", "then", "l_who")

    # work: gated by the game variable the event "Chronicle begins" sets
    option("o_work", T("Do you have work for me?", "Hast du Arbeit fuer mich?"), "MKC_Maren_Work")
    cond("c_work", "var", {"name": "mkc_ch1", "value": 1}, "o_work")
    g.link("c_work", "out", "o_work", "when")
    node("l_work", "line", {"speaker": "npc", "text": T("The ink is running low. Bring me three pearls from the shore: salt pearls make the best ink.", "Die Tinte wird knapp. Bring mir drei Perlen vom Strand: Salzperlen geben die beste Tinte.")}, "o_work", 1)
    node("l_work2", "line", {"speaker": "player", "text": T("Pearls. Fine.", "Perlen. Gut.")}, "o_work", 2)
    node("q_work", "quest", {"quest": "Q_MKC_Ink", "action": "start"}, "o_work", 3)
    node("g_work", "log", {"quest": "Q_MKC_Ink", "text": T("Maren needs three pearls for her ink.", "Maren braucht drei Perlen fuer ihre Tinte.")}, "o_work", 4)
    g.chain("l_work", "l_work2", "q_work", "g_work")
    g.link("o_work", "then", "l_work")

    # turn in: engine conditions (quest running AND three pearls), the Give command, and the talk objective of the quest
    option("o_ink", T("Here are your pearls.", "Hier sind deine Perlen."), "MKC_Maren_Ink_Turnin")
    cond("c_ink_q", "quest", {"quest": "Q_MKC_Ink", "status": "Running"}, "o_ink", -50)
    cond("c_ink_i", "item", {"item": "It_Pearl", "count": 3}, "o_ink", 50)
    node("a_ink", "all", {}, "o_ink", -1)
    g.nodes[-1]["pos"][0] = -210.0
    g.link("c_ink_q", "out", "a_ink", "in")
    g.link("c_ink_i", "out", "a_ink", "in")
    g.link("a_ink", "out", "o_ink", "when")
    node("m_ink", "command", {"command": "Give", "params": {"Entity1": "player", "Entity2": "npc", "Item": "It_Pearl", "Amount": 3}}, "o_ink", 1)
    node("l_ink", "line", {"speaker": "npc", "text": T("Salt and shine. Take this plan: it will turn your iron into a story.", "Salz und Glanz. Nimm diesen Plan: er macht aus deinem Eisen eine Geschichte.")}, "o_ink", 2)
    node("m_ink2", "command", {"command": "SetGameEvent", "params": {"GameEvent": "MKC_Maren_Paid"}}, "o_ink", 3)
    g.chain("m_ink", "l_ink", "m_ink2")
    g.link("o_ink", "then", "m_ink")

    # the chronicle: a parent option with sub-options, one of them only after chapter 2 has begun
    option("o_lore", T("Tell me about the Chronicle.", "Erzaehl mir von der Chronik."), "MKC_Maren_Lore", permanent=True)
    node("l_lore", "line", {"speaker": "npc", "text": T("What do you want to hear?", "Was willst du hoeren?")}, "o_lore", 1)
    g.link("o_lore", "then", "l_lore")
    g.add("o_ash", "option", {"text": T("About the ash.", "Ueber die Asche."), "type": "Info", "permanent": True, "info": "MKC_Maren_Ash"}, x=330.0, y=g.nodes[-1]["pos"][1] + 140)
    g.add("l_ash", "line", {"speaker": "npc", "text": T("The ash falls from a sky that no longer has a sun.", "Die Asche faellt von einem Himmel, der keine Sonne mehr hat.")}, x=660.0, y=g.nodes[-1]["pos"][1])
    g.link("o_ash", "then", "l_ash")
    g.add("o_tide", "option", {"text": T("About the tide.", "Ueber die Flut."), "type": "Info", "permanent": True, "info": "MKC_Maren_Tide"}, x=330.0, y=g.nodes[-1]["pos"][1] + 140)
    g.add("c_tide", "cond", {"kind": "var", "args": {"name": "mkc_ch2", "value": 1}}, x=-90.0, y=g.nodes[-1]["pos"][1])
    g.link("c_tide", "out", "o_tide", "when")
    g.add("l_tide", "line", {"speaker": "npc", "text": T("The tide brings the raiders and takes the dead. It will rise four times.", "Die Flut bringt die Pluenderer und holt die Toten. Sie wird viermal steigen.")}, x=660.0, y=g.nodes[-1]["pos"][1])
    g.link("o_tide", "then", "l_tide")
    g.link("o_lore", "choices", "o_ash", "parent")
    g.link("o_lore", "choices", "o_tide", "parent")
    row[0] += 1

    # chapter 2: pay the tithe instead of fighting (the second path of the quest Q_MKC_Camp)
    option("o_tithe", T("I will pay the raiders' tithe (200 gold).", "Ich zahle den Zoll der Pluenderer (200 Gold)."), "MKC_Maren_Tithe")
    cond("c_tithe_q", "quest", {"quest": "Q_MKC_Camp", "status": "Running"}, "o_tithe", -50)
    cond("c_tithe_i", "item", {"item": "It_Gold", "count": 200}, "o_tithe", 50)
    node("a_tithe", "all", {}, "o_tithe", -1)
    g.nodes[-1]["pos"][0] = -210.0
    g.link("c_tithe_q", "out", "a_tithe", "in")
    g.link("c_tithe_i", "out", "a_tithe", "in")
    g.link("a_tithe", "out", "o_tithe", "when")
    node("m_tithe", "command", {"command": "Give", "params": {"Entity1": "player", "Entity2": "npc", "Item": "It_Gold", "Amount": 200}}, "o_tithe", 1)
    node("l_tithe", "line", {"speaker": "npc", "text": T("Gold buys silence. The camp will leave you alone, and so will the tide, for a while.", "Gold kauft Schweigen. Das Lager laesst dich in Ruhe, und die Flut auch, eine Weile.")}, "o_tithe", 2)
    g.chain("m_tithe", "l_tithe")
    g.link("o_tithe", "then", "m_tithe")

    # chapter 3: a dialogue switches a stock story sector (command SetSectorStatus); once, remembered as a game event
    option("o_trail", T("Show me the old trail.", "Zeig mir den alten Pfad."), "MKC_Maren_Trail")
    cond("c_trail_v", "var", {"name": "mkc_ch3", "value": 1}, "o_trail", -50)
    cond("c_trail_e", "event", {"name": "MKC_Trail_Shown"}, "o_trail", 50)
    node("n_trail", "not", {}, "o_trail", -1)
    g.nodes[-1]["pos"] = [-210.0, g.nodes[-1]["pos"][1] + 50]
    g.link("c_trail_e", "out", "n_trail", "in")
    node("a_trail", "all", {}, "o_trail", -1)
    g.nodes[-1]["pos"][0] = -20.0 - 190
    g.nodes[-1]["pos"][1] -= 60
    g.link("c_trail_v", "out", "a_trail", "in")
    g.link("n_trail", "out", "a_trail", "in")
    g.link("a_trail", "out", "o_trail", "when")
    node("m_trail", "command", {"command": "SetSectorStatus", "params": {"SectorName": "OutMain_Story_Chapter3_Lizards_Off", "SectorEnabled": True}}, "o_trail", 1)
    node("m_trail2", "command", {"command": "SetGameEvent", "params": {"GameEvent": "MKC_Trail_Shown"}}, "o_trail", 2)
    node("l_trail", "line", {"speaker": "npc", "text": T("Follow the smoke. The lizards have woken on the old trail.", "Folge dem Rauch. Auf dem alten Pfad sind die Echsen erwacht.")}, "o_trail", 3)
    g.chain("m_trail", "m_trail2", "l_trail")
    g.link("o_trail", "then", "m_trail")

    # chapter 4: any of two conditions, a script node
    option("o_done", T("Is the Chronicle complete?", "Ist die Chronik vollstaendig?"), "MKC_Maren_Done", permanent=True)
    cond("c_done_q", "quest", {"quest": "Q_MKC_Finale", "status": "Success"}, "o_done", -50)
    cond("c_done_v", "var", {"name": "mkc_finale_done", "value": 1}, "o_done", 50)
    node("y_done", "any", {}, "o_done", -1)
    g.nodes[-1]["pos"][0] = -210.0
    g.link("c_done_q", "out", "y_done", "in")
    g.link("c_done_v", "out", "y_done", "in")
    g.link("y_done", "out", "o_done", "when")
    node("l_done", "line", {"speaker": "npc", "text": T("Almost. The last line is yours to write.", "Fast. Die letzte Zeile schreibst du.")}, "o_done", 1)
    node("s_done", "script", {"script": "MKC_TideBell"}, "o_done", 2)
    g.chain("l_done", "s_done")
    g.link("o_done", "then", "l_done")

    option("o_bye", T("Farewell.", "Lebewohl."), "MKC_Maren_Bye", permanent=True)
    node("e_bye", "end", {}, "o_bye", 1)
    g.link("o_bye", "then", "e_bye")
    g.add("note", "comment", {"text": "Maren: the dialogue of every chapter. The options appear when their conditions hold: variables set by the event graphs, quest states, items carried."}, x=400.0, y=-150.0)
    return {"uid": U("dlg/maren"), "name": "Maren", "owner": "MKC_Maren", "graph": g.json(), "notes": "The chronicler: quest giver, lore, tithe, trail, finale."}


def scout_dialog():
    g = Graph("dlg/scout")
    g.add("o1", "option", {"text": T("What happened to you?", "Was ist dir passiert?"), "type": "Info", "permanent": True, "info": "MKC_Scout_Hurt"}, x=0.0, y=0.0)
    g.add("l1", "line", {"speaker": "npc", "text": T("Raiders at the Ash Camp. They burned my boat.", "Pluenderer im Aschelager. Sie haben mein Boot verbrannt.")}, x=330.0, y=0.0)
    g.add("l1b", "line", {"speaker": "MKC_Maren", "text": T("(Maren, from afar) Write that down, someone.", "(Maren, aus der Ferne) Schreibt das jemand auf.")}, x=660.0, y=0.0)
    g.link("o1", "then", "l1")
    g.link("l1", "next", "l1b")
    g.add("o2", "option", {"text": T("Take this, you look half dead.", "Nimm das, du siehst halb tot aus."), "type": "Info", "permanent": False, "info": "MKC_Scout_Gift"}, x=0.0, y=200.0)
    g.add("c2", "cond", {"kind": "item", "args": {"item": "It_Apple", "count": 1}}, x=-380.0, y=200.0)
    g.link("c2", "out", "o2", "when")
    g.add("m2", "command", {"command": "Give", "params": {"Entity1": "player", "Entity2": "npc", "Item": "It_Apple", "Amount": 1}}, x=330.0, y=200.0)
    g.add("m3", "command", {"command": "Give", "params": {"Entity1": "npc", "Entity2": "player", "Item": "It_Po_Health_01", "Amount": 1}}, x=660.0, y=200.0)
    g.add("l2", "line", {"speaker": "npc", "text": T("Thank you. Take a potion; I owe you.", "Danke. Nimm einen Trank; ich stehe in deiner Schuld.")}, x=990.0, y=200.0)
    g.chain("m2", "m3", "l2")
    g.link("o2", "then", "m2")
    return {"uid": U("dlg/scout"), "name": "Scout", "owner": "MKC_Scout", "graph": g.json()}


def smuggler_dialog():
    g = Graph("dlg/smuggler")
    g.add("o1", "option", {"text": T("What do you sell?", "Was verkaufst du?"), "type": "Info", "permanent": True, "info": "MKC_Smuggler_Trade"}, x=0.0, y=0.0)
    g.add("l1", "line", {"speaker": "npc", "text": T("Things the Inquisition would rather you did not own.", "Dinge, die die Inquisition lieber nicht in deinem Besitz saehe.")}, x=330.0, y=0.0)
    g.add("m1", "command", {"command": "DoTrade", "params": {"Entity1": "player", "Entity2": "npc"}}, x=660.0, y=0.0)
    g.chain("l1", "m1")
    g.link("o1", "then", "l1")
    g.add("o2", "option", {"text": T("I know the Chronicle.", "Ich kenne die Chronik."), "type": "Important", "permanent": False, "info": "MKC_Smuggler_Chronicle"}, x=0.0, y=200.0)
    g.add("c2", "cond", {"kind": "quest", "args": {"quest": "Q_MKC_Ink", "status": "Success"}}, x=-380.0, y=200.0)
    g.link("c2", "out", "o2", "when")
    g.add("l2", "line", {"speaker": "npc", "text": T("Then you know why the tide is wrong. Take a scroll from me, for free.", "Dann weisst du, warum die Flut falsch ist. Nimm eine Schriftrolle, umsonst.")}, x=330.0, y=200.0)
    g.add("m2", "command", {"command": "Give", "params": {"Entity1": "npc", "Entity2": "player", "Item": "MKC_Ember_Whisper_Scroll", "Amount": 1}}, x=660.0, y=200.0)
    g.chain("l2", "m2")
    g.link("o2", "then", "l2")
    return {"uid": U("dlg/smuggler"), "name": "Smuggler", "owner": "MKC_Smuggler", "graph": g.json()}


project["dialogs"] = [maren_dialog(), scout_dialog(), smuggler_dialog()]

# ---------------------------------------------------------------------------------------------------------------------------------
# quests
# ---------------------------------------------------------------------------------------------------------------------------------
def quest(name, title, qtype, nodes_fn, xp=None, silent=False, notes=None):
    g = Graph("quest/" + name)
    nodes_fn(g)
    q = {"uid": U("quest/" + name), "name": name, "title": title, "type": qtype, "graph": g.json()}
    if xp is not None:
        q["xp"] = xp
    if silent:
        q["silent"] = True
    if notes:
        q["notes"] = notes
    return q


def q_chronicle(g):
    g.add("s", "start", {"trigger": "game_start"}, 0, 0)
    g.add("l", "log", {"text": T("Somewhere on the beach a chronicler writes down the island's story.", "Irgendwo am Strand schreibt eine Chronistin die Geschichte der Insel auf.")}, 1, 0)
    g.add("o", "objective", {"kind": "var", "name": "mkc_finale_done", "value": 1, "text": T("Help the Chronicle reach its last page.", "Hilf der Chronik bis zu ihrer letzten Seite.")}, 2, 0)
    g.add("r", "reward", {"xp": 500, "gold": 0, "items": [["MKC_Tide_Amulet", 1]], "var": "mkc_chronicle_done", "var_value": 1}, 3, 0)
    g.add("e", "outcome", {"result": "success", "text": T("The Chronicle is complete.", "Die Chronik ist vollstaendig.")}, 4, 0)
    g.chain("s", "l", "o", "r", "e")


def q_ink(g):
    g.add("s", "start", {"trigger": "dialog"}, 0, 0)
    g.add("o1", "objective", {"kind": "items", "item": "It_Pearl", "count": 3, "text": T("Find three pearls on the shore.", "Finde drei Perlen am Strand.")}, 1, 0)
    g.add("o2", "objective", {"kind": "talk", "info": "MKC_Maren_Ink_Turnin", "text": T("Bring the pearls to Maren.", "Bring die Perlen zu Maren.")}, 2, 0)
    g.add("r", "reward", {"xp": 120, "gold": 40, "items": [["MKC_Recipe_Chronicle_Blade", 1], ["It_Po_Health_01", 2]]}, 3, 0)
    g.add("e", "outcome", {"result": "success", "text": T("Maren has her ink, and I have a smithing plan.", "Maren hat ihre Tinte und ich einen Schmiedeplan."), "next_quest": "Q_MKC_Scout"}, 4, 0)
    g.chain("s", "o1", "o2", "r", "e")


def q_scout(g):
    g.add("s", "start", {"trigger": "dialog"}, 0, 0)
    g.add("l", "log", {"text": T("A scout lies wounded near Maren's camp. He saw something in the west.", "Ein Kundschafter liegt verwundet bei Marens Lager. Er hat im Westen etwas gesehen.")}, 1, 0)
    g.add("o", "objective", {"kind": "talk", "info": "MKC_Scout_Hurt", "text": T("Talk to the wounded scout.", "Sprich mit dem verwundeten Kundschafter.")}, 2, 0)
    g.add("r", "reward", {"xp": 60, "items": [["It_Bread", 2]]}, 3, 0)
    g.add("e", "outcome", {"result": "success", "text": T("Raiders have a camp in the west.", "Pluenderer haben ein Lager im Westen.")}, 4, 0)
    g.chain("s", "l", "o", "r", "e")


def q_wolves(g):
    g.add("s", "start", {"trigger": "kill", "target": "Wolf"}, 0, 0)
    g.add("l", "log", {"text": T("Ash-fed wolves roam the shore. Two more of them have to go.", "Aschegefressene Woelfe streifen am Strand. Zwei weitere muessen weg.")}, 1, 0)
    g.add("o", "objective", {"kind": "kill", "target": "Wolf", "count": 2, "text": T("Kill two more wolves.", "Toete zwei weitere Woelfe.")}, 2, 0)
    g.add("r", "reward", {"xp": 80, "items": [["MKC_Tide_Potion", 1]]}, 3, 0)
    g.add("e", "outcome", {"result": "success", "text": T("The shore is quieter.", "Der Strand ist ruhiger.")}, 4, 0)
    g.chain("s", "l", "o", "r", "e")


def q_camp(g):
    g.add("s", "start", {"trigger": "dialog"}, 0, 1)
    g.add("l", "log", {"text": T("Chapter 2: the raiders' Ash Camp has appeared in the west.", "Kapitel 2: Das Aschelager der Pluenderer ist im Westen aufgetaucht.")}, 1, 1)
    g.add("b", "branch", {}, 2, 1)
    g.add("oa", "objective", {"kind": "kill", "target": "MKC_Ash_Warlord", "count": 1, "text": T("Kill the Ash Warlord.", "Toete den Aschekriegsherrn.")}, 3, 0)
    g.add("ra", "reward", {"xp": 400, "items": [["MKC_Tide_Potion", 2]]}, 4, 0)
    g.add("ea", "outcome", {"result": "success", "text": T("The Warlord is dead and the camp is broken.", "Der Kriegsherr ist tot und das Lager zerschlagen.")}, 5, 0)
    g.add("ob", "objective", {"kind": "talk", "info": "MKC_Maren_Tithe", "text": T("Or pay the tithe to Maren (200 gold).", "Oder zahle den Zoll an Maren (200 Gold).")}, 3, 2)
    g.add("rb", "reward", {"xp": 100}, 4, 2)
    g.add("eb", "outcome", {"result": "success", "text": T("The raiders were bought off.", "Die Pluenderer wurden bezahlt.")}, 5, 2)
    g.chain("s", "l", "b")
    g.link("b", "next", "oa")
    g.link("b", "next", "ob")
    g.chain("oa", "ra", "ea")
    g.chain("ob", "rb", "eb")


def q_beacon(g):
    g.add("s", "start", {"trigger": "quest", "quest": "Q_MKC_Camp", "status": "Success"}, 0, 0)
    g.add("l", "log", {"text": T("The camp is broken, but the tide is not done with the island.", "Das Lager ist zerschlagen, aber die Flut ist mit der Insel noch nicht fertig.")}, 1, 0)
    g.add("o1", "objective", {"kind": "var", "name": "mkc_ch3", "value": 1, "text": T("Wait for the third chapter of the tide.", "Warte auf das dritte Kapitel der Flut.")}, 2, 0)
    g.add("o2", "objective", {"kind": "kill", "target": "MKC_Ash_Raider_T", "count": 3, "text": T("Defend the stone circle: kill three raiders.", "Verteidige den Steinkreis: toete drei Pluenderer.")}, 3, 0)
    g.add("r", "reward", {"xp": 250, "gold": 100, "items": [["It_Plant_Health_02", 2]]}, 4, 0)
    g.add("e", "outcome", {"result": "success", "text": T("The beacon holds.", "Das Leuchtfeuer haelt.")}, 5, 0)
    g.chain("s", "l", "o1", "o2", "r", "e")


def q_finale(g):
    g.add("s", "start", {"trigger": "item", "item": "MKC_Tide_Key"}, 0, 0)
    g.add("l", "log", {"text": T("The Key of the Tide hums. The Lord of the Tide waits in the stone circle.", "Der Schluessel der Gezeiten summt. Der Herr der Gezeiten wartet im Steinkreis.")}, 1, 0)
    g.add("o", "objective", {"kind": "kill", "target": "MKC_Tide_Lord", "count": 1, "text": T("Kill the Lord of the Tide in the stone circle.", "Toete den Herrn der Gezeiten im Steinkreis.")}, 2, 0)
    g.add("r", "reward", {"xp": 800, "gold": 500, "items": [["MKC_Tide_Potion", 3]], "var": "mkc_finale_done", "var_value": 1}, 3, 0)
    g.add("e", "outcome", {"result": "success", "text": T("The tide has turned.", "Die Flut hat sich gewendet.")}, 4, 0)
    g.chain("s", "l", "o", "r", "e")


def q_epilogue(g):
    g.add("s", "start", {"trigger": "var", "name": "mkc_finale_done", "value": 1}, 0, 0)
    g.add("l", "log", {"text": T("Maren waits on the beach with a fresh page.", "Maren wartet am Strand mit einer neuen Seite.")}, 1, 0)
    g.add("r", "reward", {"xp": 100}, 2, 0)
    g.add("e", "outcome", {"result": "success", "text": T("The last line is written.", "Die letzte Zeile ist geschrieben.")}, 3, 0)
    g.chain("s", "l", "r", "e")


project["quests"] = [
    quest("Q_MKC_Chronicle", T("The Ashen Chronicle", "Die Aschechronik"), "Report", q_chronicle, notes="The main quest: started with every new game, ends with the finale."),
    quest("Q_MKC_Ink", T("Pearls for the Ink", "Perlen fuer die Tinte"), "HasItems", q_ink, xp=60),
    quest("Q_MKC_Scout", T("The Wounded Scout", "Der verwundete Kundschafter"), "Report", q_scout),
    quest("Q_MKC_Wolves", T("Ash-fed Wolves", "Aschegefressene Woelfe"), "Kill", q_wolves, silent=True),
    quest("Q_MKC_Camp", T("The Ash Camp", "Das Aschelager"), "Kill", q_camp),
    quest("Q_MKC_Beacon", T("The Beacon", "Das Leuchtfeuer"), "Kill", q_beacon),
    quest("Q_MKC_Finale", T("Lord of the Tide", "Herr der Gezeiten"), "Kill", q_finale),
    quest("Q_MKC_Epilogue", T("The Last Line", "Die letzte Zeile"), "Report", q_epilogue),
]

# ---------------------------------------------------------------------------------------------------------------------------------
# event graphs (the Events tab)
# ---------------------------------------------------------------------------------------------------------------------------------
events = []


def event(name, builder, notes=None):
    g = Graph("event/" + name)
    builder(g)
    e = {"uid": U("event/" + name), "name": name, "graph": g.json()}
    if notes:
        e["notes"] = notes
    events.append(e)


def hud(text, style="note"):
    return {"text": text, "style": style}


def ev_begin(g):
    g.add("t", "on_session", {"mode": "new"}, 0, 0)
    g.add("s", "sequence", {}, 1, 0)
    g.add("v", "set_variable", {"scope": "game", "name": "mkc_ch1", "op": "set", "value": 1}, 2, 0)
    g.add("h", "hud_message", hud(T("The Ashen Chronicle begins. Find Maren on the beach.", "Die Aschechronik beginnt. Finde Maren am Strand.")), 2, 1)
    g.add("l", "log", {"text": "chronicle: a new game began"}, 2, 2)
    g.add("e", "end", {}, 3, 1)
    g.link("t", "next", "s")
    g.link("s", "1", "v")
    g.link("s", "2", "h")
    g.link("s", "3", "l")
    g.link("h", "next", "e")


def ev_tablet(g):
    g.add("t", "on_item", {"item": "MKC_Chronicle_Tablet", "what": "picked_up"}, 0, 0)
    g.add("o", "once", {}, 1, 0)
    g.add("x", "give_xp", {"amount": 60}, 2, 0)
    g.add("v", "set_variable", {"scope": "save", "name": "mkc_fragments", "op": "add", "value": 1}, 2, 1)
    g.add("h", "hud_message", hud(T("A fragment of the Chronicle: {item}", "Ein Bruchstueck der Chronik: {item}"), "experience"), 3, 0)
    g.add("a", "log", {"text": "tablet already collected"}, 2, 2)
    g.link("t", "next", "o")
    g.link("o", "next", "x")
    g.link("o", "next", "v")
    g.link("x", "next", "h")
    g.link("o", "again", "a")


def ev_pearls(g):
    g.add("t", "on_item", {"item": "It_Pearl", "what": "added"}, 0, 0)
    g.add("c", "condition", cset({"kind": "quest", "quest": "Q_MKC_Ink", "status": "Running"}, {"kind": "item", "item": "It_Pearl", "count": 1}), 1, 0)
    g.add("d", "cooldown", {"seconds": 30}, 2, 0)
    g.add("h", "hud_message", hud(T("Maren will want these pearls.", "Maren wird diese Perlen wollen."), "log"), 3, 0)
    g.link("t", "next", "c")
    g.link("c", "then", "d")
    g.link("d", "next", "h")


def ev_ink_done(g):
    g.add("t", "on_quest", {"quest": "Q_MKC_Ink", "status": "Success"}, 0, 0)
    g.add("g", "give_item", {"item": "It_Plant_Health_01", "count": 2, "show": True}, 1, 0)
    g.add("s", "send_signal", {"name": "ink_done"}, 2, 0)
    g.link("t", "next", "g")
    g.link("g", "next", "s")


def ev_ink_after(g):
    g.add("t", "on_signal", {"name": "ink_done"}, 0, 0)
    g.add("d", "delay", {"seconds": 2}, 1, 0)
    g.add("h", "hud_message", hud(T("Maren: 'A scout came back wounded. Talk to him.'", "Maren: 'Ein Kundschafter kam verwundet zurueck. Sprich mit ihm.'")), 2, 0)
    g.add("v", "set_variable", {"scope": "game", "name": "mkc_scout_hint", "op": "set", "value": 1}, 3, 0)
    g.chain("t", "d", "h", "v")


def ev_lore(g):
    g.add("t", "on_dialogue", {"info": "MKC_Maren_Lore"}, 0, 0)
    g.add("c", "condition", cset({"kind": "event", "name": "MKC_Maren_Paid"}), 1, 0)
    g.add("v", "set_variable", {"scope": "game", "name": "mkc_lore", "op": "set", "value": 1}, 2, 0)
    g.add("h", "hud_message", hud(T("Maren remembers the pearls you brought.", "Maren erinnert sich an die Perlen, die du brachtest.")), 3, 0)
    g.add("h2", "hud_message", hud(T("Maren has heard nothing yet to write down.", "Maren hat noch nichts zum Aufschreiben gehoert.")), 2, 1)
    g.chain("t", "c")
    g.link("c", "then", "v")
    g.link("v", "next", "h")
    g.link("c", "else", "h2")


def ev_walker(g):
    g.add("t", "on_move", {"min_m": 50}, 0, 0)
    g.add("v", "set_variable", {"scope": "save", "name": "mkc_steps", "op": "add", "value": 1}, 1, 0)
    g.add("n", "count", {"n": 20, "repeat": False}, 2, 0)
    g.add("c", "condition", cset({"kind": "guild", "guild": "None"}, {"kind": "area", "at": "entity", "entity": "MKC_Maren", "radius": 30}, mode="any"), 3, 0)
    g.add("g", "give_item", {"item": "It_Apple", "count": 2, "show": True}, 4, 0)
    g.add("h", "hud_message", hud(T("A long walk on the shore: Maren notes it down.", "Ein langer Marsch am Strand: Maren schreibt es auf.")), 4, 1)
    g.chain("t", "v", "n", "c")
    g.link("c", "then", "g")
    g.link("c", "else", "h")


def ev_chapter2(g):
    g.add("t", "on_chapter", {"chapter": 2}, 0, 0)
    g.add("o", "once", {}, 1, 0)
    g.add("c", "condition", cset({"kind": "chapter", "op": ">=", "value": 2}, {"kind": "layer", "layer": "AshCamp", "state": "disabled"}), 2, 0)
    g.add("s", "sequence", {}, 3, 0)
    g.add("l1", "layer", {"layer": "AshCamp", "action": "enable"}, 4, 0)
    g.add("l2", "sector", {"sector": "OutMain_Story_Chapter2_MonsterForFollowPatty_Off", "action": "enable"}, 4, 1)
    g.add("l3", "quest_action", {"quest": "Q_MKC_Camp", "action": "start"}, 4, 2)
    g.add("l4", "set_variable", {"scope": "game", "name": "mkc_ch2", "op": "set", "value": 1}, 4, 3)
    g.add("h", "hud_message", hud(T("Chapter {chapter}: smoke rises in the west. The Ash Camp.", "Kapitel {chapter}: Im Westen steigt Rauch auf. Das Aschelager.")), 5, 3)
    g.add("sg", "send_signal", {"name": "camp_open"}, 6, 3)
    g.link("t", "next", "o")
    g.link("o", "next", "c")
    g.link("c", "then", "s")
    for i, k in enumerate(["l1", "l2", "l3", "l4"]):
        g.link("s", str(i + 1), k)
    g.chain("l4", "h", "sg")


def ev_camp_falls(g):
    g.add("t", "on_kill", {"target": "MKC_Ash_Warlord", "by_player": True}, 0, 0)
    g.add("c", "condition", cset({"kind": "alive", "who": "MKC_Maren"}, vtest("mkc_ch2", "==", 1)), 1, 0)
    g.add("v", "set_variable", {"scope": "game", "name": "mkc_camp_broken", "op": "set", "value": 1}, 2, 0)
    g.add("x", "give_xp", {"amount": 150}, 3, 0)
    g.add("h", "hud_message", hud(T("The Ash Warlord is dead. Maren will hear of it.", "Der Aschekriegsherr ist tot. Maren wird davon hoeren."), "experience"), 4, 0)
    g.add("s", "send_signal", {"name": "camp_down"}, 5, 0)
    g.add("l", "log", {"text": "the warlord fell but Maren or the chapter variable is missing"}, 2, 1)
    g.chain("t", "c")
    g.link("c", "then", "v")
    g.chain("v", "x", "h", "s")
    g.link("c", "else", "l")


def ev_raiders(g):
    g.add("t", "on_kill", {"target": "MKC_Ash_Raider_T", "by_player": True}, 0, 0)
    g.add("c", "condition", cset(vtest("mkc_ch3", "==", 1), {"kind": "skill", "skill": "Strength", "op": ">=", "value": 1}), 1, 0)
    g.add("v", "set_variable", {"scope": "save", "name": "mkc_raiders", "op": "add", "value": 1}, 2, 0)
    g.add("n", "count", {"n": 3, "repeat": False}, 3, 0)
    g.add("d", "set_variable", {"scope": "game", "name": "mkc_ambush_done", "op": "set", "value": 1}, 4, 0)
    g.add("x", "give_xp", {"amount": 200}, 4, 1)
    g.add("hl", "heal", {"hp": 100, "mana": 50}, 4, 2)
    g.add("h", "hud_message", hud(T("The stone circle is clear.", "Der Steinkreis ist frei."), "experience"), 5, 0)
    g.add("s", "send_signal", {"name": "beacon_cleared"}, 6, 0)
    g.link("t", "next", "c")
    g.link("c", "then", "v")
    g.chain("v", "n")
    g.link("n", "next", "d")
    g.link("n", "next", "x")
    g.link("n", "next", "hl")
    g.link("d", "next", "h")
    g.link("h", "next", "s")


def ev_chapter3(g):
    g.add("t", "on_chapter", {"chapter": 3}, 0, 0)
    g.add("o", "once", {}, 1, 0)
    g.add("s", "sequence", {}, 2, 0)
    g.add("v", "set_variable", {"scope": "game", "name": "mkc_ch3", "op": "set", "value": 1}, 3, 0)
    g.add("sc", "sector", {"sector": "Druid_Outdoor_Story_LizardHunter_Off", "action": "enable"}, 3, 1)
    g.add("h", "hud_message", hud(T("Chapter {chapter}: the tide brings smugglers, and the stone circle glows.", "Kapitel {chapter}: Die Flut bringt Schmuggler, und der Steinkreis gluehen.")), 3, 2)
    g.add("sg", "send_signal", {"name": "chapter3"}, 3, 3)
    g.link("t", "next", "o")
    g.link("o", "next", "s")
    for i, k in enumerate(["v", "sc", "h", "sg"]):
        g.link("s", str(i + 1), k)


def ev_beacon(g):
    g.add("t", "on_area", {"at": "point", "x": CIRCLE[0], "y": CIRCLE[1], "z": CIRCLE[2], "radius": 15, "on": "enters"}, 0, 0)
    g.add("c", "condition", cset(vtest("mkc_ch3", "==", 1), vtest("mkc_ambush_done", "==", 0), vtest("mkc_ambushes", "==", 0, "save"),
                                 {"kind": "level", "op": ">=", "value": 1}, {"kind": "chapter", "op": "<=", "value": 3}), 1, 0)
    g.add("v", "set_variable", {"scope": "save", "name": "mkc_ambushes", "op": "set", "value": 1}, 2, 0)
    g.add("h", "hud_message", hud(T("The circle glows. Ash raiders rise from the stones!", "Der Kreis gluehen. Aschepluenderer steigen aus den Steinen!"), "log"), 3, 0)
    g.add("d", "delay", {"seconds": 3}, 4, 0)
    g.add("r", "repeat", {"count": 3}, 5, 0)
    g.add("sp", "spawn_npc", {"template": "MKC_Ash_Raider_T", "name": "", "at": "near_player", "x": 500, "y": 0, "z": 0, "peaceful": False}, 6, 0)
    g.add("sp2", "spawn_npc", {"template": "MKC_Ash_Raider_T", "name": "MKC_Beacon_Wraith", "at": "point", "x": CIRCLE[0] + 400, "y": CIRCLE[1] + 100, "z": CIRCLE[2], "peaceful": False}, 6, 1)
    g.add("l", "log", {"text": "beacon ambush spawned"}, 6, 2)
    g.link("t", "next", "c")
    g.link("c", "then", "v")
    g.chain("v", "h", "d", "r")
    g.link("r", "body", "sp")
    g.link("r", "done", "sp2")
    g.link("r", "done", "l")


def ev_dusk(g):
    g.add("t", "on_time", {"hour": 20, "minute": 0}, 0, 0)
    g.add("c", "condition", cset({"kind": "time", "from": 19, "to": 23}, {"kind": "chance", "percent": 100}), 1, 0)
    g.add("h", "hud_message", hud(T("The tide bell rings.", "Die Gezeitenglocke laeutet.")), 2, 0)
    g.add("r", "run_script", {"script": "MKC_TideBell", "arg": 1}, 3, 0)
    g.link("t", "next", "c")
    g.link("c", "then", "h")
    g.chain("h", "r")


def ev_watchfire(g):
    g.add("t", "on_timer", {"seconds": 1800, "repeat": False}, 0, 0)
    g.add("h", "hud_message", hud(T("Maren has been waiting a long time.", "Maren wartet schon lange.")), 1, 0)
    g.link("t", "next", "h")


def ev_chapter4(g):
    g.add("t", "on_chapter", {"chapter": 4}, 0, 0)
    g.add("o", "once", {}, 1, 0)
    g.add("s", "sequence", {}, 2, 0)
    g.add("v", "set_variable", {"scope": "game", "name": "mkc_ch4", "op": "set", "value": 1}, 3, 0)
    g.add("k", "give_item", {"item": "MKC_Tide_Key", "count": 1, "show": True}, 3, 1)
    g.add("h", "hud_message", hud(T("Chapter {chapter}: the Key of the Tide hums in your pack.", "Kapitel {chapter}: Der Schluessel der Gezeiten summt in deinem Rucksack.")), 3, 2)
    g.link("t", "next", "o")
    g.link("o", "next", "s")
    for i, k in enumerate(["v", "k", "h"]):
        g.link("s", str(i + 1), k)


def ev_tide_rises(g):
    g.add("t", "on_variable", {"scope": "game", "name": "mkc_ch4", "op": ">=", "value": 1}, 0, 0)
    g.add("s", "sequence", {}, 1, 0)
    g.add("l", "layer", {"layer": "TideCircle", "action": "enable"}, 2, 0)
    g.add("sc", "sector", {"sector": "Monastery_Story_LizardAttack_Off", "action": "enable"}, 2, 1)
    g.add("ti", "set_time", {"hour": 21, "minute": 0}, 2, 2)
    g.add("so", "play_sound", {"sound": "sounds/maren_who.wav"}, 2, 3)
    g.link("t", "next", "s")
    for i, k in enumerate(["l", "sc", "ti", "so"]):
        g.link("s", str(i + 1), k)


def ev_lord_falls(g):
    g.add("t", "on_kill", {"target": "MKC_Tide_Lord", "by_player": True}, 0, 0)
    g.add("c", "condition", cset({"kind": "quest", "quest": "Q_MKC_Finale", "status": "Running"}, vtest("mkc_ch4", ">=", 1), mode="any"), 1, 0)
    g.add("s", "sequence", {}, 2, 0)
    g.add("v", "set_variable", {"scope": "game", "name": "mkc_finale_done", "op": "set", "value": 1}, 3, 0)
    g.add("h", "hud_message", hud(T("The Lord of the Tide has fallen. The Chronicle is nearly complete.", "Der Herr der Gezeiten ist gefallen. Die Chronik ist fast vollstaendig."), "experience"), 3, 1)
    g.add("sg", "send_signal", {"name": "chronicle_complete"}, 3, 2)
    g.add("rm", "remove_npc", {"target": "MKC_Ash_Raider_T", "radius": 100}, 3, 3)
    g.link("t", "next", "c")
    g.link("c", "then", "s")
    for i, k in enumerate(["v", "h", "sg", "rm"]):
        g.link("s", str(i + 1), k)


def ev_epilogue(g):
    g.add("t", "on_signal", {"name": "chronicle_complete"}, 0, 0)
    g.add("o", "once", {}, 1, 0)
    g.add("d", "delay", {"seconds": 6}, 2, 0)
    g.add("c", "condition", cset({"kind": "dead", "who": "MKC_Tide_Lord"}, {"kind": "sector", "sector": "Monastery_Story_LizardAttack_Off", "state": "enabled"}, mode="any"), 3, 0)
    g.add("s", "sequence", {}, 4, 0)
    g.add("hl", "heal", {"hp": 500, "mana": 200}, 5, 0)
    g.add("tk", "take_item", {"item": "MKC_Tide_Key", "count": 1}, 5, 1)
    g.add("ch", "set_chapter", {"chapter": 4}, 5, 2)
    g.add("tp", "teleport", {"at": "point", "x": -31900, "y": 650, "z": -4800}, 5, 3)
    g.add("ti", "set_time", {"hour": 8, "minute": 0}, 5, 4)
    g.add("h", "hud_message", hud(T("The Chronicle is complete. Maren waits on the beach.", "Die Chronik ist vollstaendig. Maren wartet am Strand.")), 6, 4)
    g.add("ql", "quest_log", {"quest": "Q_MKC_Chronicle", "text": T("The last page is written.", "Die letzte Seite ist geschrieben.")}, 5, 5)
    g.add("lg", "log", {"text": "the epilogue condition did not hold"}, 4, 1)
    g.chain("t", "o", "d", "c")
    g.link("c", "then", "s")
    g.link("c", "else", "lg")
    for i, k in enumerate(["hl", "tk", "ch", "tp"]):
        g.link("s", str(i + 1), k)
    g.chain("tp", "ti", "h")
    g.link("ti", "next", "ql")


def ev_camp_open(g):
    g.add("t", "on_signal", {"name": "camp_open"}, 0, 0)
    g.add("l", "quest_log", {"quest": "Q_MKC_Camp", "text": T("The scout says the camp lies where the plants grow, west of the beach.", "Der Kundschafter sagt, das Lager liege dort, wo die Pflanzen wachsen, westlich vom Strand.")}, 1, 0)
    g.link("t", "next", "l")


def ev_camp_down(g):
    g.add("t", "on_signal", {"name": "camp_down"}, 0, 0)
    g.add("l", "quest_log", {"quest": "Q_MKC_Chronicle", "text": T("The Ash Camp is broken.", "Das Aschelager ist zerschlagen.")}, 1, 0)
    g.link("t", "next", "l")


def ev_beacon_done(g):
    g.add("t", "on_signal", {"name": "beacon_cleared"}, 0, 0)
    g.add("v", "set_variable", {"scope": "save", "name": "mkc_beacon", "op": "set", "value": 1}, 1, 0)
    g.add("l", "quest_log", {"quest": "Q_MKC_Chronicle", "text": T("The beacon at the stone circle holds.", "Das Leuchtfeuer am Steinkreis haelt.")}, 2, 0)
    g.chain("t", "v", "l")


def ev_chapter3_hint(g):
    g.add("t", "on_signal", {"name": "chapter3"}, 0, 0)
    g.add("d", "delay", {"seconds": 4}, 1, 0)
    g.add("h", "hud_message", hud(T("Smuggler Joss has pitched his tent east of the bench.", "Schmuggler Joss hat sein Zelt oestlich der Bank aufgeschlagen."), "log"), 2, 0)
    g.chain("t", "d", "h")


# the first link of `chain` is pin `next`; graphs that start with a trigger are linked explicitly where they differ
event("Chronicle begins", ev_begin, "On a new game: a game variable (which the dialogue reads), a HUD message and a log line, started in parallel by a Sequence.")
event("Tablet found", ev_tablet, "On item + Once: the first tablet gives experience and counts a save variable.")
event("Pearls in the pocket", ev_pearls, "On item (added) + Condition + Cooldown.")
event("Ink delivered", ev_ink_done, "On quest status: a gift, then a signal to another graph.")
event("After the ink", ev_ink_after, "On signal + Delay (survives a save).")
event("Lore heard", ev_lore, "On dialogue option + the 'game event happened' check.")
event("Walking the shore", ev_walker, "On player moved (50 m) + After N passes + any of guild / area checks.")
event("Chapter 2: the hidden camp", ev_chapter2, "Chapter 2: layer AshCamp on, a stock story sector on, the quest started, a variable, a message, a signal.")
event("Camp falls", ev_camp_falls, "On kill (a named NPC, by the player) + alive / variable checks.")
event("Camp open", ev_camp_open, "On signal: a log line.")
event("Camp down", ev_camp_down, "On signal: a log line in the main quest.")
event("Beacon cleared", ev_beacon_done, "On signal: a save variable and a log line.")
event("Smugglers hint", ev_chapter3_hint, "On signal + Delay.")
event("Raider hunt", ev_raiders, "On kill (a template) + skill check + counter + After N passes + Heal.")
event("Chapter 3: smugglers and the beacon", ev_chapter3, "Chapter 3: a variable, a stock sector (Druid_Outdoor_Story_LizardHunter_Off), a message. The Smugglers layer switches itself (chapter >= 3).")
event("The beacon", ev_beacon, "On enter area (the stone circle) + level / chapter / variable checks + Delay + Repeat spawning raiders.")
event("Dusk bell", ev_dusk, "On time of day (20:00) + time window + chance + Run script (custom.lua).")
event("Old watchfire", ev_watchfire, "On timer: 30 minutes of play.")
event("Chapter 4: the key", ev_chapter4, "Chapter 4: a variable, the Key of the Tide (it starts the finale quest: Start kind 'item').")
event("The tide rises", ev_tide_rises, "On variable (>= 1): layer TideCircle on, the monastery lizard sector on, time of day, a sound.")
event("The Lord falls", ev_lord_falls, "On kill: finale variable, message, signal, Remove NPC (the raiders by template).")
event("Epilogue", ev_epilogue, "On signal + Once + Delay + any-of check (dead NPC / sector): heal, take the key, set chapter, teleport, set time, quest log.")
project["events"] = events

# ---------------------------------------------------------------------------------------------------------------------------------
# layers
# ---------------------------------------------------------------------------------------------------------------------------------
project["layers"] = [
    {"uid": U("layer/beach"), "name": "Beach", "label": T("Maren's beach", "Marens Strand"), "enabled": True,
     "npcs": ["MKC_Maren"], "spawns": [U("spawn/tablet"), U("spawn/bench")],
     "notes": "Chapter 1: on from the start."},
    {"uid": U("layer/camp"), "name": "AshCamp", "label": T("The Ash Camp", "Das Aschelager"), "enabled": False,
     "npcs": ["MKC_Ash_Warlord", "MKC_Ash_Raider_1", "MKC_Ash_Raider_2", "MKC_Ash_Raider_3"],
     "spawns": [U("spawn/fire"), U("spawn/tent_bench")], "containers": [U("cont/camp")],
     "disable_when": cset({"kind": "quest", "quest": "Q_MKC_Beacon", "status": "Success"}),
     "notes": "Chapter 2: switched on by the graph 'Chapter 2: the hidden camp', off when the beacon quest is done."},
    {"uid": U("layer/smugglers"), "name": "Smugglers", "label": T("The smugglers' cove", "Die Schmugglerbucht"), "enabled": False,
     "npcs": ["MKC_Smuggler"], "spawns": [U("spawn/crate")],
     "enable_when": cset({"kind": "chapter", "op": ">=", "value": 3}),
     "notes": "Chapter 3: switches itself on when the chapter reaches 3 (no graph involved)."},
    {"uid": U("layer/circle"), "name": "TideCircle", "label": T("The Tide Circle", "Der Gezeitenkreis"), "enabled": False,
     "npcs": ["MKC_Tide_Lord", "MKC_Tide_Guard_1", "MKC_Tide_Guard_2"], "spawns": [U("spawn/brazier")],
     "disable_when": cset({"kind": "quest", "quest": "Q_MKC_Epilogue", "status": "Success"}),
     "notes": "Chapter 4: switched on by 'The tide rises'."},
]

# ---------------------------------------------------------------------------------------------------------------------------------
# world edits
# ---------------------------------------------------------------------------------------------------------------------------------
project["world"] = {
    "world": "World",
    "spawns": [
        {"uid": U("spawn/tablet"), "template": "MKC_Chronicle_Tablet", "position": [-31880.0, 620.0, -4600.0], "notes": "a fragment of the chronicle, with the imported 3D model"},
        {"uid": U("spawn/bench"), "template": "Object_Interact_Bench_Outdoor", "position": [-31800.0, 600.0, -4550.0], "rotation": [90.0, 0.0, 0.0]},
        {"uid": U("spawn/fire"), "template": "Def_CampFire", "position": [CAMP[0] + 100.0, CAMP[1], CAMP[2] + 50.0]},
        {"uid": U("spawn/tent_bench"), "template": "Object_Interact_Bench_Outdoor", "position": [CAMP[0] - 150.0, CAMP[1], CAMP[2] - 100.0], "rotation": [200.0, 0.0, 0.0]},
        {"uid": U("spawn/crate"), "template": "It_Pearl", "position": [MAREN[0] + 100.0, MAREN[1] + 20.0, MAREN[2] - 100.0], "amount": 2},
        {"uid": U("spawn/brazier"), "template": "Def_CampFire", "position": [CIRCLE[0], CIRCLE[1] + 40.0, CIRCLE[2] + 150.0]},
    ],
    "containers": [
        {"uid": U("cont/spell"), "guid": "2c2c2522-cdad-4dba-aba1-12a5e2054790", "entity_name": "Object_Interact_Animated_Chest_Medium", "layer": "Harbour_Town_Events",
         "items": [["MKC_Ember_Whisper", 1], ["MKC_Ember_Whisper_Scroll", 1], ["It_Gold", 120]]},
        {"uid": U("cont/camp"), "guid": "7bd3ae88-20f1-490c-8c86-f7e02d4f5787", "entity_name": "Object_Interact_Animated_Chest_Medium", "layer": "Harbour_Town_Events",
         "items": [["MKC_Tide_Potion", 2], ["It_Pearl", 3]]},
    ],
    "removals": [
        {"uid": U("rem/torch"), "guid": "01c4332e-efae-459f-9098-ae6c71dbd23d", "entity_name": "Obj_LS_Torch_Ground_01", "layer": "Game_Start_Dyn_Lights"},
    ],
    "moves": [
        {"uid": U("mov/stone"), "guid": "3dbe0473-6c27-4a13-a0c2-86fdafabff88", "entity_name": "Obj_Nat_Stone_Shape_02_Small_L01", "layer": "Game_Start_Dyn",
         "position": [-31910.0, 655.0, -3777.0], "rotation": [33.0, 0.0, 0.0]},
    ],
}

# ---------------------------------------------------------------------------------------------------------------------------------
# assets, behaviours
# ---------------------------------------------------------------------------------------------------------------------------------
project["assets"] = [
    {"uid": U("asset/blade"), "file": "chronicle/images/chronicle_blade.png", "kind": "picture"},
    {"uid": U("asset/amulet"), "file": "chronicle/images/chronicle_amulet.png", "kind": "picture"},
    {"uid": U("asset/who"), "file": "chronicle/audio/maren_who.wav", "kind": "sound"},
    {"uid": U("asset/whode"), "file": "chronicle/audio/maren_who_de.wav", "kind": "sound"},
    {"uid": U("asset/tablet"), "file": "chronicle/assets/MKC_Tablet._xmsh", "kind": "mesh"},
]
project["behaviors"] = [
    {"uid": U("bhv/chronicler"), "name": "chronicler", "file": "chronicle/behaviors/chronicler.lua"},
    {"uid": U("bhv/camp"), "name": "camp_watch", "file": "chronicle/behaviors/camp_watch.lua"},
]
project["editor"] = {}


# ---------------------------------------------------------------------------------------------------------------------------------
def make_assets():
    from PIL import Image, ImageDraw

    for d in ("images", "audio", "models", "assets", "behaviors"):
        os.makedirs(os.path.join(ASSETS, d), exist_ok=True)

    def icon(name, draw_fn, bg):
        im = Image.new("RGBA", (64, 64), bg)
        dr = ImageDraw.Draw(im)
        draw_fn(dr)
        im.save(os.path.join(ASSETS, "images", name))

    def blade(d):
        d.polygon([(30, 6), (36, 6), (38, 40), (28, 40)], fill=(200, 210, 220, 255), outline=(60, 70, 80, 255))
        d.rectangle([20, 40, 46, 46], fill=(120, 80, 30, 255), outline=(40, 25, 10, 255))
        d.rectangle([29, 46, 37, 58], fill=(90, 55, 20, 255), outline=(40, 25, 10, 255))
        d.ellipse([28, 56, 38, 64], fill=(170, 40, 40, 255))

    def amulet(d):
        d.arc([14, -10, 50, 40], 0, 180, fill=(180, 150, 70, 255), width=3)
        d.ellipse([20, 30, 44, 54], fill=(60, 140, 160, 255), outline=(220, 200, 120, 255), width=3)
        d.ellipse([28, 38, 36, 46], fill=(230, 240, 250, 255))

    icon("chronicle_blade.png", blade, (30, 25, 40, 255))
    icon("chronicle_amulet.png", amulet, (25, 35, 45, 255))

    def tone(path, freq, ms=300, rate=22050):
        with wave.open(path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            frames = bytearray()
            n = int(rate * ms / 1000)
            for i in range(n):
                env = min(1.0, i / 400.0, (n - i) / 400.0)
                frames += struct.pack("<h", int(9000 * env * math.sin(2 * math.pi * freq * i / rate)))
            w.writeframes(bytes(frames))

    tone(os.path.join(ASSETS, "audio", "maren_who.wav"), 330)
    tone(os.path.join(ASSETS, "audio", "maren_who_de.wav"), 440, 350)

    # a tablet: a box 36 x 45 x 8 (OBJ right-handed, faces counter-clockwise seen from outside, like mods/meshprobe/probe.obj)
    w_, h_, d_ = 18.0, 45.0, 4.0
    faces = [
        ([(-w_, 0, d_), (w_, 0, d_), (w_, h_, d_), (-w_, h_, d_)], (0, 0, 1)),
        ([(w_, 0, -d_), (-w_, 0, -d_), (-w_, h_, -d_), (w_, h_, -d_)], (0, 0, -1)),
        ([(w_, 0, d_), (w_, 0, -d_), (w_, h_, -d_), (w_, h_, d_)], (1, 0, 0)),
        ([(-w_, 0, -d_), (-w_, 0, d_), (-w_, h_, d_), (-w_, h_, -d_)], (-1, 0, 0)),
        ([(-w_, h_, d_), (w_, h_, d_), (w_, h_, -d_), (-w_, h_, -d_)], (0, 1, 0)),
        ([(-w_, 0, -d_), (w_, 0, -d_), (w_, 0, d_), (-w_, 0, d_)], (0, -1, 0)),
    ]
    lines = ["# the Chronicle Tablet of the example project chronicle.rmk.json (tools/chronicle_gen.py)"]
    for quad, _ in faces:
        for v in quad:
            lines.append("v %g %g %g" % v)
    for _ in faces:
        lines += ["vt 0 0", "vt 1 0", "vt 1 1", "vt 0 1"]
    for _, n in faces:
        lines += ["vn %g %g %g" % n] * 4
    for i in range(len(faces)):
        b = i * 4
        lines.append("f " + " ".join("%d/%d/%d" % (b + k + 1, b + k + 1, b + k + 1) for k in range(4)))
    with open(os.path.join(ASSETS, "models", "tablet.obj"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    with open(os.path.join(ASSETS, "behaviors", "chronicler.lua"), "w", newline="\n") as f:
        f.write(CHRONICLER_LUA)
    with open(os.path.join(ASSETS, "behaviors", "camp_watch.lua"), "w", newline="\n") as f:
        f.write(CAMP_WATCH_LUA)


CHRONICLER_LUA = '''-- chronicler: a behaviour of the example project chronicle.rmk.json (risen.behavior, docs/LUA_API.md "Behaviours").
-- Attached to the template MKC_Maren_T, so every NPC of it (Maren) runs it. Reports through the mod event "chronicle.behavior" (the live scenario
-- `chronicle` follows it). `self.state` is saved with the game: the visit counter survives a load.
return {
    description = "The chronicler greets the player the first time he comes close and counts the visits and the conversations.",
    params = { greeting = "Welcome, traveller. Mind the ash." },
    near = 450,

    start = function(self)
        self.state.visits = self.state.visits or 0
        self.state.talks = self.state.talks or 0
        risen.emit("chronicle.behavior", "start", self.entity:name(), self.resumed, self.state.visits)
    end,

    on_player_near = function(self, player)
        self.state.visits = self.state.visits + 1
        if self.state.visits == 1 then risen.hud.log(self.params.greeting) end
        risen.emit("chronicle.behavior", "near", self.entity:name(), self.state.visits)
    end,

    on_talk = function(self, info)
        self.state.talks = self.state.talks + 1
        risen.emit("chronicle.behavior", "talk", self.entity:name(), self.state.talks)
    end,
}
'''

CAMP_WATCH_LUA = '''-- camp_watch: a behaviour of the example project chronicle.rmk.json, attached to the template MKC_Ash_Warlord_T with a changed text.
return {
    description = "The warlord taunts the player once when he comes near and reports his death.",
    params = { taunt = "Go away!" },
    near = 1500,

    start = function(self)
        self.state.taunted = self.state.taunted or false
        risen.emit("chronicle.behavior", "start", self.entity:name(), self.resumed, self.state.taunted)
    end,

    on_player_near = function(self, player)
        if self.state.taunted then return end
        self.state.taunted = true
        risen.hud.log(self.params.taunt)
        risen.emit("chronicle.behavior", "taunt", self.entity:name(), self.params.taunt)
    end,

    on_death = function(self, killer)
        risen.emit("chronicle.behavior", "death", self.entity:name())
    end,
}
'''


def main():
    if "--assets" in sys.argv:
        make_assets()
    path = os.path.join(OUT, "chronicle.rmk.json")
    with open(path, "w", newline="\n") as f:
        json.dump(project, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("wrote", path)


if __name__ == "__main__":
    main()
