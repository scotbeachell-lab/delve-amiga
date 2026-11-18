/* Player, companions, monsters, projectiles and effects. */
#include "game.h"
#include "gfx.h"
#include "sys.h"
#include "sound.h"
#include "assets.h"

Actor actors[MAX_ACTORS];
Proj projs[MAX_PROJ];
Fx fxs[MAX_FX];
Hero hero;

static WORD swing_timer;          /* frames the weapon swing is shown */

/* ------------------------------------------------------------------ utils */
WORD approx_dist(WORD dx, WORD dy)
{
    dx = ABS(dx); dy = ABS(dy);
    return dx > dy ? dx + (dy >> 1) : dy + (dx >> 1);
}
void str_cpy(char *d, const char *s) { while ((*d++ = *s++)) ; }
char *str_cat(char *d, const char *s) { while (*d) d++; while ((*d++ = *s++)) ; return d - 1; }

#define PX(a) ((a)->x >> 4)
#define PY(a) ((a)->y >> 4)
#define CX(a) (PX(a) + 8)
#define CY(a) (PY(a) + 9)

static void fx_spawn_k(WORD x, WORD y, UWORD tile, UBYTE var, WORD life, UBYTE kind)
{
    WORD i;
    for (i = 0; i < MAX_FX; i++)
        if (!fxs[i].alive) {
            fxs[i].alive = 1; fxs[i].tile = fxs[i].alt = tile; fxs[i].var = var; fxs[i].kind = kind;
            fxs[i].x = x; fxs[i].y = y; fxs[i].life = life;
            return;
        }
}
#define fx_spawn(x, y, t, v, l) fx_spawn_k(x, y, t, v, l, FX_FLOAT)

static void fx_number(WORD x, WORD y, WORD n)
{
    if (n > 99) n = 99;
    if (n >= 10) {
        fx_spawn_k(x - 3, y, T_DIGIT0 + n / 10, VAR_NORMAL, 28, FX_FLOAT);
        fx_spawn_k(x + 3, y, T_DIGIT0 + n % 10, VAR_NORMAL, 28, FX_FLOAT);
    } else fx_spawn_k(x, y, T_DIGIT0 + n, VAR_NORMAL, 28, FX_FLOAT);
}

static void proj_spawn(UBYTE team, UBYTE tile, WORD x, WORD y, WORD tx, WORD ty, WORD speed, WORD dmg)
{
    WORD i, dx = tx - x, dy = ty - y, d = approx_dist(dx, dy);
    if (d == 0) d = 1;
    for (i = 0; i < MAX_PROJ; i++)
        if (!projs[i].alive) {
            Proj *p = &projs[i];
            p->alive = 1; p->team = team; p->dmg = dmg; p->life = 90;
            p->x = (x - 8) << 4; p->y = (y - 8) << 4;
            p->vx = (WORD)((LONG)dx * speed / d);
            p->vy = (WORD)((LONG)dy * speed / d);
            if (tile == T_ARROW_U) {          /* pick arrow direction */
                if (ABS(dx) > ABS(dy)) tile = dx > 0 ? T_ARROW_R : T_ARROW_L;
                else tile = dy > 0 ? T_ARROW_D : T_ARROW_U;
            }
            p->tile = tile;
            return;
        }
}

/* hitbox: 10x10 at (x+3, y+5) */
static BOOL can_stand(Actor *a, WORD nx, WORD ny)
{
    WORD px = (nx >> 4) + 3, py = (ny >> 4) + 5;
    BOOL ghost = a->team == TEAM_BAD && mons[a->cls].beh == BEH_GHOST;
    return !solid_px(px, py, ghost) && !solid_px(px + 9, py, ghost) &&
           !solid_px(px, py + 9, ghost) && !solid_px(px + 9, py + 9, ghost);
}

static BOOL move_actor(Actor *a, WORD dx, WORD dy)
{
    BOOL moved = 0;
    if (dx && can_stand(a, a->x + dx, a->y)) { a->x += dx; moved = 1; }
    if (dy && can_stand(a, a->x, a->y + dy)) { a->y += dy; moved = 1; }
    if (moved && !a->kx && !a->ky) a->moved = 1;
    if (dx > 0) a->flip = 0; else if (dx < 0) a->flip = 1;
    return moved;
}

static void move_towards(Actor *a, WORD tx, WORD ty, WORD speed)
{
    WORD dx = tx - CX(a), dy = ty - CY(a), d = approx_dist(dx, dy);
    if (d < 2) return;
    if (!move_actor(a, (WORD)((LONG)dx * speed / d), (WORD)((LONG)dy * speed / d))) {
        /* slide around obstacles */
        if (ABS(dx) > ABS(dy)) move_actor(a, 0, dy >= 0 ? speed : -speed);
        else move_actor(a, dx >= 0 ? speed : -speed, 0);
        a->stuck++;
    } else if (a->stuck) a->stuck--;
}

static WORD scale_depth(WORD base, WORD pct)
{
    return (WORD)((LONG)base * (100 + (hero.depth - 1) * pct) / 100);
}

WORD weapon_power(WORD w, WORD plus)
{
    return (WORD)((weapons[w].dmg + plus) * 100 / weapons[w].cd);
}

WORD party_size(void)
{
    WORD i, n = 0;
    for (i = 0; i < MAX_ALLIES; i++)
        if (actors[ACT_ALLY0 + i].alive || actors[ACT_ALLY0 + i].ko) n++;
    return n;
}

