/* Floor generation: a grid of flip-screen rooms connected by doorways.
 * Rooms are built as a rock/floor map, then autotiled in Tiny Dungeon's 3/4 style:
 * stone-capped rock masses, brick faces under every wall that faces the camera,
 * torches/banners on the faces, furniture on the floor and baked soft shadows. */
#include "game.h"
#include "gfx.h"
#include "sys.h"
#include "sound.h"
#include "assets.h"

Room rooms[MAX_ROOMS];
WORD grid[GRID_MAX][GRID_MAX];
WORD grid_w, grid_h, nrooms, cur_room;
UBYTE room_locked;
WORD captive_x, captive_y;

static UBYTE solid_tab[256];
static UBYTE rocky_tab[256];      /* rock or wall face: casts shadow */

static void init_solid(void)
{
    static const UBYTE rock[] = { 0, 1, 2, 12, 13, 15, 16, 17, 24, T_CAP_N, T_CAP_NW, T_CAP_NE,
        T_IN_SW, T_IN_NE, T_IN_NW, T_CAP_NS, TL_FACE, TL_WALL2, TL_WALL3, 14, T_TORCH,
        TL_BANNER, TL_GRATE, TL_STATUE, TL_FOUNTAIN, TL_DOOR, TL_DDOOR_L, TL_DDOOR_R };
    static const UBYTE objs[] = { TL_BARREL, TL_TABLE, TL_SHELF, 75, TL_TOMB, TL_CROSS,
        TL_ANVIL, TL_CHEST, TL_MIMIC, TL_BIGCHEST, TL_STOOL, 61 };
    WORD i;
    for (i = 0; i < (WORD)sizeof(rock); i++) solid_tab[rock[i]] = rocky_tab[rock[i]] = 1;
    for (i = 0; i < (WORD)sizeof(objs); i++) solid_tab[objs[i]] = 1;
}

BOOL tile_solid(UBYTE t) { return solid_tab[t]; }

UBYTE tile_at(WORD tx, WORD ty)
{
    Room *r = &rooms[cur_room];
    if (tx < 0 || ty < 0 || tx >= ROOM_W || ty >= ROOM_H) return TL_ROCK;
    return r->obj[ty][tx] ? r->obj[ty][tx] : r->floor[ty][tx];
}

/* px,py in pixels. ghosts may cross interior walls but not the outer ring */
BOOL solid_px(WORD px, WORD py, BOOL ghost)
{
    WORD tx = px >> 4, ty = py >> 4;
    if (px < 0 || py < 0 || px >= ROOM_W * 16 || py >= ROOM_H * 16) return 1;
    if (ghost && !(tx == 0 || ty <= 1 || tx == ROOM_W - 1 || ty == ROOM_H - 1)) return 0;
    return solid_tab[tile_at(tx, ty)];
}

/* ------------------------------------------------------------------ items */
UBYTE item_tile(const Item *it)
{
    switch (it->kind) {
    case IK_GOLD:     return 101;
    case IK_POTION:   return 115;
    case IK_VITALITY: return 114;
    case IK_STRENGTH: return 116;
    case IK_ELIXIR:   return 113;
    case IK_WEAPON:   return weapons[it->val].tile;
    case IK_SHIELD:   return 102;
    }
    return 101;
}

WORD item_at(WORD tx, WORD ty)
{
    Room *r = &rooms[cur_room];
    WORD i;
    for (i = 0; i < r->nitems; i++)
        if (r->items[i].tx == tx && r->items[i].ty == ty) return i;
    return -1;
}

static const UWORD shade_mask[4] = { 0, SH_TOP, SH_LEFT, SH_TL };

/* draw one cell (all layers) into the background buffer */
static void paint_cell(Room *r, WORD tx, WORD ty)
{
    WORD x = tx * 16, y = ty * 16, i;
    UBYTE sh = r->shade[ty][tx];
    gfx_tile(bgbuf, x, y, r->floor[ty][tx]);
    if (sh & 3) gfx_shadow(bgbuf, x, y, shade_mask[sh & 3]);
    if (r->obj[ty][tx]) {
        gfx_shadow(bgbuf, x, y, SH_DROP);
        gfx_stamp(bgbuf, x, y, r->obj[ty][tx], VAR_NORMAL);
    }
    for (i = 0; i < r->nitems; i++)
        if (r->items[i].tx == tx && r->items[i].ty == ty) {
            gfx_shadow(bgbuf, x, y, SH_DROP);
            gfx_stamp(bgbuf, x, y - 1, item_tile(&r->items[i]), VAR_NORMAL);
        }
}

