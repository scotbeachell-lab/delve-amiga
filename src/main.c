/*
 * DELVE - a real-time rogue-lite dungeon crawler for the Amiga 1200 (AGA).
 * Graphics, sounds and font: Kenney (www.kenney.nl), CC0.
 */
#include <proto/exec.h>
#include "hw.h"
#include "sys.h"
#include "gfx.h"
#include "sound.h"
#include "assets.h"
#include "game.h"

WORD game_event;
WORD room_change_dir = -1;
static UWORD prev_in;

#ifdef AUTOPLAY
/* test-only bot: drives the hero so the game can be checked without a human */
static WORD in_play, bot_menu_reset;
UWORD prof_t0, prof_upd, prof_drw;

/* BFS from (tx,ty) over walkable cells; returns next cell toward it from (sx,sy) */
static BOOL bot_path(WORD sx, WORD sy, WORD tx, WORD ty, WORD *nx, WORD *ny)
{
    static BYTE from[ROOM_H][ROOM_W];
    static UBYTE qx[ROOM_W * ROOM_H], qy[ROOM_W * ROOM_H];
    WORD h = 0, t = 0, x, y, i;
    static const BYTE dx[4] = { 0, 1, 0, -1 }, dy[4] = { -1, 0, 1, 0 };
    for (y = 0; y < ROOM_H; y++) for (x = 0; x < ROOM_W; x++) from[y][x] = -1;
    from[ty][tx] = 4; qx[t] = (UBYTE)tx; qy[t++] = (UBYTE)ty;
    while (h < t) {
        x = qx[h]; y = qy[h++];
        if (x == sx && y == sy) { *nx = x + dx[from[y][x]]; *ny = y + dy[from[y][x]]; return 1; }
        for (i = 0; i < 4; i++) {
            WORD ax = x + dx[i], ay = y + dy[i];
            if (ax < 0 || ay < 0 || ax >= ROOM_W || ay >= ROOM_H || from[ay][ax] >= 0) continue;
            if (tile_solid(tile_at(ax, ay)) && !(ax == sx && ay == sy)) continue;
            from[ay][ax] = (BYTE)((i + 2) & 3);    /* direction back toward target */
            qx[t] = (UBYTE)ax; qy[t++] = (UBYTE)ay;
        }
    }
    return 0;
}

