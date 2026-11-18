#ifndef GAME_H
#define GAME_H
#include <exec/types.h>

/* ---------------------------------------------------------------- layout */
#define ROOM_W 20
#define ROOM_H 14
#define GRID_MAX 4
#define MAX_ROOMS (GRID_MAX * GRID_MAX)
#define MAX_ITEMS 24
#define FP 16                      /* fixed point: 1/16 pixel */

/* ---------------------------------------------------------------- tiles */
#define TL_FLOOR   48
#define TL_FLOOR2  49
#define TL_FLOOR3  42
#define TL_WALL    40
#define TL_WALL2   57
#define TL_WALL3   59
#define TL_BANNER  29
#define TL_GRATE   28
#define TL_DOOR    45
#define TL_STAIRS  66
#define TL_BARREL  82
#define TL_TABLE   72
#define TL_STOOL   73
#define TL_SHELF   63
#define TL_TOMB    65
#define TL_CROSS   64
#define TL_ANVIL   74
#define TL_CHEST   89
#define TL_MIMIC   90              /* looks like a chest... */
#define TL_BIGCHEST 91
#define TL_SKULLS  41
#define TL_ROCK    0
#define TL_FACE    40
#define TL_ARCH_L  10
#define TL_ARCH_R  11
#define TL_DDOOR_L 46
#define TL_DDOOR_R 47
#define TL_STATUE  7
#define TL_FOUNTAIN 8
#define TL_CRATE   82

/* ---------------------------------------------------------------- items */
enum { IK_NONE, IK_GOLD, IK_POTION, IK_VITALITY, IK_STRENGTH, IK_ELIXIR,
       IK_WEAPON, IK_SHIELD };
typedef struct { UBYTE tx, ty, kind, val, plus; } Item;

/* ---------------------------------------------------------------- weapons */
typedef struct {
    UBYTE tile, dmg, cd, reach, proj;   /* proj = projectile tile, 0 = melee */
    const char *name;
} WeaponDef;
#define NUM_WEAPONS 11
extern const WeaponDef weapons[NUM_WEAPONS];

/* ---------------------------------------------------------------- monsters */
enum { BEH_CHASE, BEH_ERRATIC, BEH_DASH, BEH_GHOST, BEH_RANGED };
typedef struct {
    UBYTE tile, hp, dmg, speed, xp, gold, beh, mindepth, proj;
    const char *name;
} MonDef;
enum { M_RAT, M_BAT, M_SLIME, M_SPIDER, M_GHOST, M_BANDIT, M_DEMON,
       M_CYCLOPS, M_WARLOCK, M_MIMIC, NUM_MONS };
extern const MonDef mons[NUM_MONS];

/* ---------------------------------------------------------------- allies */
enum { A_WARRIOR, A_ARCHER, A_WIZARD, A_CLERIC, NUM_CLASSES };
typedef struct {
    UBYTE tile, hp, atk, speed, cd, range, proj;
    const char *name, *title;
} AllyDef;
extern const AllyDef allydefs[NUM_CLASSES];

/* ---------------------------------------------------------------- actors */
enum { TEAM_GOOD, TEAM_BAD };
typedef struct {
    UBYTE alive, team, cls, facing;   /* facing 0 up 1 right 2 down 3 left */
    UBYTE flip, flash, iframes, champion;
    UBYTE boss, ko, level, moved;
    UBYTE spawn, atk_anim, hurt_anim, pad2;
    WORD x, y;                        /* fixed point top-left of 16x16 */
    WORD kx, ky;                      /* knockback velocity */
    WORD hp, maxhp, atk;
    WORD cd, aitimer, heal_cd;
    WORD wx, wy;                      /* wander/dash direction */
    WORD stuck, target;
    UWORD xp;
} Actor;

#define MAX_ALLIES 3
#define MAX_MONS   10
#define ACT_PLAYER 0
#define ACT_ALLY0  1
#define ACT_MON0   (1 + MAX_ALLIES)
#define MAX_ACTORS (ACT_MON0 + MAX_MONS)
extern Actor actors[MAX_ACTORS];