static void redraw_cell(WORD tx, WORD ty)
{
    paint_cell(&rooms[cur_room], tx, ty);
    gfx_copy_cell(tx * 16, ty * 16);
    /* items are drawn 1px high: refresh the cell above's bottom edge too */
    if (ty > 0) { paint_cell(&rooms[cur_room], tx, ty - 1); gfx_copy_cell(tx * 16, (ty - 1) * 16); }
}

void room_set_tile(WORD tx, WORD ty, UBYTE t)
{
    rooms[cur_room].floor[ty][tx] = t;
    redraw_cell(tx, ty);
}

void room_set_obj(WORD tx, WORD ty, UBYTE t)
{
    rooms[cur_room].obj[ty][tx] = t;
    redraw_cell(tx, ty);
}

static BOOL cell_free(Room *r, WORD tx, WORD ty)
{
    WORD i;
    if (tx < 1 || ty < 2 || tx >= ROOM_W - 1 || ty >= ROOM_H - 1) return 0;
    if (r->obj[ty][tx] || solid_tab[r->floor[ty][tx]]) return 0;
    for (i = 0; i < r->nitems; i++)
        if (r->items[i].tx == tx && r->items[i].ty == ty) return 0;
    return 1;
}

/* drop near (tx,ty), spiralling outwards for a free cell */
BOOL item_drop(WORD tx, WORD ty, UBYTE kind, UBYTE val, UBYTE plus)
{
    Room *r = &rooms[cur_room];
    WORD rad, dx, dy;
    if (r->nitems >= MAX_ITEMS) return 0;
    for (rad = 0; rad < 6; rad++)
        for (dy = -rad; dy <= rad; dy++)
            for (dx = -rad; dx <= rad; dx++) {
                if (ABS(dx) != rad && ABS(dy) != rad) continue;
                if (cell_free(r, tx + dx, ty + dy)) {
                    Item *it = &r->items[r->nitems++];
                    it->tx = (UBYTE)(tx + dx);
                    it->ty = (UBYTE)(ty + dy);
                    it->kind = kind;
                    it->val = val;
                    it->plus = plus;
                    redraw_cell(tx + dx, ty + dy);
                    return 1;
                }
            }
    return 0;
}

void item_remove(WORD idx)
{
    Room *r = &rooms[cur_room];
    WORD tx = r->items[idx].tx, ty = r->items[idx].ty;
    r->items[idx] = r->items[--r->nitems];
    redraw_cell(tx, ty);
}

/* quality: 0 = monster drop, 1 = chest, 2 = big chest */
void item_random_drop(WORD tx, WORD ty, WORD quality)
{
    WORD d = hero.depth;
    WORD roll = RND(100);
    if (quality == 0) {
        if (roll < 45) item_drop(tx, ty, IK_GOLD, (UBYTE)MIN(250, 2 + d * 2 + RND(4 + d * 2)), 0);
        else if (roll < 56) item_drop(tx, ty, IK_POTION, 0, 0);
        else if (roll < 60) {
            WORD w = MIN(NUM_WEAPONS - 1, RND(d / 2 + 3));
            item_drop(tx, ty, IK_WEAPON, (UBYTE)w, (UBYTE)RND(d / 4 + 1));
        }
        return;
    }
    roll -= quality * 10;
    if (roll < 22) {
        WORD w = MIN(NUM_WEAPONS - 1, RND(d / 2 + 2 + quality) + quality);
        item_drop(tx, ty, IK_WEAPON, (UBYTE)w, (UBYTE)RND(d / 3 + quality));
    } else if (roll < 32) item_drop(tx, ty, IK_SHIELD, 0, 0);
    else if (roll < 42) item_drop(tx, ty, IK_VITALITY, 0, 0);
    else if (roll < 52) item_drop(tx, ty, IK_STRENGTH, 0, 0);
    else if (roll < 60) item_drop(tx, ty, IK_ELIXIR, 0, 0);
    else if (roll < 75) item_drop(tx, ty, IK_POTION, 0, 0);
    else item_drop(tx, ty, IK_GOLD, (UBYTE)MIN(250, 10 + d * 5 + RND(10 + d * 4)), 0);
}