static UWORD bot_input(void)
{
    static const BYTE dcx[4] = { 9, 19, 9, 0 }, dcy[4] = { 1, 6, 13, 6 };
    static const BYTE pushx[4] = { 0, 1, 0, -1 }, pushy[4] = { -1, 0, 1, 0 };
    Actor *p = &actors[0];
    WORD px = (p->x >> 4) + 8, py = (p->y >> 4) + 9, tx = -1, ty = -1, i, best = 9999;
    UWORD in = 0;
    BOOL push = 0;
    WORD pushdir = 0;
    if (!in_play) {     /* menus: wait ~6 s, then press fire */
        static ULONG since; static WORD was_play = 1;
        if (bot_menu_reset) { was_play = 1; bot_menu_reset = 0; }
        if (was_play) { since = frame_count; was_play = 0; }
        if (frame_count - since < 300) return 0;
        return (frame_count & 32) ? IN_FIRE : 0;
    }
    for (i = ACT_MON0; i < MAX_ACTORS; i++)
        if (actors[i].alive) {
            WORD d = approx_dist((actors[i].x >> 4) + 8 - px, (actors[i].y >> 4) + 9 - py);
            if (d < best) { best = d; tx = (actors[i].x >> 4) + 8; ty = (actors[i].y >> 4) + 9; }
        }
    if (tx >= 0) {
        if (best < 22 || weapons[hero.weapon].proj) in |= IN_FIRE;
    } else if (rooms[cur_room].nitems) {
        tx = rooms[cur_room].items[0].tx * 16 + 8; ty = rooms[cur_room].items[0].ty * 16 + 8;
    } else if (rooms[cur_room].captive && party_size() < MAX_ALLIES) {
        tx = captive_x + 8; ty = captive_y + 9;
    } else if (rooms[cur_room].type == RT_STAIRS) {
        tx = 9 * 16 + 8; ty = 6 * 16 + 8;
    } else {
        static WORD goal = -1, goalroom = -1;
        if (goalroom != cur_room) {
            WORD d, pick = -1;
            for (d = 0; d < 4; d++)
                if (rooms[cur_room].doors & (1 << d)) {
                    static const BYTE gdx[4] = { 0, 1, 0, -1 }, gdy[4] = { -1, 0, 1, 0 };
                    WORD o = grid[rooms[cur_room].gy + gdy[d]][rooms[cur_room].gx + gdx[d]];
                    if (pick < 0 || (!rooms[o].visited && RND(2)) || RND(4) == 0) pick = d;
                }
            goal = pick; goalroom = cur_room;
        }
        tx = dcx[goal] * 16 + 16; ty = dcy[goal] * 16 + 8;
        if (goal == 3) tx = 8;
        push = 1; pushdir = goal;
    }
    if (tx >= 0) {
        WORD cx = px >> 4, cy = py >> 4, gx = tx >> 4, gy = ty >> 4, nx, ny, dx, dy;
        if (gx > 19) gx = 19;
        if (push && cx == gx && cy == gy) { dx = pushx[pushdir] * 20; dy = pushy[pushdir] * 20; }
        else if (bot_path(cx, cy, gx, gy, &nx, &ny) && !(cx == gx && cy == gy)) {
            dx = nx * 16 + 8 - px; dy = ny * 16 + 8 - py;
        } else { dx = tx - px; dy = ty - py; }
        if (dx > 2) in |= IN_RIGHT; else if (dx < -2) in |= IN_LEFT;
        if (dy > 2) in |= IN_DOWN; else if (dy < -2) in |= IN_UP;
        if ((in & IN_FIRE) && best < 22) {
            WORD ex = tx - px, ey = ty - py;
            in &= ~(IN_LEFT | IN_RIGHT | IN_UP | IN_DOWN);
            if (ABS(ex) > ABS(ey)) in |= ex > 0 ? IN_RIGHT : IN_LEFT;
            else in |= ey > 0 ? IN_DOWN : IN_UP;
        }
    }
    if (p->hp < p->maxhp / 3 && hero.potions && (frame_count & 16)) in |= IN_FIRE2;
    return in;
}
#define input_read() (bot_input() | input_read())
#endif

static UWORD read_pressed(UWORD *held)
{
    UWORD in = input_read();
    UWORD pressed = in & ~prev_in;
    prev_in = in;
    if (held) *held = in;
    return pressed;
}

static void fade_out(void)
{
    WORD l;
    for (l = 16; l >= 0; l -= 2) { gfx_fade(l); wait_vbl(); }
}

static void fade_in(void)
{
    WORD l;
    for (l = 0; l <= 16; l += 2) { gfx_fade(l); wait_vbl(); }
}

static void clear_all(void)
{
    gfx_clear_area(bgbuf, 0, SCR_H);
    gfx_clear_area(frontbuf, 0, SCR_H);
    gfx_clear_area(backbuf, 0, SCR_H);
    gfx_reset_dirty();
}

static void text_center(WORD y, const char *s, UWORD col)
{
    WORD x = ((40 - str_len(s)) / 2) * 8;
    gfx_text_all(x < 0 ? 0 : x, y, s, col, -1);
}

