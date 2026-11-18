/* Game balance tables. Tile numbers refer to Kenney's Tiny Dungeon sheet. */
#include "game.h"
#include "assets.h"

/*  tile dmg  cd reach proj      name                (ordered weakest -> strongest) */
const WeaponDef weapons[NUM_WEAPONS] = {
    { 103,  2, 12, 12, 0,          "DAGGER" },
    { 104,  3, 15, 14, 0,          "SHORT SWORD" },
    { 105,  4, 16, 14, 0,          "CLEAVER" },
    { 131,  5, 18, 22, 0,          "SPEAR" },
    { 106,  6, 19, 16, 0,          "BROADSWORD" },
    { 129,  5, 20, 0,  T_ORB,      "ARCANE STAFF" },
    { 119,  8, 22, 14, 0,          "BATTLE AXE" },
    { 117, 11, 28, 15, 0,          "WARHAMMER" },
    { 130,  8, 17, 0,  T_BOLT,     "FROST STAFF" },
    { 118, 12, 25, 17, 0,          "GREAT AXE" },
    { 107, 14, 18, 17, 0,          "FLAME SWORD" },
};

/*  tile  hp dmg spd xp gold behaviour   mindepth proj        name */
const MonDef mons[NUM_MONS] = {
    { 124,  4, 1, 10,  1, 1, BEH_CHASE,   1, 0,          "RAT" },
    { 120,  3, 1, 18,  1, 1, BEH_ERRATIC, 1, 0,          "BAT" },
    { 108,  7, 2,  7,  2, 2, BEH_CHASE,   1, 0,          "SLIME" },
    { 122,  6, 2, 12,  3, 2, BEH_DASH,    2, 0,          "SPIDER" },
    { 121, 10, 3,  8,  4, 3, BEH_GHOST,   3, 0,          "GHOST" },
    { 112, 12, 3, 11,  5, 5, BEH_RANGED,  3, T_ARROW_U,  "BANDIT" },
    { 110, 18, 4, 11,  6, 4, BEH_CHASE,   4, 0,          "FIRE CRAB" },
    { 109, 32, 6,  9,  9, 6, BEH_CHASE,   5, 0,          "CYCLOPS" },
    { 111, 16, 5,  8,  8, 8, BEH_RANGED,  6, T_FIREBALL, "WARLOCK" },
    {  92, 25, 6, 13, 10, 15, BEH_CHASE, 99, 0,          "MIMIC" },
};

/*  tile hp atk spd cd range proj       name      title */
const AllyDef allydefs[NUM_CLASSES] = {
    {  87, 28, 3, 20, 24, 14,  0,          "BRUNI", "DWARF WARRIOR" },
    {  98, 16, 2, 20, 30, 120, T_ARROW_U,  "LYRA",  "ARCHER" },
    {  84, 12, 4, 18, 42, 110, T_ORB,      "ZOLT",  "WIZARD" },
    { 100, 14, 1, 18, 30, 14,  0,          "MAEVE", "CLERIC" },
};