/* ------------------------------------------------------------------ room building */
/* cells just inside each doorway (must stay walkable) */
static const BYTE door_inner[4][2][2] = {
    { { 9, 2 }, { 10, 2 } }, { { 18, 6 }, { 18, 7 } },
    { { 9, 12 }, { 10, 12 } }, { { 1, 6 }, { 1, 7 } },
};

enum { S_FLOOR, S_ROCK };
static UBYTE sem[ROOM_H][ROOM_W];

static BOOL is_rock(WORD x, WORD y)
{
    if (x < 0 || y < 0 || x >= ROOM_W || y >= ROOM_H) return 1;
    return sem[y][x] == S_ROCK;
}

static void rock_rect(WORD x0, WORD y0, WORD w, WORD h)
{
    WORD x, y;
    /* only on clear floor with a 1-cell margin (+ the face row below) */
    for (y = y0 - 1; y <= y0 + h + 1; y++)
        for (x = x0 - 1; x <= x0 + w; x++)
            if (x < 0 || y < 0 || x >= ROOM_W || y >= ROOM_H || (sem[y][x] == S_ROCK &&
                x > 0 && x < ROOM_W - 1 && y > 0 && y < ROOM_H - 1)) return;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            if (x > 0 && y > 1 && x < ROOM_W - 1 && y < ROOM_H - 2) sem[y][x] = S_ROCK;
}

static UBYTE rock_tile(WORD x, WORD y)
{
    BOOL n = !is_rock(x, y - 1), e = !is_rock(x + 1, y), s = !is_rock(x, y + 1), w = !is_rock(x - 1, y);
    if (n && s) return T_CAP_NS;
    if (s && w) return 16;
    if (s && e) return 17;
    if (n && w) return T_CAP_NW;
    if (n && e) return T_CAP_NE;
    if (s) return 2;
    if (n) return T_CAP_N;
    if (w) return 15;
    if (e) return 13;
    if (!is_rock(x + 1, y + 1)) return 1;
    if (!is_rock(x - 1, y + 1)) return T_IN_SW;
    if (!is_rock(x + 1, y - 1)) return T_IN_NE;
    if (!is_rock(x - 1, y - 1)) return T_IN_NW;
    { WORD v = RND(12); return v == 0 ? 12 : v == 1 ? 24 : 0; }
}

static void put_obj(Room *r, WORD x, WORD y, UBYTE t)
{
    if (x > 0 && y > 1 && x < ROOM_W - 1 && y < ROOM_H - 1 &&
        !solid_tab[r->floor[y][x]] && !r->obj[y][x]) r->obj[y][x] = t;
}

static void interior(Room *r, WORD pattern, BOOL objects)
{
    WORD i, x, y;
    if (!objects) {                   /* rock layout */
        switch (pattern) {
        case 1: rock_rect(4, 3, 2, 2); rock_rect(14, 3, 2, 2); rock_rect(4, 8, 2, 2); rock_rect(14, 8, 2, 2); break;
        case 2: rock_rect(7, 5, 6, 2); break;
        case 3: rock_rect(3, 4, 3, 3); rock_rect(14, 7, 3, 3); break;
        case 8: for (i = 0; i < 3; i++) rock_rect(2 + RND(14), 3 + RND(6), 2 + RND(2), 2); break;
        case 9: rock_rect(5, 3, 2, 3); rock_rect(13, 3, 2, 3); rock_rect(9, 8, 2, 2); break;
        }
        return;
    }
    switch (pattern) {
    case 4:                                    /* storeroom */
        for (i = 0; i < 8; i++) put_obj(r, 2 + RND(16), RND(2) ? 2 : 11, RND(3) ? TL_BARREL : TL_TABLE);
        put_obj(r, 5, 6, TL_TABLE); put_obj(r, 6, 6, TL_TABLE); put_obj(r, 5, 7, TL_STOOL);
        break;
    case 5:                                    /* crypt */
        for (y = 4; y <= 10; y += 3)
            for (x = 3; x <= 16; x += 4)
                if (RND(3)) put_obj(r, x, y, RND(2) ? TL_TOMB : TL_CROSS);
        break;
    case 6:                                    /* library */
        for (x = 2; x < 18; x++) if (x < 8 || x > 11) put_obj(r, x, 2, RND(2) ? TL_SHELF : 75);
        put_obj(r, 8, 6, TL_TABLE); put_obj(r, 11, 7, TL_TABLE); put_obj(r, 9, 6, TL_STOOL);
        break;
    case 7:                                    /* forge */
        put_obj(r, 4, 4, TL_ANVIL); put_obj(r, 15, 9, TL_ANVIL);
        for (i = 0; i < 4; i++) put_obj(r, 2 + i, 11, TL_BARREL);
        break;
    default:                                   /* a little clutter */
        for (i = RND(3); i > 0; i--) put_obj(r, 2 + RND(16), 3 + RND(8), RND(2) ? TL_BARREL : TL_STOOL);
        break;
    }
}