/* decorative frame of wall tiles around the whole screen */
static void draw_frame(void)
{
    WORD x, y;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 20; x++) {
            UBYTE t = ((x * 7 + y * 3) % 11) == 0 ? TL_FLOOR2 : TL_FLOOR;
            if (y == 0) t = (x == 0 || x == 19) ? TL_ROCK : 2;
            else if (y == 1) t = (x == 0) ? 13 : (x == 19) ? 15 : (x % 5 == 2) ? T_TORCH : TL_FACE;
            else if (y == 15) t = (x == 0) ? 13 : (x == 19) ? 15 : T_CAP_N;
            else if (x == 0) t = 13;
            else if (x == 19) t = 15;
            gfx_tile(bgbuf, x * 16, y * 16, t);
            if (y == 2 && x > 0 && x < 19) gfx_shadow(bgbuf, x * 16, y * 16, SH_TOP);
            if (x == 1 && y > 1 && y < 15) gfx_shadow(bgbuf, x * 16, y * 16, SH_LEFT);
        }
    gfx_copy_area(bgbuf, frontbuf, 0, SCR_H);
    gfx_copy_area(bgbuf, backbuf, 0, SCR_H);
}

static void panel(WORD x, WORD y, WORD w, WORD h)
{
    gfx_rect_all(x, y, w, h, COL_HUDBG);
    gfx_rect_all(x, y, w, 1, COL_HUDLINE);
    gfx_rect_all(x, y + h - 1, w, 1, COL_HUDLINE);
    gfx_rect_all(x, y, 1, h, COL_HUDLINE);
    gfx_rect_all(x + w - 1, y, 1, h, COL_HUDLINE);
}

/* ------------------------------------------------------------------ title */
static WORD title_screen(void)
{
    static const UBYTE parade[] = { 96, 87, 98, 84, 100, 0, 124, 120, 108, 122, 121, 112, 110, 109, 111 };
    WORD i;
    fade_out();
    gfx_set_biome(0);
    clear_all();
    draw_frame();
    panel(24, 34, 272, 200);
    gfx_text2x_all(120, 40, "DELVE", COL_YELLOW);
    text_center(60, "A ROGUE-LITE DUNGEON CRAWL", COL_WHITE);
    text_center(94, "HEROES                 MONSTERS", COL_GREY);
    text_center(116, "MOVE     ARROWS / WASD / JOYSTICK", COL_WHITE);
    text_center(128, "ATTACK   SPACE / FIRE", COL_WHITE);
    text_center(140, "POTION   Q / 2ND FIRE BUTTON", COL_WHITE);
    text_center(152, "PAUSE P   MUSIC M   QUIT ESC", COL_WHITE);
    text_center(172, "RESCUE UP TO 3 COMPANIONS", COL_GREEN);
    text_center(182, "UPGRADE YOUR PARTY AT CAMP", COL_GREEN);
    text_center(202, "PRESS FIRE TO DELVE", COL_YELLOW);
    text_center(220, "ART AND SOUND: KENNEY.NL (CC0)", COL_GREY);
    gfx_copy_area(frontbuf, bgbuf, 0, SCR_H);      /* panel becomes the background */
    music_play(SONG_TITLE, 0);
    gfx_reset_dirty();
    prev_in = input_read();
    for (i = 0;; i++) {
        UWORD p = read_pressed(0);
        WORD k;
        bobs_begin();
        for (k = 0; k < (WORD)sizeof(parade); k++) {       /* the cast marches in place */
            UBYTE t = parade[k], slot;
            UWORD ft = t;
            WORD seq = (WORD)(((frame_count + k * 7) / 6) & 3);
            if (!t) continue;
            slot = anim_slot[t];
            if (slot != 255 && seq == 0) ft = anim_tab[slot][k > 5][AN_WALK_A];
            else if (slot != 255 && seq == 2) ft = anim_tab[slot][k > 5][AN_WALK_B];
            else if (k > 5 && tile_flip[t]) ft = tile_flip[t];
            bob_draw_shadowed(40 + k * 16, (seq & 1) ? 73 : 74, ft, VAR_NORMAL);
        }
        gfx_swap();
        if (i == 0) fade_in();
        if (p & IN_FIRE) { rnd_seed(frame_count * 2654435761UL ^ REG16(VHPOSR)); sound_play(SFX_SELECT, 5); return 1; }
        if (p & IN_ESC) return 0;
#ifdef TEST_QUIT
        if (frame_count > 100) return 0;
#endif
    }
}