static UWORD xp_for(WORD level) { return (UWORD)(8 + level * level * 6); }

/* ------------------------------------------------------------------ setup */
void party_new_game(void)
{
    WORD i;
    BYTE *p = (BYTE *)actors;
    for (i = 0; i < (WORD)sizeof(actors); i++) p[i] = 0;
    hero.weapon = 0; hero.wplus = 0; hero.armor = 0; hero.atkbonus = 0;
    hero.gold = 0; hero.potions = 1; hero.level = 1; hero.xp = 0;
    hero.xpnext = xp_for(1); hero.depth = 1; hero.kills = 0; hero.speed = 24;
    actors[0].alive = 1;
    actors[0].team = TEAM_GOOD;
    actors[0].maxhp = actors[0].hp = 24;
    actors[0].facing = 2;
}

BOOL ally_recruit(UBYTE cls)
{
    WORD i;
    for (i = 0; i < MAX_ALLIES; i++) {
        Actor *a = &actors[ACT_ALLY0 + i];
        if (!a->alive && !a->ko) {
            const AllyDef *d = &allydefs[cls];
            BYTE *p = (BYTE *)a;
            WORD k;
            for (k = 0; k < (WORD)sizeof(Actor); k++) p[k] = 0;
            a->alive = 1; a->team = TEAM_GOOD; a->cls = cls; a->level = 1;
            a->maxhp = a->hp = d->hp + hero.depth * 2;
            a->atk = d->atk + hero.depth / 3;
            a->x = actors[0].x + (RND(3) - 1) * 16 * FP;
            a->y = actors[0].y + 16 * FP;
            if (!can_stand(a, a->x, a->y)) { a->x = actors[0].x; a->y = actors[0].y; }
            return 1;
        }
    }
    return 0;
}

static void ally_level_up(Actor *a)
{
    a->level++;
    a->maxhp += allydefs[a->cls].hp / 5 + 2;
    a->hp = a->maxhp;
    a->atk += 1;
    a->xp = 0;
}

void hero_gain_xp(WORD xp)
{
    WORD i;
    hero.xp += xp;
    while (hero.xp >= hero.xpnext) {
        hero.xp -= hero.xpnext;
        hero.level++;
        hero.xpnext = xp_for(hero.level);
        actors[0].maxhp += 4;
        actors[0].hp = actors[0].maxhp;
        sound_play(SFX_LEVELUP, 9);
        sound_play(SFX_V_LEVELUP, 9);
        {
            char buf[32];
            str_cpy(buf, "LEVEL UP! NOW LEVEL ");
            fmt_int(buf + str_len(buf), hero.level);
            message(buf, 120);
        }
        fx_spawn(PX(&actors[0]), PY(&actors[0]) - 12, T_HEAL, VAR_WHITE, 30);
    }
    /* companions learn too */
    for (i = 0; i < MAX_ALLIES; i++) {
        Actor *a = &actors[ACT_ALLY0 + i];
        if (!a->alive) continue;
        a->xp += xp;
        if (a->xp >= xp_for(a->level) + 4 && a->level < 20) {
            ally_level_up(a);
            {
                char buf[40];
                str_cpy(buf, allydefs[a->cls].name);
                str_cat(buf, " REACHES LEVEL ");
                fmt_int(buf + str_len(buf), a->level);
                message(buf, 100);
            }
        }
    }
    hud_dirty();
}

void party_heal_all(BOOL revive)
{
    WORD i;
    for (i = 0; i < MAX_ALLIES + 1; i++) {
        Actor *a = &actors[i];
        if (a->ko && revive) { a->ko = 0; a->alive = 1; a->x = actors[0].x; a->y = actors[0].y; }
        if (a->alive) a->hp = a->maxhp;
    }
    hud_dirty();
}

void spawn_monster(WORD kind, WORD px, WORD py, BOOL champion, BOOL boss)
{
    WORD i;
    for (i = ACT_MON0; i < MAX_ACTORS; i++) {
        Actor *a = &actors[i];
        if (!a->alive) {
            const MonDef *m = &mons[kind];
            BYTE *p = (BYTE *)a;
            WORD k;
            for (k = 0; k < (WORD)sizeof(Actor); k++) p[k] = 0;
            a->alive = 1; a->team = TEAM_BAD; a->cls = (UBYTE)kind;
            a->x = px << 4; a->y = py << 4;
            a->maxhp = scale_depth(m->hp, 22);
            a->atk = scale_depth(m->dmg, 14);
            if (champion) { a->maxhp *= 2; a->atk = a->atk * 3 / 2; a->champion = 1; }
            if (boss) { a->maxhp *= 5; a->atk = a->atk * 3 / 2 + 1; a->boss = 1; a->champion = 1; }
            a->hp = a->maxhp;
            a->cd = 40 + RND(40);
            a->aitimer = RND(50);
            a->spawn = 16;                     /* rise out of the ground */
            return;
        }
    }
}

WORD monsters_alive(void)
{
    WORD i, n = 0;
    for (i = ACT_MON0; i < MAX_ACTORS; i++) if (actors[i].alive) n++;
    return n;
}

static WORD pick_monster_kind(void)
{
    WORD a, b, d = hero.depth;
    do a = RND(NUM_MONS - 1); while (mons[a].mindepth > d);
    do b = RND(NUM_MONS - 1); while (mons[b].mindepth > d);
    /* deeper floors favour the newer, nastier monsters */
    if (RND(100) < 40 + d * 3) return mons[a].mindepth > mons[b].mindepth ? a : b;
    return a;
}