static UBYTE flood[ROOM_H][ROOM_W];
static void flood_fill(Room *r, WORD sx, WORD sy)
{
    static UBYTE qx[ROOM_W * ROOM_H], qy[ROOM_W * ROOM_H];
    WORD head = 0, tail = 0, x, y;
    for (y = 0; y < ROOM_H; y++)
        for (x = 0; x < ROOM_W; x++) flood[y][x] = 0;
    qx[tail] = (UBYTE)sx; qy[tail++] = (UBYTE)sy; flood[sy][sx] = 1;
    while (head < tail) {
        static const BYTE ddx[4] = { 0, 1, 0, -1 }, ddy[4] = { -1, 0, 1, 0 };
        WORD i;
        x = qx[head]; y = qy[head++];
        for (i = 0; i < 4; i++) {
            WORD nx = x + ddx[i], ny = y + ddy[i];
            UBYTE t;
            if (nx < 0 || ny < 0 || nx >= ROOM_W || ny >= ROOM_H || flood[ny][nx]) continue;
            t = r->obj[ny][nx] ? r->obj[ny][nx] : r->floor[ny][nx];
            if (solid_tab[t] && t != TL_CHEST && t != TL_BIGCHEST && t != TL_MIMIC) continue;
            flood[ny][nx] = 1;
            qx[tail] = (UBYTE)nx; qy[tail++] = (UBYTE)ny;
        }
    }
}

static BOOL room_ok(Room *r)
{
    WORD d, c, x, y, sx = 9, sy = 6, total = 0;
    for (d = 0; d < 4; d++)
        if (r->doors & (1 << d)) { sx = door_inner[d][0][0]; sy = door_inner[d][0][1]; }
    if (solid_tab[r->floor[sy][sx]] || r->obj[sy][sx]) return 0;
    flood_fill(r, sx, sy);
    for (d = 0; d < 4; d++)
        if (r->doors & (1 << d))
            for (c = 0; c < 2; c++)
                if (!flood[door_inner[d][c][1]][door_inner[d][c][0]]) return 0;
    for (y = 1; y < ROOM_H - 1; y++)
        for (x = 1; x < ROOM_W - 1; x++) {
            UBYTE t = r->obj[y][x] ? r->obj[y][x] : r->floor[y][x];
            if (!solid_tab[t] && !flood[y][x]) return 0;     /* no sealed pockets */
            total += flood[y][x];
        }
    return total > 110;
}