/* ------------------------------------------------------------------ floor */
static void enter_room(WORD r, WORD fromdir)
{
    cur_room = r;
    room_locked = 0;
    room_draw();
    actors_enter_room(fromdir);
    hud_dirty();
}

static WORD pause_game(void)
{
    message("PAUSED - FIRE TO RESUME, ESC TO QUIT", 0);
    hud_update();
    for (;;) {
        UWORD p = read_pressed(0);
        wait_vbl();
        if (p & (IN_FIRE | IN_PAUSE)) { message("", 1); return 0; }
        if (p & IN_ESC) return 1;
    }
}

static WORD play_floor(void)
{
    static const BYTE gdx[4] = { 0, 1, 0, -1 }, gdy[4] = { -1, 0, 1, 0 };
    char buf[40];
    fade_out();
    clear_all();
    floor_generate(hero.depth);
    hud_full();
    enter_room(0, -1);
    str_cpy(buf, "FLOOR ");
    fmt_int(buf + 6, hero.depth);
    str_cat(buf, " - ");
    str_cat(buf, hero.depth % 5 == 0 ? "A BOSS LURKS HERE" : biome_name[((hero.depth - 1) / 3) % NUM_BIOMES]);
    message(buf, 150);
    hud_update();
    actors_draw();
    gfx_swap();
    fade_in();
    music_for_floor();
    if (hero.depth == 1) sound_play(SFX_V_PREPARE, 10);
    else sound_play(SFX_FLOOR, 9);
    game_event = EV_NONE;
#ifdef AUTOPLAY
    in_play = 1;
    bot_menu_reset = 1;
#endif
    prev_in = input_read();

    for (;;) {
        UWORD held, pressed = read_pressed(&held);
        if (pressed & (IN_ESC | IN_PAUSE)) {
            if (pause_game()) return EV_QUIT;
            continue;
        }
        if (pressed & IN_MUSIC) { music_toggle(); message(music_on ? "MUSIC ON" : "MUSIC OFF", 50); }
        room_change_dir = -1;
#ifdef AUTOPLAY
        { extern UWORD prof_t0; prof_t0 = REG16(VHPOSR) >> 8 | ((REG16(VPOSR) & 1) << 8); }
#endif
        actors_update(held, pressed);
#ifdef AUTOPLAY
        { extern UWORD prof_t0, prof_upd; UWORD t = REG16(VHPOSR) >> 8 | ((REG16(VPOSR) & 1) << 8);
          prof_upd = (t - prof_t0 + 313) % 313; prof_t0 = t; }
#endif
        if (room_change_dir >= 0) {
            Room *r = &rooms[cur_room];
            WORD nx = r->gx + gdx[room_change_dir], ny = r->gy + gdy[room_change_dir];
            if (nx >= 0 && ny >= 0 && nx < grid_w && ny < grid_h && grid[ny][nx] >= 0) {
                WORD l;
                for (l = 16; l >= 0; l -= 4) { gfx_fade(l); wait_vbl(); }
                enter_room(grid[ny][nx], (room_change_dir + 2) & 3);
                hud_update();
                actors_draw();
                gfx_swap();
                actors_draw();
                gfx_swap();
                for (l = 0; l <= 16; l += 4) { gfx_fade(l); wait_vbl(); }
                continue;
            }
        }
        if (game_event != EV_NONE) {
#ifdef AUTOPLAY
            in_play = 0;
#endif
            return game_event;
        }
        actors_draw();
#ifdef AUTOPLAY
        { extern UWORD prof_t0, prof_drw; UWORD t = REG16(VHPOSR) >> 8 | ((REG16(VPOSR) & 1) << 8);
          prof_drw = (t - prof_t0 + 313) % 313; }
#endif
        message_tick();
        hud_update();
#ifdef AUTOPLAY
        {   /* test build: frames drawn per second */
            static ULONG last_vbl; static WORD frames;
            frames++;
            if (frame_count - last_vbl >= 50) {
                char b[12];
                char d[40];
                str_cpy(b, "FPS "); fmt_int(b + 4, frames);
                gfx_rect_all(0, 247, 320, 8, COL_HUDBG);
                gfx_text_all(272, 247, b, COL_GREEN, -1);
                fmt_int(d, actors[0].x >> 4); str_cat(d, ","); fmt_int(d + str_len(d), actors[0].y >> 4);
                str_cat(d, " U"); fmt_int(d + str_len(d), prof_upd);
                str_cat(d, " D"); fmt_int(d + str_len(d), prof_drw);
                gfx_text_all(0, 247, d, COL_WHITE, -1);
                frames = 0; last_vbl = frame_count;
            }
        }
#endif
        gfx_swap();
    }
}