/* called after the room tiles are drawn; fromdir = side the player entered from */
void actors_enter_room(WORD fromdir)
{
    Room *r = &rooms[cur_room];
    WORD i;
    static const WORD ex[4] = { 152, 284, 152, 20 }, ey[4] = { 34, 96, 184, 96 };

    for (i = 0; i < MAX_PROJ; i++) projs[i].alive = 0;
    for (i = 0; i < MAX_FX; i++) fxs[i].alive = 0;
    for (i = ACT_MON0; i < MAX_ACTORS; i++) actors[i].alive = 0;
    swing_timer = 0;

    if (fromdir >= 0) {        /* player position next to the entry door */
        WORD px = ex[fromdir], py = ey[fromdir];
        actors[0].x = px << 4;
        actors[0].y = py << 4;
    } else {
        actors[0].x = 152 << 4;
        actors[0].y = 104 << 4;
    }
    for (i = 0; i < MAX_ALLIES; i++) {
        Actor *a = &actors[ACT_ALLY0 + i];
        static const BYTE ox[3] = { -14, 14, 0 }, oy[3] = { 0, 0, 14 };
        if (!a->alive) continue;
        a->x = actors[0].x + ox[i] * FP;
        a->y = actors[0].y + oy[i] * FP;
        if (!can_stand(a, a->x, a->y)) { a->x = actors[0].x; a->y = actors[0].y; }
        a->stuck = 0;
    }

    r->visited = 1;
    if (r->captive) { captive_x = 152; captive_y = 96; }

    if (!r->cleared) {
        WORD n = r->type == RT_TREASURE ? 1 + RND(2) : MIN(MAX_MONS - 1, 2 + hero.depth / 2 + RND(2));
        if (r->type == RT_CAPTIVE) n++;
        for (i = 0; i < n; i++) {
            WORD tries, tx = 0, ty = 0;
            for (tries = 0; tries < 50; tries++) {
                tx = 1 + RND(ROOM_W - 2);
                ty = 2 + RND(ROOM_H - 3);
                if (tile_solid(tile_at(tx, ty)) || tile_at(tx, ty) == TL_STAIRS) continue;
                if (approx_dist(tx * 16 - PX(&actors[0]), ty * 16 - PY(&actors[0])) < 80) continue;
                break;
            }
            spawn_monster(pick_monster_kind(), tx * 16, ty * 16,
                          RND(100) < 4 + hero.depth * 2, 0);
        }
        if (r->boss) {
            static const BYTE bosskind[4] = { M_CYCLOPS, M_WARLOCK, M_DEMON, M_GHOST };
            WORD k = bosskind[(hero.depth / 5 - 1) & 3];
            char buf[40];
            spawn_monster(k, 152, 40, 1, 1);
            music_play(SONG_BOSS, 0);
            sound_play(SFX_V_FIGHT, 10);
            str_cpy(buf, "BOSS: ");
            str_cat(buf, mons[k].name);
            str_cat(buf, " LORD");
            message(buf, 150);
        }
        room_lock(1);
    }
}

/* ------------------------------------------------------------------ combat */
static void actor_damage(Actor *a, WORD dmg, Actor *from);

static void monster_die(Actor *m)
{
    const MonDef *d = &mons[m->cls];
    WORD tx = CX(m) >> 4, ty = CY(m) >> 4, drops, i;
    m->alive = 0;
    sound_play(SFX_DIE, 4);
    {
        UBYTE slot = anim_slot[d->tile];
        if (slot != 255) {
            WORD i2;
            fx_spawn_k(PX(m), PY(m), anim_tab[slot][m->flip][AN_SQUASH1], VAR_WHITE, 12, FX_DEATH);
            for (i2 = 0; i2 < MAX_FX; i2++)
                if (fxs[i2].alive && fxs[i2].kind == FX_DEATH && fxs[i2].life == 12)
                    fxs[i2].alt = anim_tab[slot][m->flip][AN_SQUASH2];
        } else fx_spawn_k(PX(m), PY(m), T_SMOKE0, VAR_NORMAL, 18, FX_SMOKE);
    }
    if (m->boss) gfx_shake(25, 3);
    hero.kills++;
    hero_gain_xp((WORD)(d->xp * (2 + hero.depth / 2) * (m->champion ? 2 : 1) / 2));
    drops = m->boss ? 5 : m->champion ? 2 : 1;
    for (i = 0; i < drops; i++) item_random_drop(tx, ty, m->boss ? 2 : m->champion ? 1 : 0);
    if (d->gold && RND(100) < 30) item_drop(tx, ty, IK_GOLD, (UBYTE)MIN(250, d->gold * hero.depth), 0);
}

