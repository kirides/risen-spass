#!/usr/bin/env python3
"""Data-free self-test of the pure helpers of tools/extmod_gen.py (no mod, no game data needed):  python -I tools/test_extmod_gen.py"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extmod_gen as G  # noqa: E402

fails = []


def eq(name, got, want):
    if got != want:
        fails.append("%s: got %r, wanted %r" % (name, got, want))


eq("lua scalar", G.lua(5), "5")
eq("lua float", G.lua(1500.0), "1500")
eq("lua float frac", G.lua(0.5), "0.5")
eq("lua string", G.lua('a"b\\c\n'), '"a\\"b\\\\c\\n"')
eq("lua list", G.lua([1, 2]), "{ 1, 2 }")
eq("lua dict sorted", G.lua({"b": 1, "a": 2}), "{ a = 2, b = 1 }")
eq("lua keyword key", G.lua_key("end"), '["end"]')
eq("lua nested", G.lua({"a": {"x": 1}}), "{\n    a = { x = 1 },\n}")
eq("csv plain", G.csv_field("Ruby Blade"), "Ruby Blade")
eq("csv quoted", G.csv_field('He said "hi"; ok'), '"He said ""hi""; ok"')
eq("csv trailing space", G.csv_field("x "), '"x "')
eq("cell placeholder", G.norm_cell("-"), None)
eq("cell artefact", G.norm_cell("Aric''s sword'"), "Aric's sword")
eq("cell wrapped", G.norm_cell("'Hello'"), "Hello")
eq("cell normal", G.norm_cell("Cole's bow"), "Cole's bow")
eq("mesh name", G.norm_mesh("Item_Pearl._xmsh"), "Item_Pearl.xmsh")
eq("mesh plain", G.norm_mesh("Item_Pearl.xmsh"), "Item_Pearl.xmsh")
eq("vec", G.vec_text((0.0, 12.0, -15.5)), "0; 12; -15.5")
ctx = G.Ctx({"enums": {"gEItemHoldType": {"1": "1H"}}, "skills": {"26": "Lockpick"}})
eq("enum known", ctx.enum("bTPropertyContainer<enum gEItemUseType>", 18), "gEItemUseType_Axe")
eq("enum from file", ctx.enum("bTPropertyContainer<enum gEItemHoldType>", 1), "gEItemHoldType_1H")
eq("enum unknown", ctx.enum("bTPropertyContainer<enum gEItemHoldType>", 99), None)
eq("skill known", ctx.skill(7), "Strength")
eq("skill from file", ctx.skill(26), "Lockpick")
eq("skill unknown", ctx.skill(27), None)
r = {"station": "anvil", "ingredients": [("It_IronOre", 2)], "skills": [("Smith", 1)], "result": "It_X", "craft": 7, "amount": 1}
eq("recipe", G.recipe_lua(r), collections.OrderedDict([("station", "anvil"), ("ingredients", [{"item": "It_IronOre", "count": 2}]),
                                                         ("skills", {"Smith": 1}), ("result", "It_X")]))
if fails:
    print("\n".join(fails))
    sys.exit(1)
print("extmod_gen helpers: ok")
