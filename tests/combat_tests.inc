// ---- Damage hooks (M14): the pure logic of src/game/combat_names.h (classification of damage scripts, what a handler may return, filters) ----
// Included by test_main.cpp inside its anonymous namespace.

TEST(combat_classify) {
    using namespace spass::game::combat;
    CHECK(classifyDamageScript("DoMagicFireballDamage").kind == "spell" && classifyDamageScript("DoMagicFireballDamage").spell == "fireball");
    CHECK(classifyDamageScript("DoMagicFrostDamage").spell == "frost");
    CHECK(classifyDamageScript("DoMagicMissileDamage").spell == "missile");
    CHECK(classifyDamageScript("DoInfernoDamage").kind == "spell" && classifyDamageScript("DoInfernoDamage").spell == "inferno");
    for (const char* s : {"DoSwordDamage", "DoAxeDamage", "DoStaffDamage", "DoFistDamage", "DoMeleeDamage", "DoSpecialMonsterDamage"}) {
        CHECK(classifyDamageScript(s).kind == "melee");
        CHECK(classifyDamageScript(s).spell.empty());
    }
    for (const char* s : {"DoProjectileDamage", "DoArrowDamage", "DoBoltDamage"}) {
        CHECK(classifyDamageScript(s).kind == "ranged");
    }
    for (const char* s : {"DoFireDamage", "DoPhysicalDamage", "DoTitanDamage", "DoToucheDamage", "", "SomethingElse", "dosworddamage"}) {
        CHECK(classifyDamageScript(s).kind == "other");
        CHECK(classifyDamageScript(s).spell.empty());
    }
}

TEST(combat_type_and_protection) {
    using namespace spass::game::combat;
    CHECK(damageTypeName("gEDamageType_Magic") == "Magic");
    CHECK(damageTypeName("gEDamageType_Edge") == "Edge");
    CHECK(damageTypeName("Magic") == "Magic");
    CHECK(damageTypeName("").empty());
    CHECK(protectionSkill("Edge") == 10 && protectionSkill("Blunt") == 11 && protectionSkill("Point") == 12);
    CHECK(protectionSkill("Fire") == 13 && protectionSkill("Ice") == 14 && protectionSkill("Magic") == 15);
    CHECK(protectionSkill("None") == -1 && protectionSkill("") == -1 && protectionSkill("magic") == -1);
}

TEST(combat_interpret_result) {
    using namespace spass::game::combat;
    // interpretResult(isNil, isBool, boolValue, isNumber, number)
    Decision d = interpretResult(true, false, false, false, 0);
    CHECK(d.verdict == Verdict::Keep);
    d = interpretResult(false, true, true, false, 0);
    CHECK(d.verdict == Verdict::Keep);                      // true: unchanged
    d = interpretResult(false, true, false, false, 0);
    CHECK(d.verdict == Verdict::Cancel);                    // false: cancel
    d = interpretResult(false, false, false, true, 0.0);
    CHECK(d.verdict == Verdict::Cancel);                    // 0: cancel
    d = interpretResult(false, false, false, true, 0.4);
    CHECK(d.verdict == Verdict::Cancel);                    // rounds to 0
    d = interpretResult(false, false, false, true, 0.5);
    CHECK(d.verdict == Verdict::Replace && d.value == 1);
    d = interpretResult(false, false, false, true, 262.5);
    CHECK(d.verdict == Verdict::Replace && d.value == 263);
    d = interpretResult(false, false, false, true, 175.0);
    CHECK(d.verdict == Verdict::Replace && d.value == 175);
    d = interpretResult(false, false, false, true, 1e12);
    CHECK(d.verdict == Verdict::Replace && d.value == kMaxDamage);
    d = interpretResult(false, false, false, true, -1.0);
    CHECK(d.verdict == Verdict::Invalid && !d.why.empty());
    d = interpretResult(false, false, false, true, std::nan(""));
    CHECK(d.verdict == Verdict::Invalid);
    d = interpretResult(false, false, false, true, HUGE_VAL);
    CHECK(d.verdict == Verdict::Invalid);
    d = interpretResult(false, false, false, true, -HUGE_VAL);
    CHECK(d.verdict == Verdict::Invalid);
    d = interpretResult(false, false, false, false, 0);     // a string, a table, a function ...
    CHECK(d.verdict == Verdict::Invalid && !d.why.empty());
}

