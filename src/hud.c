/* Status panel at the bottom of the screen (y 224..255). */
#include "game.h"
#include "gfx.h"
#include "sys.h"
#include "assets.h"

#define HY 224
static UBYTE dirty = 1;
static char msg[44];
static WORD msg_timer;
static UBYTE msg_dirty;

void hud_dirty(void) { dirty = 1; }

static void bar(WORD x, WORD y, WORD w, WORD h, WORD val, WORD max, UWORD col, UWORD back)
{
    WORD f = max > 0 ? (WORD)((LONG)w * MAX(0, val) / max) : 0;
    if (f > w) f = w;
    gfx_rect_all(x, y, w, h, back);
    if (f > 0) gfx_rect_all(x, y, f, h, col);
}

/* cached values: each HUD section is redrawn only when its numbers change */
static WORD c_wpn = -1, c_wplus, c_hp = -1, c_maxhp, c_xp = -1, c_lv = -1, c_depth = -1,
            c_pot = -1, c_room = -1;
static LONG c_gold = -1;
static WORD c_ally[MAX_ALLIES][4];

void hud_full(void)
{
    gfx_rect_all(0, HY, SCR_W, SCR_H - HY, COL_HUDBG);
    gfx_rect_all(0, HY, SCR_W, 1, COL_HUDLINE);
    dirty = 1;
    c_room = -1;
    msg_dirty = 1;
    hud_update();
}

void hud_update(void)
{
    char buf[24];
    WORD i;
    Actor *p = &actors[0];
    if (msg_dirty) {
        WORD len = str_len(msg), x = ((40 - len) / 2) * 8;
        msg_dirty = 0;
        gfx_rect_all(0, 247, SCR_W, 8, COL_HUDBG);
        if (len) gfx_text_all(x < 0 ? 0 : x, 247, msg, COL_YELLOW, -1);
    }
    if (dirty) {                     /* forced full refresh */
        dirty = 0;
        c_wpn = c_hp = c_xp = c_lv = c_depth = c_pot = c_room = -1;
        c_gold = -1;
        for (i = 0; i < MAX_ALLIES; i++) c_ally[i][0] = -2;
    }
    if (c_wpn != hero.weapon || c_wplus != hero.wplus) {
        c_wpn = hero.weapon; c_wplus = hero.wplus;
        gfx_rect_all(0, 225, 22, 22, COL_HUDBG);
        gfx_stamp_both(4, 227, weapons[hero.weapon].tile, VAR_NORMAL);
        if (hero.wplus) {
            str_cpy(buf, "+"); fmt_int(buf + 1, hero.wplus);
            gfx_text_all(0, 238, buf, COL_WHITE, -1);
        }
    }
    if (c_hp != p->hp || c_maxhp != p->maxhp) {
        c_hp = p->hp; c_maxhp = p->maxhp;
        gfx_rect_all(24, 227, 64, 8, COL_HUDBG);
        fmt_int(buf, MAX(0, p->hp)); str_cat(buf, "/"); fmt_int(buf + str_len(buf), p->maxhp);
        gfx_text_all(24, 227, buf, COL_WHITE, -1);
        bar(24, 237, 64, 3, p->hp, p->maxhp, COL_RED, COL_DARKRED);
    }
    if (c_xp != (WORD)hero.xp || c_lv != hero.level) {
        c_xp = hero.xp; c_lv = hero.level;
        bar(24, 242, 64, 2, hero.xp, hero.xpnext, COL_BLUE, COL_HUDLINE);
        gfx_rect_all(96, 227, 32, 8, COL_HUDBG);
        str_cpy(buf, "LV"); fmt_int(buf + 2, hero.level);
        gfx_text_all(96, 227, buf, COL_GREEN, -1);
    }
    if (c_depth != hero.depth) {
        c_depth = hero.depth;
        gfx_rect_all(128, 227, 16, 8, COL_HUDBG);
        str_cpy(buf, "F"); fmt_int(buf + 1, hero.depth);
        gfx_text_all(128, 227, buf, COL_ORANGE, -1);
    }
    if (c_gold != hero.gold) {
        c_gold = hero.gold;
        gfx_rect_all(96, 237, 48, 8, COL_HUDBG);
        str_cpy(buf, "G"); fmt_int(buf + 1, hero.gold);
        gfx_text_all(96, 237, buf, COL_YELLOW, -1);
    }
    if (c_pot != hero.potions) {
        c_pot = hero.potions;
        gfx_rect_all(144, 225, 32, 18, COL_HUDBG);
        gfx_stamp_both(144, 225, 115, VAR_NORMAL);
        str_cpy(buf, "X"); fmt_int(buf + 1, hero.potions);
        gfx_text_all(160, 230, buf, COL_WHITE, -1);
    }
    for (i = 0; i < MAX_ALLIES; i++) {
        Actor *a = &actors[ACT_ALLY0 + i];
        WORD x = 184 + i * 34;
        WORD present = (a->alive || a->ko) ? a->cls : -1;
        if (c_ally[i][0] == present && c_ally[i][1] == a->level &&
            c_ally[i][2] == a->hp && c_ally[i][3] == a->ko) continue;
        c_ally[i][0] = present; c_ally[i][1] = a->level; c_ally[i][2] = a->hp; c_ally[i][3] = a->ko;
        gfx_rect_all(x, 225, 33, 21, COL_HUDBG);
        if (present < 0) continue;
        gfx_stamp_both(x, 225, a->ko ? T_KO : allydefs[a->cls].tile, VAR_NORMAL);
        str_cpy(buf, "L"); fmt_int(buf + 1, a->level);
        gfx_text_all(((x + 16) + 7) & ~7, 229, buf, a->ko ? COL_GREY : COL_WHITE, -1);
        bar(x, 242, 30, 3, a->ko ? 0 : a->hp, a->maxhp, COL_GREEN, COL_DARKGREEN);
    }
    if (c_room != cur_room) {
        c_room = cur_room;
        minimap_draw();
    }
}

void message(const char *s, WORD frames)
{
    WORD i;
    for (i = 0; i < 40 && s[i]; i++) msg[i] = s[i];
    msg[i] = 0;
    msg_timer = frames;
    msg_dirty = 1;
}

void message2(const char *a, const char *b, WORD frames)
{
    char buf[44];
    str_cpy(buf, a);
    str_cat(buf, b);
    message(buf, frames);
}

void message_tick(void)
{
    if (msg_timer && !--msg_timer) { msg[0] = 0; msg_dirty = 1; }
}