static void finish_room(Room *r)
{
    WORD x, y, d, c, run = 0;
    /* autotile rock, faces under rock, floors elsewhere */
    for (y = 0; y < ROOM_H; y++)
        for (x = 0; x < ROOM_W; x++) {
            if (sem[y][x] == S_ROCK) r->floor[y][x] = rock_tile(x, y);
            else if (is_rock(x, y - 1) && y > 0 &&
                     !((x == 0 || x == ROOM_W - 1) && (y == 6 || y == 7))) r->floor[y][x] = TL_FACE;
            else {
                WORD v = RND(100);
                r->floor[y][x] = v < 74 ? TL_FLOOR : v < 93 ? TL_FLOOR2 : v < 96 ? TL_FLOOR3 : 50;
            }
        }
    /* north doorway arch */
    if (r->doors & DOOR_N) { r->floor[1][9] = TL_ARCH_L; r->floor[1][10] = TL_ARCH_R; }
    /* decorate the top wall faces: torches, banners, statue niches */
    for (x = 1; x < ROOM_W - 1; x++) {
        if (r->floor[1][x] != TL_FACE) { run = 0; continue; }
        if (x >= 8 && x <= 11 && (r->doors & DOOR_N)) { run = 0; continue; }
        run++;
        if (run >= 4 + RND(3)) { r->floor[1][x] = T_TORCH; run = 0; }
        else if (RND(14) == 0) r->floor[1][x] = RND(3) ? TL_BANNER : RND(2) ? TL_STATUE : TL_GRATE;
    }
    /* torches on interior block faces too */
    for (y = 3; y < ROOM_H - 1; y++)
        for (x = 1; x < ROOM_W - 1; x++)
            if (r->floor[y][x] == TL_FACE && RND(6) == 0) r->floor[y][x] = T_TORCH;
    /* keep door approaches clear of furniture */
    for (d = 0; d < 4; d++)
        if (r->doors & (1 << d))
            for (c = 0; c < 2; c++) r->obj[door_inner[d][c][1]][door_inner[d][c][0]] = 0;
}

static void compute_shade(Room *r)
{
    WORD x, y;
    for (y = 0; y < ROOM_H; y++)
        for (x = 0; x < ROOM_W; x++) {
            UBYTE s = 0;
            if (!rocky_tab[r->floor[y][x]]) {
                if (y > 0 && rocky_tab[r->floor[y - 1][x]]) s |= 1;
                if (x > 0 && rocky_tab[r->floor[y][x - 1]]) s |= 2;
            }
            r->shade[y][x] = s;
        }
}

static void build_room(Room *r)
{
    WORD x, y, d, c, tries;
    for (tries = 0; tries < 40; tries++) {
        WORD pattern = (tries > 25 || r->type == RT_START) ? 0 : RND(10);
        for (y = 0; y < ROOM_H; y++)
            for (x = 0; x < ROOM_W; x++) {
                sem[y][x] = (x == 0 || y == 0 || x == ROOM_W - 1 || y == ROOM_H - 1) ? S_ROCK : S_FLOOR;
                r->obj[y][x] = 0;
            }
        if (r->doors & DOOR_S) sem[13][9] = sem[13][10] = S_FLOOR;
        if (r->doors & DOOR_W) sem[6][0] = sem[7][0] = S_FLOOR;
        if (r->doors & DOOR_E) sem[6][19] = sem[7][19] = S_FLOOR;

        interior(r, pattern, 0);
        /* rock must not block door approaches or the room centre */
        for (d = 0; d < 4; d++)
            if (r->doors & (1 << d))
                for (c = 0; c < 2; c++) sem[door_inner[d][c][1]][door_inner[d][c][0]] = S_FLOOR;
        if (r->type == RT_STAIRS || r->type == RT_CAPTIVE)
            for (y = 5; y <= 8; y++) for (x = 8; x <= 11; x++) sem[y][x] = S_FLOOR;

        finish_room(r);
        interior(r, pattern, 1);

        if (r->type == RT_STAIRS) r->obj[6][9] = TL_STAIRS, r->obj[6][10] = 0;
        /* spawn point and the two cells inside every doorway stay clear */
        for (y = 5; y <= 8; y++) for (x = 8; x <= 11; x++) if (r->obj[y][x] != TL_STAIRS) r->obj[y][x] = 0;
        for (d = 0; d < 4; d++)
            if (r->doors & (1 << d)) {
                static const BYTE in2x[4] = { 0, -1, 0, 1 }, in2y[4] = { 1, 0, -1, 0 };
                for (c = 0; c < 2; c++) {
                    WORD ix = door_inner[d][c][0], iy = door_inner[d][c][1];
                    r->obj[iy][ix] = 0;
                    r->obj[iy + in2y[d]][ix + in2x[d]] = 0;
                }
            }

        {   /* chests */
            WORD nchest = r->type == RT_TREASURE ? 2 + RND(2) : (RND(100) < 18 ? 1 : 0);
            for (c = 0; c < nchest; c++)
                for (d = 0; d < 20; d++) {
                    x = 2 + RND(16); y = 3 + RND(9);
                    if (!r->obj[y][x] && !solid_tab[r->floor[y][x]]) {
                        UBYTE t = TL_CHEST;
                        if (r->type == RT_TREASURE && c == 0) t = TL_BIGCHEST;
                        else if (hero.depth >= 3 && RND(100) < 20) t = TL_MIMIC;
                        r->obj[y][x] = t;
                        break;
                    }
                }
        }
        if (room_ok(r)) break;
    }
    compute_shade(r);
}