/* ------------------------------------------------------------------ camp */
enum { CM_REST, CM_SHARPEN, CM_ARMOR, CM_POTION, CM_TRAIN, CM_HIRE, CM_DESCEND };
typedef struct { UBYTE what, arg; WORD cost; } CampItem;

static WORD build_camp_menu(CampItem *m, WORD hire_cls)
{
    WORD n = 0, i, d = hero.depth;
    m[n].what = CM_REST;    m[n].arg = 0; m[n++].cost = 5 + d * 3;
    m[n].what = CM_SHARPEN; m[n].arg = 0; m[n++].cost = 20 + hero.wplus * 15 + d * 2;
    m[n].what = CM_ARMOR;   m[n].arg = 0; m[n++].cost = 25 + hero.armor * 20;
    m[n].what = CM_POTION;  m[n].arg = 0; m[n++].cost = 12 + d * 2;
    for (i = 0; i < MAX_ALLIES; i++) {
        Actor *a = &actors[ACT_ALLY0 + i];
        if (a->alive || a->ko) { m[n].what = CM_TRAIN; m[n].arg = (UBYTE)i; m[n++].cost = 20 + a->level * 15; }
    }
    if (party_size() < MAX_ALLIES && hire_cls >= 0) {
        m[n].what = CM_HIRE; m[n].arg = (UBYTE)hire_cls; m[n++].cost = 60 + d * 5;
    }
    m[n].what = CM_DESCEND; m[n].arg = 0; m[n++].cost = 0;
    return n;
}

static void camp_label(char *buf, CampItem *c)
{
    switch (c->what) {
    case CM_REST:    str_cpy(buf, "REST: HEAL + REVIVE PARTY"); break;
    case CM_SHARPEN: str_cpy(buf, "SHARPEN WEAPON (+"); fmt_int(buf + str_len(buf), hero.wplus + 1); str_cat(buf, ")"); break;
    case CM_ARMOR:   str_cpy(buf, "REINFORCE ARMOR (+1)"); break;
    case CM_POTION:  str_cpy(buf, "BUY HEALTH POTION"); break;
    case CM_TRAIN:   str_cpy(buf, "TRAIN "); str_cat(buf, allydefs[actors[ACT_ALLY0 + c->arg].cls].name);
                     str_cat(buf, " TO LV"); fmt_int(buf + str_len(buf), actors[ACT_ALLY0 + c->arg].level + 1); break;
    case CM_HIRE:    str_cpy(buf, "HIRE "); str_cat(buf, allydefs[c->arg].name); str_cat(buf, " THE ");
                     str_cat(buf, allydefs[c->arg].title); break;
    case CM_DESCEND: str_cpy(buf, "DESCEND TO FLOOR "); fmt_int(buf + str_len(buf), hero.depth + 1); break;
    }
}