typedef struct {
    UBYTE alive, team, tile, pad;
    WORD x, y, vx, vy, dmg, life;
} Proj;
#define MAX_PROJ 14
extern Proj projs[MAX_PROJ];

enum { FX_STATIC, FX_FLOAT, FX_SPARK, FX_SMOKE, FX_DEATH };
typedef struct { UBYTE alive, var, kind, pad; UWORD tile, alt; WORD x, y, life; } Fx;
#define MAX_FX 16
extern Fx fxs[MAX_FX];

/* ---------------------------------------------------------------- hero */
typedef struct {
    WORD weapon, wplus, armor, atkbonus;
    LONG gold;
    WORD potions;
    WORD level;
    UWORD xp, xpnext;
    WORD depth, kills;
    WORD speed;
} Hero;
extern Hero hero;

/* ---------------------------------------------------------------- rooms */
enum { RT_NORMAL, RT_START, RT_STAIRS, RT_TREASURE, RT_CAPTIVE };
#define DOOR_N 1
#define DOOR_E 2
#define DOOR_S 4
#define DOOR_W 8
typedef struct {
    UBYTE exists, doors, type, cleared;
    UBYTE visited, captive, gx, gy;   /* captive = ally class + 1, 0 none */
    UBYTE boss, nitems, dist, pad;
    UBYTE floor[ROOM_H][ROOM_W];      /* opaque layer: floor, rock, wall faces, doors */
    UBYTE obj[ROOM_H][ROOM_W];        /* transparent layer: furniture, chests, stairs */
    UBYTE shade[ROOM_H][ROOM_W];      /* baked shadow: 1 top, 2 left, 3 both, +4 drop */
    Item items[MAX_ITEMS];
} Room;
extern Room rooms[MAX_ROOMS];
extern WORD grid[GRID_MAX][GRID_MAX];
extern WORD grid_w, grid_h, nrooms, cur_room;
extern UBYTE room_locked;
extern WORD captive_x, captive_y;     /* pixel pos of captive in current room */

/* ---------------------------------------------------------------- level.c */
void floor_generate(WORD depth);
void room_draw(void);
void room_set_tile(WORD tx, WORD ty, UBYTE t);
UBYTE tile_at(WORD tx, WORD ty);
BOOL tile_solid(UBYTE t);
BOOL solid_px(WORD px, WORD py, BOOL ghost);
void room_lock(BOOL lock);
BOOL item_drop(WORD tx, WORD ty, UBYTE kind, UBYTE val, UBYTE plus);
void item_remove(WORD idx);
void item_random_drop(WORD tx, WORD ty, WORD quality);
WORD item_at(WORD tx, WORD ty);
UBYTE item_tile(const Item *it);
void minimap_draw(void);
void music_for_floor(void);
void room_set_obj(WORD tx, WORD ty, UBYTE t);

/* ---------------------------------------------------------------- actors.c */
void party_new_game(void);
void actors_enter_room(WORD fromdir);
void actors_update(UWORD in, UWORD pressed);
void actors_draw(void);
WORD monsters_alive(void);
BOOL ally_recruit(UBYTE cls);
WORD party_size(void);
void spawn_monster(WORD kind, WORD px, WORD py, BOOL champion, BOOL boss);
void hero_gain_xp(WORD xp);
void party_heal_all(BOOL revive);
WORD weapon_power(WORD w, WORD plus);

/* ---------------------------------------------------------------- hud.c */
void hud_full(void);
void hud_update(void);
void hud_dirty(void);
void message(const char *s, WORD frames);
void message2(const char *a, const char *b, WORD frames);
void message_tick(void);

/* ---------------------------------------------------------------- state */
enum { EV_NONE, EV_NEXT_FLOOR, EV_DEAD, EV_QUIT };
extern WORD game_event;
extern WORD room_change_dir;    /* -1 none, else 0..3 */

#define ABS(a) ((a) < 0 ? -(a) : (a))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
WORD approx_dist(WORD dx, WORD dy);
void str_cpy(char *d, const char *s);
char *str_cat(char *d, const char *s);
#endif