static void actor_damage(Actor *a, WORD dmg, Actor *from)
{
    if (!a->alive || a->iframes) return;
    if (a == &actors[0]) {
        dmg = MAX(1, dmg - hero.armor);
        a->iframes = 50;
        sound_play(SFX_HURT, 7);
    } else if (a->team == TEAM_GOOD) {
        dmg = MAX(1, dmg - (a->cls == A_WARRIOR ? a->level / 2 : 0));
        a->iframes = 30;
    } else {
        sound_play(SFX_HIT, 3);
        a->iframes = 6;
    }
    a->hp -= dmg;
    a->flash = 6;
    a->hurt_anim = 5;
    fx_spawn_k(PX(a), PY(a) - 2, T_SPARK0, VAR_NORMAL, 8, FX_SPARK);
    fx_number(PX(a), PY(a) - 10, dmg);
    if (a == &actors[0]) gfx_shake(8, 2);
    else if (a->boss) gfx_shake(4, 1);
    if (from) {     /* knockback away from attacker */
        WORD dx = CX(a) - CX(from), dy = CY(a) - CY(from), d = approx_dist(dx, dy);
        WORD kb = a->boss ? 16 : 48;
        if (d) { a->kx = (WORD)((LONG)dx * kb / d); a->ky = (WORD)((LONG)dy * kb / d); }
    }
    if (a->hp <= 0) {
        a->hp = 0;
        if (a->team == TEAM_BAD) monster_die(a);
        else if (a == &actors[0]) { a->alive = 0; game_event = EV_DEAD; }
        else {
            char buf[40];
            a->alive = 0; a->ko = 1;
            sound_play(SFX_DIE, 6);
            str_cpy(buf, allydefs[a->cls].name);
            str_cat(buf, " HAS FALLEN!");
            message(buf, 100);
        }
    }
}