TEST(combat_filters) {
    using namespace spass::game::combat;
    Hit fireball;
    fireball.kind = "spell";
    fireball.spell = "fireball";
    fireball.type = "Magic";
    fireball.weapon = "It_Projectile_Fireball_S0";
    fireball.script = "DoMagicFireballDamage";
    fireball.attacker = "PC_Hero";
    fireball.attackerIsPlayer = true;
    fireball.victim = "Wolf_1";
    Hit bite;
    bite.kind = "melee";
    bite.type = "Edge";
    bite.weapon = "It_Fist_Monster";
    bite.script = "DoFistDamage";
    bite.attacker = "Wolf_1";
    bite.victim = "PC_Hero";
    bite.victimIsPlayer = true;

    Filter any;
    CHECK(matches(any, fireball) && matches(any, bite));

    Filter spell;
    spell.kinds = {"spell"};
    CHECK(matches(spell, fireball) && !matches(spell, bite));
    spell.spells = {"frost", "fireball"};
    CHECK(matches(spell, fireball));
    spell.spells = {"frost"};
    CHECK(!matches(spell, fireball));

    Filter weapon;
    weapon.weapons = {"It_Fist_Monster"};
    CHECK(!matches(weapon, fireball) && matches(weapon, bite));
    Filter script;
    script.scripts = {"DoMagicFireballDamage"};
    CHECK(matches(script, fireball) && !matches(script, bite));
    Filter type;
    type.types = {"Edge", "Blunt"};
    CHECK(!matches(type, fireball) && matches(type, bite));

    Filter byPlayer;
    byPlayer.attackerPlayer = true;
    CHECK(matches(byPlayer, fireball) && !matches(byPlayer, bite));
    Filter onPlayer;
    onPlayer.victimPlayer = true;
    CHECK(!matches(onPlayer, fireball) && matches(onPlayer, bite));
    Filter byWolf;
    byWolf.attackers = {"Wolf_1"};
    CHECK(!matches(byWolf, fireball) && matches(byWolf, bite));
    byWolf.attackerPlayer = true;                           // names and "the player" are alternatives
    CHECK(matches(byWolf, fireball) && matches(byWolf, bite));
    Filter victimWolf;
    victimWolf.victims = {"Wolf_1", "Wolf_2"};
    CHECK(matches(victimWolf, fireball) && !matches(victimWolf, bite));

    // all keys must hold together
    Filter both;
    both.kinds = {"spell"};
    both.attackerPlayer = true;
    both.victims = {"Wolf_2"};
    CHECK(!matches(both, fireball));
    both.victims = {"Wolf_1"};
    CHECK(matches(both, fireball));

    // an unnamed attacker (a floor, a trap) only matches filters that do not ask for one
    Hit trap;
    trap.kind = "other";
    CHECK(matches(any, trap) && !matches(byWolf, trap) && !matches(byPlayer, trap));
}

TEST(combat_filter_keys) {
    using namespace spass::game::combat;
    for (const char* k : {"kind", "spell", "type", "weapon", "script", "attacker", "victim"}) {
        CHECK(validFilterKey(k));
    }
    CHECK(!validFilterKey("") && !validFilterKey("Kind") && !validFilterKey("colour") && !validFilterKey("attackers"));
    CHECK(validKind("spell") && validKind("melee") && validKind("ranged") && validKind("other") && !validKind("magic") && !validKind(""));
    CHECK(validSpell("fireball") && validSpell("frost") && validSpell("missile") && validSpell("inferno") && !validSpell("lightning") && !validSpell("Fireball"));
}