void floor_generate(WORD depth)
{
    static const BYTE gdx[4] = { 0, 1, 0, -1 }, gdy[4] = { -1, 0, 1, 0 };
    WORD x, y, i, n, target, guard;
    WORD order[MAX_ROOMS];

    if (!solid_tab[TL_WALL]) { init_solid(); solid_tab[TL_WALL] = 1; }
    gfx_set_biome(((depth - 1) / 3) % NUM_BIOMES);
    grid_w = depth <= 2 ? 3 : 4;
    grid_h = depth <= 4 ? 3 : 4;
    target = MIN(grid_w * grid_h, 4 + depth);

    for (y = 0; y < GRID_MAX; y++)
        for (x = 0; x < GRID_MAX; x++) grid[y][x] = -1;
    for (i = 0; i < MAX_ROOMS; i++) {
        BYTE *p = (BYTE *)&rooms[i];
        LONG k;
        for (k = 0; k < (LONG)sizeof(Room); k++) p[k] = 0;
    }

    nrooms = 1;
    rooms[0].exists = 1;
    rooms[0].gx = (UBYTE)RND(grid_w);
    rooms[0].gy = (UBYTE)RND(grid_h);
    rooms[0].type = RT_START;
    grid[rooms[0].gy][rooms[0].gx] = 0;

    guard = 0;
    while (nrooms < target && guard++ < 2000) {
        WORD r = RND(nrooms), d = RND(4);
        WORD nx = rooms[r].gx + gdx[d], ny = rooms[r].gy + gdy[d];
        if (nx < 0 || ny < 0 || nx >= grid_w || ny >= grid_h || grid[ny][nx] >= 0) continue;
        grid[ny][nx] = nrooms;
        rooms[nrooms].exists = 1;
        rooms[nrooms].gx = (UBYTE)nx;
        rooms[nrooms].gy = (UBYTE)ny;
        rooms[r].doors |= 1 << d;
        rooms[nrooms].doors |= 1 << ((d + 2) & 3);
        nrooms++;
    }
    for (i = 0; i < 1 + depth / 4; i++) {          /* a few loops */
        WORD r = RND(nrooms), d = RND(4);
        WORD nx = rooms[r].gx + gdx[d], ny = rooms[r].gy + gdy[d];
        if (nx < 0 || ny < 0 || nx >= grid_w || ny >= grid_h || grid[ny][nx] < 0) continue;
        rooms[r].doors |= 1 << d;
        rooms[grid[ny][nx]].doors |= 1 << ((d + 2) & 3);
    }

    for (i = 0; i < nrooms; i++) rooms[i].dist = 255;
    rooms[0].dist = 0;
    order[0] = 0;
    n = 1;
    for (i = 0; i < n; i++) {
        WORD r = order[i], d;
        for (d = 0; d < 4; d++)
            if (rooms[r].doors & (1 << d)) {
                WORD o = grid[rooms[r].gy + gdy[d]][rooms[r].gx + gdx[d]];
                if (rooms[o].dist == 255) { rooms[o].dist = rooms[r].dist + 1; order[n++] = o; }
            }
    }
    {
        WORD far = order[n - 1];
        rooms[far].type = RT_STAIRS;
        if (depth % 5 == 0) rooms[far].boss = 1;
    }
    {
        WORD cands[MAX_ROOMS], nc = 0;
        for (i = 1; i < nrooms; i++) if (rooms[i].type == RT_NORMAL) cands[nc++] = i;
        if (nc > 0 && party_size() < MAX_ALLIES && (depth == 1 || depth == 3 || RND(100) < 35)) {
            WORD k = RND(nc), cls = 0, tries;
            rooms[cands[k]].type = RT_CAPTIVE;
            for (tries = 0; tries < 20; tries++) {
                BOOL have = 0;
                cls = RND(NUM_CLASSES);
                for (x = 0; x < MAX_ALLIES; x++)
                    if ((actors[ACT_ALLY0 + x].alive || actors[ACT_ALLY0 + x].ko) &&
                        actors[ACT_ALLY0 + x].cls == cls) have = 1;
                if (!have) break;
            }
            rooms[cands[k]].captive = (UBYTE)(cls + 1);
            cands[k] = cands[--nc];
        }
        if (nc > 0 && (depth % 2 == 0 || RND(100) < 50)) rooms[cands[RND(nc)]].type = RT_TREASURE;
    }
    for (i = 0; i < nrooms; i++) build_room(&rooms[i]);
    rooms[0].cleared = 1;
    cur_room = 0;
}