static void camp_party(void)
{
    WORD i, x;
    char buf[24];
    panel(16, 56, 288, 52);
    gfx_stamp_both(32, 60, 96, VAR_NORMAL);
    str_cpy(buf, "HERO"); gfx_text_all(24, 78, buf, COL_YELLOW, -1);
    str_cpy(buf, "L"); fmt_int(buf + 1, hero.level); gfx_text_all(32, 88, buf, COL_WHITE, -1);
    fmt_int(buf, actors[0].hp); str_cat(buf, "HP"); gfx_text_all(24, 97, buf, COL_RED, -1);
    for (i = 0; i < MAX_ALLIES; i++) {
        Actor *a = &actors[ACT_ALLY0 + i];
        x = 96 + i * 72;
        if (!a->alive && !a->ko) { gfx_text_all(x + 8, 78, "EMPTY", COL_GREY, -1); continue; }
        gfx_stamp_both(x + 16, 60, a->ko ? T_KO : allydefs[a->cls].tile, VAR_NORMAL);
        gfx_text_all(x, 78, allydefs[a->cls].name, COL_YELLOW, -1);
        str_cpy(buf, "L"); fmt_int(buf + 1, a->level); gfx_text_all(x, 88, buf, COL_WHITE, -1);
        if (a->ko) str_cpy(buf, "KO");
        else { fmt_int(buf, a->hp); str_cat(buf, "HP"); }
        gfx_text_all(x, 97, buf, a->ko ? COL_GREY : COL_GREEN, -1);
    }
}

static void camp_draw(CampItem *m, WORD n, WORD sel)
{
    WORD i;
    char buf[44];
    panel(16, 112, 288, 122);
    for (i = 0; i < n; i++) {
        WORD y = 118 + i * 11;
        BOOL afford = hero.gold >= m[i].cost;
        camp_label(buf, &m[i]);
        gfx_text_all(32, y, buf, i == sel ? COL_YELLOW : afford ? COL_WHITE : COL_GREY, -1);
        if (i == sel) gfx_text_all(24, y, ">", COL_YELLOW, -1);
        if (m[i].cost) {
            fmt_int(buf, m[i].cost); str_cat(buf, "G");
            gfx_text_all(296 - str_len(buf) * 8, y, buf, afford ? COL_YELLOW : COL_GREY, -1);
        }
    }
    gfx_rect_all(24, 222, 270, 8, COL_HUDBG);
    str_cpy(buf, "GOLD: "); fmt_int(buf + 6, hero.gold);
    str_cat(buf, "   ATK "); fmt_int(buf + str_len(buf), weapons[hero.weapon].dmg + hero.wplus + hero.atkbonus);
    str_cat(buf, "  ARM "); fmt_int(buf + str_len(buf), hero.armor);
    gfx_text_all(32, 222, buf, COL_YELLOW, -1);
}

