"""Sanity + spot-checks for gns.db (Spells module)."""
import sqlite3
import sys
from pathlib import Path

DB = Path(__file__).resolve().parent.parent / "gns.db"


def main() -> int:
    if not DB.exists():
        print(f"{DB} not found - run 'py gns/build/load_gns.py' first.", file=sys.stderr)
        return 1

    c = sqlite3.connect(DB)
    c.execute("PRAGMA foreign_keys = ON")
    ok = True

    def check(label, got, expected):
        nonlocal ok
        flag = "OK " if got == expected else "XX "
        if got != expected:
            ok = False
        print(f"  [{flag}] {label}: {got} (expected {expected})")

    one = lambda sql, *a: c.execute(sql, a).fetchone()[0]

    print("Counts:")
    check("magic_user_spells", one("select count(*) from magic_user_spells"), 50)
    check("  described (L1-2)", one("select count(*) from magic_user_spells where is_described=1"), 32)
    check("  name-only stubs (L3)", one("select count(*) from magic_user_spells where is_described=0"), 18)
    check("cleric_spells", one("select count(*) from cleric_spells"), 16)
    check("  reversible cleric", one("select count(*) from cleric_spells where is_reversible=1"), 6)
    check("spell_detail_tables", one("select count(*) from spell_detail_tables"), 2)
    check("spell_detail_rows", one("select count(*) from spell_detail_rows"), 11)
    check("all_spells view rows", one("select count(*) from all_spells"), 66)

    print("\nIntegrity:")
    check("detail tables w/ wrong parent count", one(
        "select count(*) from spell_detail_tables "
        "where ((mu_spell_id is not null)+(cleric_spell_id is not null)) != 1"), 0)
    check("NULL name/level", one(
        "select count(*) from all_spells where name is null or spell_level is null"), 0)
    check("reversible<->reversed_name mismatch", one(
        "select count(*) from cleric_spells where (is_reversible=1) != (reversed_name is not null)"), 0)

    print("\nSpot-checks vs source:")
    cp = one("select range_feet from magic_user_spells where name='Charm Person'")
    check("Charm Person range_feet", cp, 120)
    cp_dur = one("select duration_text from magic_user_spells where name='Charm Person'")
    check("Charm Person duration_text is NULL", cp_dur is None, True)
    cp_rows = one(
        "select count(*) from spell_detail_rows r "
        "join spell_detail_tables t on t.id=r.detail_table_id "
        "join magic_user_spells m on m.id=t.mu_spell_id where m.name='Charm Person'")
    check("Charm Person detail rows", cp_rows, 6)
    last = one(
        "select value_text from spell_detail_rows r "
        "join spell_detail_tables t on t.id=r.detail_table_id "
        "join magic_user_spells m on m.id=t.mu_spell_id "
        "where m.name='Charm Person' order by r.row_order desc limit 1")
    check("Charm Person last row value", last, "day")
    sleep_rows = one(
        "select count(*) from spell_detail_rows r "
        "join spell_detail_tables t on t.id=r.detail_table_id "
        "join magic_user_spells m on m.id=t.mu_spell_id where m.name='Sleep'")
    check("Sleep detail rows", sleep_rows, 5)

    # Hold Person exists in BOTH tables (this is exactly why the split is needed).
    hp_mu = one("select count(*) from magic_user_spells where name='Hold Person'")
    hp_cl = one("select count(*) from cleric_spells where name='Hold Person'")
    check("Hold Person in magic_user_spells", hp_mu, 1)
    check("Hold Person in cleric_spells", hp_cl, 1)

    print("\nReversible cleric spells:")
    for name, rev in c.execute(
        "select name, reversed_name from cleric_spells where is_reversible=1 order by name"):
        print(f"    {name}  ->  {rev}")

    print("\nMonster counts:")
    check("total monsters", one("select count(*) from monsters"), 97)
    check("  top-level (parent_id IS NULL)",
          one("select count(*) from monsters where parent_id is null"), 79)
    check("  variants (parent_id NOT NULL)",
          one("select count(*) from monsters where parent_id is not null"), 18)
    check("  group entries (is_group=1)",
          one("select count(*) from monsters where is_group=1"), 4)
    check("monster_detail_tables", one("select count(*) from monster_detail_tables"), 1)
    check("monster_detail_rows (dragon age)",
          one("select count(*) from monster_detail_rows"), 8)

    print("\nMonster integrity:")
    check("every parent_id resolves", one(
        "select count(*) from monsters c left join monsters p on p.id=c.parent_id "
        "where c.parent_id is not null and p.id is null"), 0)
    check("every variant's parent is a group", one(
        "select count(*) from monsters c join monsters p on p.id=c.parent_id "
        "where p.is_group=0"), 0)
    check("NULL monster name", one("select count(*) from monsters where name is null"), 0)

    print("\nMonster spot-checks vs source:")
    def mone(sql, *a):
        return c.execute(sql, a).fetchone()

    rd = mone("select breath_weapon_text, hit_dice_text, "
              "(select name from monsters p where p.id=m.parent_id) "
              "from monsters m where name='Red Dragon'")
    check("Red Dragon breath", rd[0], "fire")
    check("Red Dragon HD text", rd[1], "9-11")
    check("Red Dragon parent", rd[2], "Dragon")
    check("Hill Giant size", mone("select size_text from monsters where name='Hill Giant'")[0], "12'")
    check("Werewolf parent",
          mone("select (select name from monsters p where p.id=m.parent_id) "
               "from monsters m where name='Werewolf'")[0], "Lycanthrope")
    check("Giant Spider treasure_type (was mislabeled 'Attacks')",
          mone("select treasure_type from monsters where name='Giant Spider'")[0], "C")

    # monster_effective resolves inherited stats for a promoted variant.
    eff = mone("select move_text, armor_class_text, treasure_type "
               "from monster_effective where name='Red Dragon'")
    check("Red Dragon inherits move from Dragon", eff[0], "90 feet/turn, 240 feet flying")
    check("Red Dragon inherits AC from Dragon", eff[1], "2")
    check("Red Dragon inherits treasure from Dragon", eff[2], "H")

    print("\nCharacter counts:")
    check("abilities", one("select count(*) from ability"), 6)
    check("alignments", one("select count(*) from alignment"), 5)
    check("races", one("select count(*) from race"), 7)
    check("character_class total", one("select count(*) from character_class"), 11)
    check("  base classes (parent NULL)",
          one("select count(*) from character_class where parent_class_id is null"), 4)
    check("  subclasses", one("select count(*) from character_class where parent_class_id is not null"), 7)
    check("character_sheet", one("select count(*) from character_sheet"), 1)
    check("character_ability rows", one("select count(*) from character_ability"), 6)

    print("\nCharacter integrity / spot-checks:")
    # Classes contain NO races (the whole point of separating them).
    check("no race names leaked into classes", one(
        "select count(*) from character_class where name in "
        "('Human','Elf','Half-elf','Halfling','Orc','Half-Orc','Dwarf')"), 0)
    # Prime requisites resolve correctly.
    def primereq(cls):
        return c.execute(
            "select a.name from character_class cc join ability a "
            "on a.id=cc.prime_requisite_ability_id where cc.name=?", (cls,)).fetchone()[0]
    check("Fighter prime = Strength", primereq("Fighter"), "Strength")
    check("Magic-User prime = Intelligence", primereq("Magic-User"), "Intelligence")
    check("Cleric prime = Wisdom", primereq("Cleric"), "Wisdom")
    check("Thief prime = Dexterity", primereq("Thief"), "Dexterity")
    # Sub-class parents.
    def parent(cls):
        return c.execute(
            "select p.name from character_class c join character_class p "
            "on p.id=c.parent_class_id where c.name=?", (cls,)).fetchone()[0]
    for sub, base in [("Ranger", "Fighter"), ("Paladin", "Fighter"),
                      ("Illusionist", "Magic-User"), ("Witch", "Magic-User"),
                      ("Druid", "Cleric"), ("Monk", "Cleric"), ("Assassin", "Thief")]:
        check(f"{sub} -> {base}", parent(sub), base)
    # Magic-User restriction mentions armor + dagger (from the user's example).
    mu_restr = one("select restrictions from character_class where name='Magic-User'") or ""
    check("Magic-User restriction mentions armor", "armor" in mu_restr.lower(), True)
    check("Magic-User restriction mentions dagger", "dagger" in mu_restr.lower(), True)
    # Race columns populated.
    check("all races have a special ability", one(
        "select count(*) from race where special_ability is null"), 0)
    check("all races have restrictions text", one(
        "select count(*) from race where restrictions is null"), 0)

    print("\nSample character (Morgan Ironwolf) via character_full:")
    mi = mone("select class, base_class, race, alignment, level, armor_class, hit_points, "
              "prime_requisite, earned_xp_bonus_pct from character_full "
              "where character_name='Morgan Ironwolf'")
    check("class", mi[0], "Fighter")
    check("race", mi[2], "Human")
    check("alignment", mi[3], "Lawful Good")
    check("armor_class", mi[5], 3)
    check("prime_requisite resolves", mi[7], "Strength")
    str_row = mone("select ca.score, ca.adjustment_text from character_ability ca "
                   "join ability a on a.id=ca.ability_id "
                   "join character_sheet cs on cs.id=ca.character_sheet_id "
                   "where cs.character_name='Morgan Ironwolf' and a.name='Strength'")
    check("Morgan STR score", str_row[0], 16)
    check("Morgan STR adjustment", str_row[1], "+2")

    print("\nClass progression (class_level):")
    check("class_level rows", one("select count(*) from class_level"), 12)
    def clvl(cls, lvl, col):
        return c.execute(
            f"select {col} from class_level cl join character_class cc on cc.id=cl.class_id "
            "where cc.name=? and cl.level=?", (cls, lvl)).fetchone()[0]
    check("Fighter L1 XP", clvl("Fighter", 1, "experience_points"), 0)
    check("Fighter L2 XP", clvl("Fighter", 2, "experience_points"), 2000)
    check("Fighter L3 XP", clvl("Fighter", 3, "experience_points"), 4000)
    check("Fighter L1 hit_dice", clvl("Fighter", 1, "hit_dice"), "1d8")
    check("Magic-User L1 spells", clvl("Magic-User", 1, "spells"), "1 first level")
    check("Cleric L1 spells (none)", clvl("Cleric", 1, "spells"), "None")
    # Sub-class inherits base progression via the view.
    pal2 = one("select experience_points from class_progression where class='Paladin' and level=2")
    check("Paladin L2 XP inherited from Fighter", pal2, 2000)
    src = one("select source from class_progression where class='Paladin' and level=2")
    check("Paladin progression marked inherited", "inherited from Fighter" in (src or ""), True)

    print("\nEquipment / weapons / armor:")
    check("weapons", one("select count(*) from weapon"), 19)
    check("armor", one("select count(*) from armor"), 4)
    check("armor_class rows", one("select count(*) from armor_class"), 8)
    check("equipment", one("select count(*) from equipment"), 19)
    check("strength_capacity rows", one("select count(*) from strength_capacity"), 16)
    check("Two-handed Sword damage", one("select damage from weapon where name='Two-handed Sword'"), "1d10 (1-10)")
    check("Two-handed Sword damage_max", one("select damage_max from weapon where name='Two-handed Sword'"), 10)
    check("Dagger damage", one("select damage from weapon where name='Dagger'"), "1d4 (1-4)")
    check("Mace is cleric-usable", one("select cleric_usable from weapon where name='Mace'"), 1)
    check("Sword has a weight", one("select weight_lbs from weapon where name='Sword'") is not None, True)
    check("Plate Mail AC", one("select armor_class from armor where name='Plate Mail Armor'"), 3)
    check("Plate Mail weight", one("select weight_lbs from armor where name='Plate Mail Armor'"), 50)
    check("Shield ac_modifier", one("select ac_modifier from armor where name='Shield'"), -1)
    check("AC 2 = Plate Mail & Shield", one("select armor_type from armor_class where armor_class=2"), "Plate Mail Armor & Shield")
    check("STR 18 base capacity", one("select base_capacity_lbs from strength_capacity where strength_score=18"), 150)

    print("\nEncumbrance (race + strength) via character_load:")
    check("Halfling carry modifier", one("select carry_capacity_modifier from race where name='Halfling'"), 0.5)
    check("Human carry modifier", one("select carry_capacity_modifier from race where name='Human'"), 1.0)
    check("Morgan inventory rows", one("select count(*) from character_inventory"), 14)
    load = mone("select weight_carried_lbs, capacity_lbs, max_capacity_lbs, encumbered, "
                "base_movement_ft, effective_movement_ft "
                "from character_load where character_name='Morgan Ironwolf'")
    check("Morgan weight carried (lbs)", load[0], 120)
    check("Morgan capacity (STR16 x Human 1.0)", load[1], 100)
    check("Morgan max capacity", load[2], 200)
    check("Morgan is encumbered (120 > 100)", load[3], 1)

    print("\nCharacter movement:")
    check("Halfling base movement", one("select base_movement_ft from race where name='Halfling'"), 90)
    check("Human base movement", one("select base_movement_ft from race where name='Human'"), 120)
    check("Morgan base movement", load[4], 120)
    check("Morgan effective movement halved (encumbered)", load[5], 60)

    print("\nTreasure:")
    check("treasure types", one("select count(*) from treasure_type"), 20)
    check("magic item categories", one("select count(*) from magic_item_category"), 7)
    check("magic items", one("select count(*) from magic_item"), 70)
    check("Type A maps/magic", one("select maps_magic from treasure_type where code='A'"), "30%: any 3")
    check("Sword category die range low", one("select die_low from magic_item_category where name='Sword'"), 1)
    check("Potion category die low", one("select die_low from magic_item_category where name='Potion'"), 41)
    heal = one("select m.description from magic_item m join magic_item_category c on c.id=m.category_id "
               "where c.name='Potion' and m.name='Healing'")
    check("Healing potion has a description", heal is not None and "healing spell" in heal, True)
    swd = one("select count(*) from magic_item m join magic_item_category c on c.id=m.category_id "
              "where c.name='Sword'")
    check("Sword sub-table has 10 items", swd, 10)

    print("\nMonster treasure-type reference:")
    check("Ogre treasure code", one("select treasure_type_code from monsters where name='Ogre'"), "C")
    check("Bandit treasure code", one("select treasure_type_code from monsters where name='Bandit'"), "A")
    check("Deer (wild animal) has no treasure", one("select treasure_type_code from monsters where name='Deer'") is None, True)
    check("every treasure_type_code is valid (FK)", one(
        "select count(*) from monsters m where m.treasure_type_code is not null "
        "and not exists (select 1 from treasure_type t where t.code=m.treasure_type_code)"), 0)

    print("\nEnvironments & wandering monsters:")
    check("environments", one("select count(*) from environment"), 10)
    check("wandering entries", one("select count(*) from wandering_monster"), 95)
    def env_has(env, mon):
        return one("select count(*) from wandering_monster wm "
                   "join environment e on e.id=wm.environment_id "
                   "join monsters m on m.id=wm.monster_id where e.name=? and m.name=?", env, mon)
    check("Dungeon (L1) includes Skeleton", one(
        "select count(*) from wandering_monster wm join environment e on e.id=wm.environment_id "
        "join monsters m on m.id=wm.monster_id where e.name='Dungeon' and m.name='Skeleton' "
        "and wm.party_level_min<=1 and wm.party_level_max>=1"), 1)
    check("Dungeon does NOT include Elephant", env_has("Dungeon", "Elephant"), 0)
    check("Lowland Valley includes Elephant", env_has("Lowland Valley", "Elephant"), 1)
    check("Lowland Valley does NOT include Skeleton", env_has("Lowland Valley", "Skeleton"), 0)
    # monster_environment view matches the wandering table.
    check("Skeleton common environments (via view)", one(
        "select count(distinct environment) from monster_environment where monster='Skeleton'"), 2)
    check("Elephant common environment", one(
        "select environment from monster_environment where monster='Elephant'"), "Lowland Valley")

    print("\nRules / reference tables:")
    check("ability_adjustment rows", one("select count(*) from ability_adjustment"), 13)
    check("Prime req 15+ XP modifier", one(
        "select modifier from ability_adjustment where applies_to='Prime Requisite' and score_low=15"), 10)
    check("CON 18 hit-die modifier", one(
        "select modifier from ability_adjustment where applies_to='Constitution' and score_low=18"), 3)

    check("attack_table rows", one("select count(*) from attack_table"), 80)
    check("char L1-3 vs AC2 needs 17", one(
        "select roll_needed from attack_table where attacker_kind='character' "
        "and attacker_label='1st-3rd Level Character' and target_ac=2"), 17)
    check("monster HD '11 up' vs AC9 needs 0", one(
        "select roll_needed from attack_table where attacker_kind='monster' "
        "and attacker_label='11 up' and target_ac=9"), 0)

    check("saving_throw groups", one("select count(*) from saving_throw"), 5)
    # Morgan's sheet saves should match the Fighting Man row.
    fm = mone("select vs_death_poison, vs_magic_wand, vs_stone, vs_dragon_breath, vs_spell_staff "
              "from saving_throw where class_group='Fighting Man, Thief, Hobgoblin'")
    check("Fighting Man saves match Morgan's sheet", list(fm), [12, 13, 14, 15, 16])

    check("monster_xp rows", one("select count(*) from monster_xp"), 11)
    check("HD 5+1 XP value", one("select value from monster_xp where hit_dice='5+1'"), 225)

    check("thief_skill rows", one("select count(*) from thief_skill"), 21)
    check("L1 Open Locks 15%", one(
        "select chance_pct from thief_skill where level=1 and skill='Open Locks'"), 15)

    check("turn_undead rows", one("select count(*) from turn_undead"), 24)
    check("L1 cleric turns Skeleton on 7", one(
        "select result from turn_undead where cleric_level=1 and undead_type='Skeleton'"), "7")
    check("L3 cleric auto-turns Zombie (T)", one(
        "select result from turn_undead where cleric_level=3 and undead_type='Zombie'"), "T")

    check("magic_user_intelligence rows", one("select count(*) from magic_user_intelligence"), 8)
    check("INT 10-12 knows 50%", one(
        "select pct_to_know from magic_user_intelligence where int_low=10"), 50)

    check("reaction_table rows", one("select count(*) from reaction_table"), 5)
    check("languages", one("select count(*) from language"), 15)
    check("coins", one("select count(*) from coin"), 5)
    check("platinum worth 5 gp", one("select value_gp from coin where abbreviation='pp'"), 5.0)
    check("treasure_value rows (gems+jewelry)", one("select count(*) from treasure_value"), 6)

    print("\nWeapon ranges & light sources:")
    check("Long Bow long range", one("select range_long_ft from weapon where name='Long Bow'"), 210)
    check("Dagger thrown short range", one("select range_short_ft from weapon where name='Dagger'"), 10)
    check("Torch light radius", one("select light_radius_ft from equipment where name='Torches (6)'"), 30)
    check("Lantern burns 24 turns", one(
        "select light_duration_turns from equipment where name='Lantern'"), 24)

    print("\nResult:", "ALL CHECKS PASSED" if ok else "SOME CHECKS FAILED")
    return 0 if ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