static WORD nearest_enemy(Actor *a, WORD maxd)
{
    WORD i, best = -1, bd = maxd;
    WORD from = a->team == TEAM_GOOD ? ACT_MON0 : 0;
    WORD to = a->team == TEAM_GOOD ? MAX_ACTORS : ACT_MON0;
    for (i = from; i < to; i++) {
        Actor *o = &actors[i];
        WORD d;
        if (!o->alive) continue;
        d = approx_dist(CX(o) - CX(a), CY(o) - CY(a));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void melee_hit(Actor *a, WORD reach, WORD dmg, WORD facing)
{
    /* box in front of attacker */
    WORD x0 = CX(a) - 8, y0 = CY(a) - 9, x1 = x0 + 16, y1 = y0 + 16, i;
    switch (facing) {
    case 0: y0 -= reach; y1 = CY(a); break;
    case 1: x1 += reach; x0 = CX(a); break;
    case 2: y1 += reach; y0 = CY(a); break;
    case 3: x0 -= reach; x1 = CX(a); break;
    }
    for (i = (a->team == TEAM_GOOD ? ACT_MON0 : 0); i < (a->team == TEAM_GOOD ? MAX_ACTORS : ACT_MON0); i++) {
        Actor *o = &actors[i];
        if (!o->alive) continue;
        if (CX(o) + 6 < x0 || CX(o) - 6 > x1 || CY(o) + 6 < y0 || CY(o) - 6 > y1) continue;
        actor_damage(o, dmg, a);
    }
}

static WORD facing_to(WORD dx, WORD dy)
{
    if (ABS(dx) > ABS(dy)) return dx > 0 ? 1 : 3;
    return dy > 0 ? 2 : 0;
}

/* ------------------------------------------------------------------ player */
static void open_chest(WORD tx, WORD ty)
{
    UBYTE t = tile_at(tx, ty);
    WORD i, n;
    if (t == TL_MIMIC) {
        room_set_obj(tx, ty, 0);
        spawn_monster(M_MIMIC, tx * 16, ty * 16, hero.depth >= 8, 0);
        sound_play(SFX_HURT, 8);
        message("IT'S A MIMIC!", 90);
        return;
    }
    room_set_obj(tx, ty, 0);
    sound_play(SFX_CHEST, 6);
    fx_spawn_k(tx * 16, ty * 16 - 4, T_TWINKLE, VAR_NORMAL, 14, FX_FLOAT);
    n = t == TL_BIGCHEST ? 3 + RND(2) : 1 + RND(2);
    for (i = 0; i < n; i++) item_random_drop(tx, ty, t == TL_BIGCHEST ? 2 : 1);
    message(t == TL_BIGCHEST ? "A GREAT TREASURE CHEST!" : "CHEST OPENED", 80);
}

static void pickup(void)
{
    Actor *p = &actors[0];
    WORD idx = item_at(CX(p) >> 4, CY(p) >> 4);
    Item it;
    char buf[44];
    if (idx < 0) return;
    it = rooms[cur_room].items[idx];
    switch (it.kind) {
    case IK_GOLD:
        hero.gold += it.val;
        str_cpy(buf, "+"); fmt_int(buf + 1, it.val); str_cat(buf, " GOLD");
        message(buf, 50);
        sound_play(SFX_COIN, 5);
        break;
    case IK_POTION:
        if (hero.potions >= 9) { message("POTION BELT FULL", 50); return; }
        hero.potions++;
        message("HEALTH POTION  (Q/FIRE 2 TO DRINK)", 90);
        sound_play(SFX_POTION, 5);
        break;
    case IK_VITALITY:
        p->maxhp += 5; p->hp += 5;
        message("VITALITY POTION: MAX HP +5", 90);
        sound_play(SFX_POTION, 5);
        sound_play(SFX_V_POWERUP, 6);
        break;
    case IK_STRENGTH:
        hero.atkbonus++;
        message("STRENGTH POTION: ATTACK +1", 90);
        sound_play(SFX_POTION, 5);
        sound_play(SFX_V_POWERUP, 6);
        break;
    case IK_ELIXIR:
        party_heal_all(1);
        message("ELIXIR: PARTY HEALED AND REVIVED", 90);
        sound_play(SFX_POTION, 5);
        break;
    case IK_SHIELD:
        hero.armor++;
        str_cpy(buf, "SHIELD: ARMOR NOW ");
        fmt_int(buf + str_len(buf), hero.armor);
        message(buf, 90);
        sound_play(SFX_CHEST, 5);
        break;
    case IK_WEAPON:
        if (weapon_power(it.val, it.plus) > weapon_power(hero.weapon, hero.wplus)) {
            hero.weapon = it.val; hero.wplus = it.plus;
            str_cpy(buf, "EQUIPPED ");
            str_cat(buf, weapons[it.val].name);
            if (it.plus) { str_cat(buf, " +"); fmt_int(buf + str_len(buf), it.plus); }
            message(buf, 100);
            sound_play(SFX_SWING, 5);
        } else {
            WORD v = 3 + it.val * 3 + it.plus * 5;
            hero.gold += v;
            str_cpy(buf, "SOLD ");
            str_cat(buf, weapons[it.val].name);
            str_cat(buf, " FOR ");
            fmt_int(buf + str_len(buf), v);
            str_cat(buf, "G");
            message(buf, 80);
            sound_play(SFX_COIN, 5);
        }
        break;
    }
    item_remove(idx);
    hud_dirty();
}

static void player_update(UWORD in, UWORD pressed)
{
    Actor *p = &actors[0];
    WORD dx = 0, dy = 0, sp = hero.speed;
    if (in & IN_LEFT) dx = -sp;
    if (in & IN_RIGHT) dx = sp;
    if (in & IN_UP) dy = -sp;
    if (in & IN_DOWN) dy = sp;
    if (dx && dy) { dx = dx * 3 / 4; dy = dy * 3 / 4; }
    if (dx || dy) {
        if (!(in & IN_FIRE) || p->cd == 0)
            p->facing = (UBYTE)(dy && !dx ? (dy < 0 ? 0 : 2) : (dx > 0 ? 1 : 3));
        move_actor(p, dx, dy);
        /* bump into chests */
        {
            static const BYTE fx[4] = { 0, 12, 0, -12 }, fy[4] = { -12, 0, 12, 0 };
            WORD tx = (CX(p) + fx[p->facing]) >> 4, ty = (CY(p) + fy[p->facing]) >> 4;
            UBYTE t = tile_at(tx, ty);
            if (t == TL_CHEST || t == TL_BIGCHEST || t == TL_MIMIC) open_chest(tx, ty);
        }
    }

    if ((in & IN_FIRE) && p->cd == 0) {
        const WeaponDef *w = &weapons[hero.weapon];
        WORD dmg = w->dmg + hero.wplus + hero.atkbonus + hero.level / 2;
        if (RND(100) < 8) dmg *= 2;                      /* critical */
        p->cd = w->cd;
        if (w->proj) {
            static const BYTE vx[4] = { 0, 1, 0, -1 }, vy[4] = { -1, 0, 1, 0 };
            proj_spawn(TEAM_GOOD, w->proj, CX(p), CY(p) - 2,
                       CX(p) + vx[p->facing] * 100, CY(p) - 2 + vy[p->facing] * 100, 48, dmg);
            sound_play(SFX_MAGIC, 4);
        } else {
            melee_hit(p, w->reach, dmg, p->facing);
            sound_play(SFX_SWING, 2);
        }
        swing_timer = 9;
    }
    if ((pressed & IN_FIRE2) && hero.potions > 0 && p->hp < p->maxhp) {
        hero.potions--;
        p->hp = MIN(p->maxhp, p->hp + MAX(8, p->maxhp / 2));
        sound_play(SFX_POTION, 6);
        fx_spawn(PX(p), PY(p) - 10, T_HEAL, VAR_NORMAL, 25);
        hud_dirty();
    }
    pickup();

    /* stairs */
    if (tile_at(CX(p) >> 4, CY(p) >> 4) == TL_STAIRS && !room_locked) game_event = EV_NEXT_FLOOR;

    /* captive companion */
    if (rooms[cur_room].captive &&
        approx_dist(CX(p) - (captive_x + 8), CY(p) - (captive_y + 9)) < 16) {
        UBYTE cls = rooms[cur_room].captive - 1;
        if (party_size() >= MAX_ALLIES) {
            if (!(frame_count & 63)) message("YOUR PARTY IS FULL (MAX 3)", 60);
        } else {
            char buf[44];
            ally_recruit(cls);
            rooms[cur_room].captive = 0;
            str_cpy(buf, allydefs[cls].name);
            str_cat(buf, " THE ");
            str_cat(buf, allydefs[cls].title);
            str_cat(buf, " JOINS YOU!");
            message(buf, 150);
            sound_play(SFX_LEVELUP, 8);
            hud_dirty();
        }
    }

    /* room exits (doorway gaps in the outer wall) */
    if (!room_locked) {
        /* hitbox limits: CX 5..315, CY 4..218 */
        if (CY(p) < 26) room_change_dir = 0;
        else if (CX(p) > ROOM_W * 16 - 8) room_change_dir = 1;
        else if (CY(p) > ROOM_H * 16 - 10) room_change_dir = 2;
        else if (CX(p) < 8) room_change_dir = 3;
    }
}

/* ------------------------------------------------------------------ allies */
static void ally_update(Actor *a, WORD slot)
{
    const AllyDef *d = &allydefs[a->cls];
    Actor *p = &actors[0];
    WORD speed = d->speed;
    WORD t;

    /* cleric: heal the most wounded party member */
    if (a->cls == A_CLERIC) {
        if (a->heal_cd) a->heal_cd--;
        else {
            WORD i, best = -1, bestpct = 70;
            for (i = 0; i <= MAX_ALLIES; i++) {
                Actor *o = &actors[i];
                WORD pct;
                if (!o->alive) continue;
                pct = (WORD)((LONG)o->hp * 100 / o->maxhp);
                if (pct < bestpct) { bestpct = pct; best = i; }
            }
            if (best >= 0) {
                Actor *o = &actors[best];
                o->hp = MIN(o->maxhp, o->hp + 3 + a->level * 2);
                fx_spawn(PX(o), PY(o) - 10, T_HEAL, VAR_NORMAL, 25);
                sound_play(SFX_POTION, 2);
                a->heal_cd = MAX(60, 170 - a->level * 8);
                hud_dirty();
            } else a->heal_cd = 20;
        }
    }

    t = nearest_enemy(a, d->proj ? 150 : 110);
    if (t >= 0 && approx_dist(CX(p) - CX(a), CY(p) - CY(a)) < 150) {
        Actor *e = &actors[t];
        WORD dx = CX(e) - CX(a), dy = CY(e) - CY(a), dist = approx_dist(dx, dy);
        if (d->proj) {
            if (dist > d->range) move_towards(a, CX(e), CY(e), speed);
            else if (dist < 36) move_towards(a, CX(a) - dx, CY(a) - dy, speed);
            if (dist <= d->range && a->cd == 0) {
                proj_spawn(TEAM_GOOD, d->proj, CX(a), CY(a) - 2, CX(e), CY(e) - 2, 40,
                           a->atk + a->level / 2);
                if (a->cls == A_WIZARD) sound_play(SFX_MAGIC, 1);
                a->cd = d->cd;
                a->atk_anim = 8;
            }
        } else {
            if (dist > 14) move_towards(a, CX(e), CY(e), speed);
            if (dist < 20 && a->cd == 0) {
                a->facing = (UBYTE)facing_to(dx, dy);
                melee_hit(a, d->range, a->atk + a->level / 2, a->facing);
                a->cd = d->cd;
                a->atk_anim = 8;
            }
        }
        if (dx > 0) a->flip = 0; else if (dx < 0) a->flip = 1;
    } else {
        /* follow the hero in a loose formation */
        static const BYTE fx[3] = { -18, 18, 0 }, fy[3] = { 10, 10, 22 };
        WORD tx = CX(p) + fx[slot], ty = CY(p) + fy[slot];
        WORD dist = approx_dist(tx - CX(a), ty - CY(a));
        if (dist > 10) move_towards(a, tx, ty, dist > 60 ? speed + 8 : speed);
    }
    if (a->stuck > 80 || approx_dist(CX(p) - CX(a), CY(p) - CY(a)) > 220) {
        a->x = p->x; a->y = p->y; a->stuck = 0;
    }
}

/* ------------------------------------------------------------------ monsters */
static void monster_update(Actor *m)
{
    const MonDef *d = &mons[m->cls];
    WORD t = nearest_enemy(m, 400);
    WORD speed = d->speed + (m->boss ? 2 : 0);
    Actor *e;
    WORD dx, dy, dist;

    if (t < 0 || m->spawn) return;
    e = &actors[t];
    dx = CX(e) - CX(m); dy = CY(e) - CY(m);
    dist = approx_dist(dx, dy);

    switch (d->beh) {
    case BEH_ERRATIC:
        if (--m->aitimer <= 0) {
            m->aitimer = 10 + RND(20);
            m->wx = RND(3) - 1; m->wy = RND(3) - 1;
        }
        if (RND(3)) move_towards(m, CX(e), CY(e), speed);
        else move_actor(m, m->wx * speed, m->wy * speed);
        break;
    case BEH_DASH:
        if (m->aitimer > 0) {                /* dashing */
            m->aitimer--;
            move_actor(m, m->wx, m->wy);
        } else if (--m->stuck < -60 + RND(30)) {
            m->stuck = 0;
            m->aitimer = 22;
            if (dist) { m->wx = (WORD)((LONG)dx * 40 / dist); m->wy = (WORD)((LONG)dy * 40 / dist); }
        } else move_towards(m, CX(e), CY(e), speed / 3);
        break;
    case BEH_RANGED:
        if (dist > 110) move_towards(m, CX(e), CY(e), speed);
        else if (dist < 50) move_towards(m, CX(m) - dx, CY(m) - dy, speed);
        if (dist < 150 && m->cd == 0) {
            proj_spawn(TEAM_BAD, d->proj, CX(m), CY(m) - 2, CX(e), CY(e) - 2,
                       d->proj == T_FIREBALL ? 30 : 36, m->atk);
            m->cd = (m->boss ? 40 : 80) + RND(40);
            m->atk_anim = 8;
            if (m->boss) m->cd = 35;
        }
        break;
    default:
        move_towards(m, CX(e), CY(e), speed);
        break;
    }
    if (dx > 0) m->flip = 0; else if (dx < 0) m->flip = 1;

    /* contact damage */
    if (m->cd == 0 && d->beh != BEH_RANGED && ABS(dx) < 12 && ABS(dy) < 12) {
        actor_damage(e, m->atk, m);
        m->cd = 45;
        m->atk_anim = 8;
    } else if (m->cd == 0 && ABS(dx) < 10 && ABS(dy) < 10) {
        actor_damage(e, m->atk, m);
        m->cd = 50;
    }
}

/* push overlapping actors apart a little */
static void separate(void)
{
    WORD i, j;
    for (i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &actors[i];
        if (!a->alive) continue;
        for (j = i + 1; j < MAX_ACTORS; j++) {
            Actor *b = &actors[j];
            WORD dx, dy;
            if (!b->alive) continue;
            dx = b->x - a->x; dy = b->y - a->y;
            if (ABS(dx) < 10 * FP && ABS(dy) < 10 * FP) {
                WORD px = dx >= 0 ? 8 : -8, py = dy >= 0 ? 8 : -8;
                if (ABS(dx) > ABS(dy)) py = 0; else px = 0;
                if (i != ACT_PLAYER) move_actor(a, -px, -py);
                move_actor(b, px, py);
            }
        }
    }
}

static void projs_update(void)
{
    WORD i, j;
    for (i = 0; i < MAX_PROJ; i++) {
        Proj *p = &projs[i];
        WORD cx, cy;
        if (!p->alive) continue;
        p->x += p->vx; p->y += p->vy;
        cx = (p->x >> 4) + 8; cy = (p->y >> 4) + 8;
        if (--p->life <= 0 || solid_px(cx, cy, 0)) { p->alive = 0; continue; }
        for (j = (p->team == TEAM_GOOD ? ACT_MON0 : 0); j < (p->team == TEAM_GOOD ? MAX_ACTORS : ACT_MON0); j++) {
            Actor *a = &actors[j];
            if (!a->alive) continue;
            if (ABS(CX(a) - cx) < 8 && ABS(CY(a) - cy) < 9) {
                Actor src;
                src.x = p->x - p->vx * 4; src.y = p->y - p->vy * 4;
                actor_damage(a, p->dmg, &src);
                p->alive = 0;
                break;
            }
        }
    }
}

void actors_update(UWORD in, UWORD pressed)
{
    WORD i;
    for (i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &actors[i];
        a->moved = 0;
        if (!a->alive) continue;
        if (a->cd) a->cd--;
        if (a->spawn) a->spawn--;
        if (a->atk_anim) a->atk_anim--;
        if (a->hurt_anim) a->hurt_anim--;
        if (a->iframes) a->iframes--;
        if (a->flash) a->flash--;
        if (a->kx || a->ky) {
            move_actor(a, a->kx, a->ky);
            a->kx = a->kx * 3 / 4; a->ky = a->ky * 3 / 4;
        }
    }
    player_update(in, pressed);
    for (i = 0; i < MAX_ALLIES; i++)
        if (actors[ACT_ALLY0 + i].alive) ally_update(&actors[ACT_ALLY0 + i], i);
    for (i = ACT_MON0; i < MAX_ACTORS; i++)
        if (actors[i].alive) monster_update(&actors[i]);
    separate();
    projs_update();
    for (i = 0; i < MAX_FX; i++)
        if (fxs[i].alive) {
            Fx *f = &fxs[i];
            if (--f->life <= 0) { f->alive = 0; continue; }
            if (f->kind == FX_FLOAT && (f->life & 1)) f->y--;
            else if (f->kind == FX_SPARK) f->tile = f->life > 4 ? T_SPARK0 : T_SPARK1;
            else if (f->kind == FX_DEATH) {
                if (f->life == 8) { f->tile = f->alt; f->var = VAR_NORMAL; }
                if (f->life == 1) { f->kind = FX_SMOKE; f->tile = T_SMOKE0; f->life = 18; f->var = VAR_NORMAL; }
            }
            else if (f->kind == FX_SMOKE) { f->tile = T_SMOKE0 + (f->life > 12 ? 0 : f->life > 6 ? 1 : 2); if (f->life & 2) f->y--; }
        }
    /* treasure twinkles */
    if ((frame_count & 31) == 0 && rooms[cur_room].nitems) {
        Item *it = &rooms[cur_room].items[RND(rooms[cur_room].nitems)];
        fx_spawn_k(it->tx * 16 + RND(8) - 4, it->ty * 16 + RND(6) - 6, T_TWINKLE, VAR_NORMAL, 8, FX_STATIC);
    }
    if (swing_timer) swing_timer--;

    if (room_locked && monsters_alive() == 0) {
        rooms[cur_room].cleared = 1;
        room_lock(0);
        sound_play(SFX_DOOR, 6);
        if (rooms[cur_room].type == RT_STAIRS) message("ROOM CLEARED - THE TRAPDOOR LEADS DOWN", 120);
        if (rooms[cur_room].boss) music_for_floor();
    }
}

/* ------------------------------------------------------------------ drawing */
typedef struct { WORD key, y, x; UWORD tile; UBYTE var, shadow; } Draw;
static Draw list[64];
static WORD nlist;

static void add_draw(WORD x, WORD y, WORD key, UWORD tile, UBYTE var, UBYTE shadow)
{
    if (nlist >= 64) return;
    list[nlist].x = x; list[nlist].y = y; list[nlist].key = key;
    list[nlist].tile = tile; list[nlist].var = var; list[nlist++].shadow = shadow;
}

/* weapon held at the side (upright), mirrored when facing left */
static void add_held(WORD x, WORD y, WORD key, UBYTE wtile, BOOL left)
{
    if (left) add_draw(x - 7, y + 2, key + 1, tile_flip[wtile], VAR_NORMAL, 0);
    else add_draw(x + 7, y + 2, key + 1, wtile, VAR_NORMAL, 0);
}

/* pick the animation frame for an actor; *bob gets a vertical offset */
static UWORD actor_frame(Actor *a, WORD i, UBYTE base, WORD *bob)
{
    UBYTE slot = anim_slot[base];
    BOOL left = a->flip;
    BOOL always = a->team == TEAM_BAD && (base == 120 || base == 121 || base == 108);  /* flap/waver/squish */
    WORD f = -1;
    *bob = 0;
    if (slot == 255) return left && tile_flip[base] ? tile_flip[base] : base;
    if (a->spawn) f = a->spawn > 8 ? AN_SQUASH2 : AN_SQUASH1;
    else if (a->hurt_anim) f = AN_SQUASH1;
    else if (a->atk_anim > 3) f = AN_ATTACK;
    else if (a->moved || always) {
        WORD speed = base == 120 ? 3 : 5;
        WORD seq = (WORD)(((frame_count + i * 5) / speed) & 3);
        if (seq == 0) f = AN_WALK_A;
        else if (seq == 2) f = AN_WALK_B;
        else *bob = (base == 120) ? -2 : -1;
    } else if (((frame_count + i * 13) & 63) >= 44) f = AN_BREATHE;
    if (f < 0) return left && tile_flip[base] ? tile_flip[base] : base;
    return anim_tab[slot][left][f];
}

void actors_draw(void)
{
    WORD i, j;
    Actor *p = &actors[0];
    nlist = 0;

    if (rooms[cur_room].captive) {
        UBYTE t = allydefs[rooms[cur_room].captive - 1].tile;
        UBYTE slot = anim_slot[t];
        WORD hop = (frame_count & 16) ? -1 : 0;
        UWORD ft = (slot != 255 && (frame_count & 32)) ? anim_tab[slot][0][AN_BREATHE] : t;
        add_draw(captive_x, captive_y + hop, captive_y, ft, VAR_NORMAL, 1);
        if (frame_count & 32) add_draw(captive_x, captive_y - 13, captive_y + 1, T_TWINKLE, VAR_NORMAL, 0);
    }
    for (i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &actors[i];
        UBYTE base, var = VAR_NORMAL;
        UWORD tile;
        WORD x, y, bob;
        if (!a->alive) continue;
        if (i == 0) {
            base = 96;
            if (a->iframes && (frame_count & 4)) continue;
            if (swing_timer) a->atk_anim = swing_timer;
        } else if (a->team == TEAM_GOOD) base = allydefs[a->cls].tile;
        else base = mons[a->cls].tile;
        tile = actor_frame(a, i, base, &bob);
        if (a->flash) var = a->team == TEAM_GOOD ? VAR_RED : VAR_WHITE;
        else if (a->champion) var = VAR_RED;
        x = PX(a); y = PY(a);
        if (i == 0 && swing_timer > 4) {           /* lunge into the attack */
            static const BYTE lx[4] = { 0, 2, 0, -2 }, ly[4] = { -2, 0, 2, 0 };
            x += lx[p->facing]; y += ly[p->facing];
        }
        add_draw(x, y + bob, PY(a), tile, var, a->spawn > 8 ? 0 : 1);
        if (i == 0) {
            if (!swing_timer || weapons[hero.weapon].proj)
                add_held(x, y + bob, PY(a), weapons[hero.weapon].tile, p->facing == 3 || (p->facing == 0 && p->flip));
        } else if (a->team == TEAM_GOOD) {
            static const UBYTE held[NUM_CLASSES] = { 119, 0, 129, 130 };
            if (held[a->cls] && a->atk_anim <= 3) add_held(x, y + bob, PY(a), held[a->cls], a->flip);
        }
    }
    if (swing_timer && !weapons[hero.weapon].proj) {
        /* three-step slashing arc through the facing direction */
        static const BYTE ox8[8] = { 0, 9, 12, 9, 0, -9, -12, -9 }, oy8[8] = { -12, -9, 0, 9, 11, 9, 0, -9 };
        WORD phase = (9 - swing_timer) / 3;
        WORD d8 = (p->facing * 2 - 1 + (phase > 2 ? 2 : phase) + 8) & 7;
        add_draw(PX(p) + ox8[d8], PY(p) + oy8[d8], PY(p) + (oy8[d8] < 0 ? -1 : 1),
                 weapon8[weapons[hero.weapon].tile][d8], VAR_NORMAL, 0);
    }
    for (i = 0; i < MAX_PROJ; i++)
        if (projs[i].alive) {
            UWORD t = projs[i].tile;
            if (frame_count & 4) {                   /* pulse */
                if (t == T_ORB) t = T_ORB2;
                else if (t == T_FIREBALL) t = T_FIREBALL2;
                else if (t == T_BOLT) t = T_BOLT2;
            }
            add_draw(projs[i].x >> 4, projs[i].y >> 4, (projs[i].y >> 4) + 1000, t, VAR_NORMAL, 0);
        }
    for (i = 0; i < MAX_FX; i++)
        if (fxs[i].alive)
            add_draw(fxs[i].x, fxs[i].y, fxs[i].kind == FX_DEATH ? fxs[i].y : fxs[i].y + 2000,
                     fxs[i].tile, fxs[i].var, fxs[i].kind == FX_DEATH);

    for (i = 1; i < nlist; i++) {               /* depth sort */
        Draw t = list[i];
        j = i - 1;
        while (j >= 0 && list[j].key > t.key) { list[j + 1] = list[j]; j--; }
        list[j + 1] = t;
    }
    bobs_begin();
    for (i = 0; i < nlist; i++) {
        if (list[i].shadow) bob_draw_shadowed(list[i].x, list[i].y, list[i].tile, list[i].var);
        else bob_draw(list[i].x, list[i].y, list[i].tile, list[i].var);
    }
}