/* dungeon theme, transposed per biome */
void music_for_floor(void)
{
    static const BYTE tr[NUM_BIOMES] = { 0, -2, 3, -5, 1, -3 };
    music_play(SONG_DUNGEON, tr[((hero.depth - 1) / 3) % NUM_BIOMES]);
}

void room_lock(BOOL lock)
{
    Room *r = &rooms[cur_room];
    room_locked = (UBYTE)lock;
    if (r->doors & DOOR_N) {
        r->floor[1][9] = lock ? TL_DDOOR_L : TL_ARCH_L;
        r->floor[1][10] = lock ? TL_DDOOR_R : TL_ARCH_R;
        redraw_cell(9, 1); redraw_cell(10, 1);
    }
    if (r->doors & DOOR_S) { room_set_tile(9, 13, lock ? TL_DOOR : TL_FLOOR); room_set_tile(10, 13, lock ? TL_DOOR : TL_FLOOR); }
    if (r->doors & DOOR_W) { room_set_tile(0, 6, lock ? TL_DOOR : TL_FLOOR); room_set_tile(0, 7, lock ? TL_DOOR : TL_FLOOR); }
    if (r->doors & DOOR_E) { room_set_tile(19, 6, lock ? TL_DOOR : TL_FLOOR); room_set_tile(19, 7, lock ? TL_DOOR : TL_FLOOR); }
}

void room_draw(void)
{
    Room *r = &rooms[cur_room];
    WORD x, y;
    for (y = 0; y < ROOM_H; y++)
        for (x = 0; x < ROOM_W; x++) paint_cell(r, x, y);
    gfx_copy_area(bgbuf, frontbuf, 0, PLAY_H);
    gfx_copy_area(bgbuf, backbuf, 0, PLAY_H);
    gfx_reset_dirty();
}

/* tiny map in the HUD: 6x4 px per room */
void minimap_draw(void)
{
    WORD x, y;
    const WORD ox = 288, oy = 227;
    gfx_rect_all(ox - 2, oy - 1, 30, 20, COL_HUDBG);
    for (y = 0; y < grid_h; y++)
        for (x = 0; x < grid_w; x++) {
            WORD r = grid[y][x];
            UWORD col;
            if (r < 0 || !rooms[r].visited) {
                if (r >= 0) {
                    WORD d; BOOL nb = 0;
                    static const BYTE gdx[4] = { 0, 1, 0, -1 }, gdy[4] = { -1, 0, 1, 0 };
                    for (d = 0; d < 4; d++)
                        if (rooms[r].doors & (1 << d)) {
                            WORD o = grid[y + gdy[d]][x + gdx[d]];
                            if (o >= 0 && rooms[o].visited) nb = 1;
                        }
                    if (nb) gfx_rect_all(ox + x * 7, oy + y * 5, 6, 4, COL_HUDLINE);
                }
                continue;
            }
            col = r == cur_room ? COL_YELLOW : rooms[r].type == RT_STAIRS ? COL_GREEN : COL_GREY;
            gfx_rect_all(ox + x * 7, oy + y * 5, 6, 4, col);
        }
}