static void camp(void)
{
    CampItem m[12];
    WORD n, sel = 0, hire_cls = -1, tries;
    char buf[44];

    for (tries = 0; tries < 20; tries++) {   /* someone at the campfire to hire */
        WORD c = RND(NUM_CLASSES), i;
        BOOL have = 0;
        for (i = 0; i < MAX_ALLIES; i++)
            if ((actors[ACT_ALLY0 + i].alive || actors[ACT_ALLY0 + i].ko) && actors[ACT_ALLY0 + i].cls == c) have = 1;
        if (!have) { hire_cls = c; break; }
    }

    fade_out();
    clear_all();
    draw_frame();
    panel(16, 34, 288, 18);
    str_cpy(buf, "CAMP - FLOOR "); fmt_int(buf + str_len(buf), hero.depth); str_cat(buf, " CLEARED");
    text_center(39, buf, COL_YELLOW);
    camp_party();
    music_play(SONG_CAMP, 0);
    n = build_camp_menu(m, hire_cls);
#ifdef AUTOPLAY
    sel = n - 1;
#endif
    camp_draw(m, n, sel);
    fade_in();
    prev_in = input_read();

    for (;;) {
        UWORD p = read_pressed(0);
        wait_vbl();
        if (p & IN_UP) { sel = (sel + n - 1) % n; sound_play(SFX_SELECT, 1); camp_draw(m, n, sel); }
        if (p & IN_DOWN) { sel = (sel + 1) % n; sound_play(SFX_SELECT, 1); camp_draw(m, n, sel); }
        if (p & IN_FIRE) {
            CampItem *c = &m[sel];
            if (c->what == CM_DESCEND) { sound_play(SFX_DOOR, 5); return; }
            if (hero.gold < c->cost) { sound_play(SFX_HURT, 3); continue; }
            if (c->what == CM_POTION && hero.potions >= 9) { sound_play(SFX_HURT, 3); continue; }
            hero.gold -= c->cost;
            sound_play(SFX_COIN, 5);
            switch (c->what) {
            case CM_REST: party_heal_all(1); break;
            case CM_SHARPEN: hero.wplus++; break;
            case CM_ARMOR: hero.armor++; break;
            case CM_POTION: hero.potions++; break;
            case CM_TRAIN: {
                Actor *a = &actors[ACT_ALLY0 + c->arg];
                a->level++;
                a->maxhp += allydefs[a->cls].hp / 5 + 2;
                a->atk++;
                if (a->alive) a->hp = a->maxhp;
                break;
            }
            case CM_HIRE:
                ally_recruit(c->arg);
                sound_play(SFX_LEVELUP, 6);
                break;
            }
            camp_party();
            n = build_camp_menu(m, c->what == CM_HIRE ? -1 : hire_cls);
            if (c->what == CM_HIRE) hire_cls = -1;
            if (sel >= n) sel = n - 1;
            camp_draw(m, n, sel);
        }
    }
}

/* ------------------------------------------------------------------ game over */
static void game_over(void)
{
    char buf[40];
    WORD i;
    music_stop();
    sound_play(SFX_V_GAMEOVER, 10);
    for (i = 0; i < 60; i++) wait_vbl();
    sound_play(SFX_GAMEOVER, 10);
    fade_out();
    clear_all();
    draw_frame();
    panel(40, 60, 240, 120);
    gfx_text2x_all(88, 72, "YOU DIED", COL_RED);
    str_cpy(buf, "REACHED FLOOR "); fmt_int(buf + str_len(buf), hero.depth);
    text_center(100, buf, COL_WHITE);
    str_cpy(buf, "HERO LEVEL "); fmt_int(buf + str_len(buf), hero.level);
    text_center(112, buf, COL_WHITE);
    str_cpy(buf, "MONSTERS SLAIN "); fmt_int(buf + str_len(buf), hero.kills);
    text_center(124, buf, COL_WHITE);
    str_cpy(buf, "GOLD "); fmt_int(buf + str_len(buf), hero.gold);
    text_center(136, buf, COL_YELLOW);
    text_center(160, "PRESS FIRE", COL_YELLOW);
    fade_in();
    for (i = 0; i < 50; i++) wait_vbl();
    prev_in = input_read();
    for (;;) {
        UWORD p = read_pressed(0);
        wait_vbl();
        if (p & (IN_FIRE | IN_ESC)) return;
    }
}

/* ------------------------------------------------------------------ main */
int main(void)
{
    int r;
    if (!gfx_init()) return 21;
    r = sys_takeover();
    if (r != 1) { gfx_free(); return 30 + r; }
    gfx_fade(0);
    gfx_start_display();
    sound_init();

    while (title_screen()) {
        party_new_game();
        for (;;) {
            WORD ev = play_floor();
            if (ev == EV_DEAD) { game_over(); break; }
            if (ev == EV_QUIT) break;
            camp();
            hero.depth++;
        }
    }

    fade_out();
    sound_stop();
    sys_restore();
    gfx_free();
    return 0;
}
