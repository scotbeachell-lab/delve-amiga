/* AGA display: 320x256, 8 interleaved bitplanes, 4x fetch mode, 24-bit palette.
 * Copper list: bitplane pointers, display setup, 256-colour palette, then a
 * per-scanline gradient for the panel colour and a bitplane-pointer reload at the
 * HUD line (so the playfield can shake while the HUD stays put). */
#include <exec/memory.h>
#include <proto/exec.h>
#include "hw.h"
#include "sys.h"
#include "gfx.h"
#include "assets.h"

UBYTE *bgbuf, *backbuf, *frontbuf;
static UBYTE *screens[2];
static UBYTE *chipmem;
static ULONG chipsize;
static UWORD *copper;
static UWORD cop_bplptr;       /* top-of-frame BPLxPT moves                  */
static UWORD cop_hudptr;       /* BPLxPT reload at the HUD line              */
static UWORD cop_pal;          /* palette                                    */
static UWORD cop_grad;         /* first gradient line                        */
static volatile UBYTE swap_pending;
static UWORD cur;              /* index of back buffer                       */
static volatile WORD shake_t, shake_amp;

#define COP_WORDS 4200
#define GRAD_WORDS 10          /* words per gradient line: WAIT + 4 MOVEs    */
#ifndef DDFSTOP_VAL
#define DDFSTOP_VAL 0x00b8     /* 4x fetch lores: 5 blocks of 64px           */
#endif

static ULONG cur_pal[128];
static WORD fade_level = 16;

/* ------------------------------------------------------------------ copper */
static void set_ptrs(UWORD at, UBYTE *buf)
{
    UWORD i, *c = copper + at;
    for (i = 0; i < PLANES; i++) {
        ULONG p = (ULONG)(buf + i * ROWB);
        c[i * 4 + 1] = (UWORD)(p >> 16);
        c[i * 4 + 3] = (UWORD)p;
    }
}

static void split_rgb(ULONG rgb, WORD level, UWORD *hi, UWORD *lo)
{
    UWORD r = (UWORD)((rgb >> 16) & 0xff) * level / 16;
    UWORD g = (UWORD)((rgb >> 8) & 0xff) * level / 16;
    UWORD b = (UWORD)(rgb & 0xff) * level / 16;
    *hi = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
    *lo = ((r & 15) << 8) | ((g & 15) << 4) | (b & 15);
}

/* shadow bank: darker, slightly cooler */
static ULONG shade(ULONG rgb)
{
    UWORD r = (UWORD)((rgb >> 16) & 0xff), g = (UWORD)((rgb >> 8) & 0xff), b = (UWORD)(rgb & 0xff);
    r = r * 9 / 20; g = g * 9 / 20; b = b * 11 / 20 + 4;
    return ((ULONG)r << 16) | ((ULONG)g << 8) | b;
}

static void write_color(UWORD i)
{
    UWORD *c = copper + cop_pal;
    ULONG rgb = i < 128 ? cur_pal[i] : shade(cur_pal[i - 128]);
    UWORD bank = i >> 5, idx = i & 31;
    split_rgb(rgb, fade_level, &c[bank * 132 + 2 + idx * 2 + 1], &c[bank * 132 + 66 + 2 + idx * 2 + 1]);
}

static void write_palette(void)
{
    UWORD i;
    for (i = 0; i < 256; i++) write_color(i);
}

/* panel colour gradient, one entry per scanline */
static ULONG grad_rgb(WORD y)
{
    WORD r, g, b, t;
    if (y >= PLAY_H) {                  /* HUD: lighter top edge fading down */
        t = y - PLAY_H;
        r = 58 - t; g = 50 - t; b = 92 - t;
    } else {                            /* menus: deep blue -> purple -> dark */
        t = y;
        r = 34 + t / 6; g = 26 + t / 16; b = 70 - t / 10;
        if (y > 160) { r -= (y - 160) / 3; g -= (y - 160) / 5; b -= (y - 160) / 4; }
    }
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    return ((ULONG)r << 16) | ((ULONG)g << 8) | b;
}

static void write_gradient(void)
{
    WORD y;
    for (y = 0; y < SCR_H; y++) {
        UWORD *c = copper + cop_grad + y * GRAD_WORDS + (y >= 256 - 0x2c ? 2 : 0) + (y >= PLAY_H ? PLANES * 4 + 2 : 0);
        split_rgb(grad_rgb(y), fade_level, &c[5], &c[9]);
    }
}

static void build_copper(void)
{
    UWORD *c = copper, i, bank;
    WORD y;
    UWORD gb = COL_HUDBG >> 5, gi = COL_HUDBG & 31;
#define MOVE(reg, val) do { *c++ = (reg); *c++ = (val); } while (0)
    cop_bplptr = (UWORD)(c - copper);
    for (i = 0; i < PLANES; i++) {
        MOVE(BPL1PTH + i * 4, 0);
        MOVE(BPL1PTH + i * 4 + 2, 0);
    }
    MOVE(FMODE, 0x0003);
    MOVE(BPLCON0, 0x0211);        /* 8 planes (BPU3), colour, ECSENA */
    MOVE(BPLCON1, 0x0000);
    MOVE(BPLCON2, 0x0000);
    MOVE(BPLCON4, 0x0011);
    MOVE(DIWSTRT, 0x2c81);
    MOVE(DIWSTOP, 0x2cc1);
    MOVE(DDFSTRT, 0x0038);
    MOVE(DDFSTOP, DDFSTOP_VAL);
    MOVE(BPL1MOD, LINEB - ROWB);
    MOVE(BPL2MOD, LINEB - ROWB);
    for (i = 0; i < 8; i++) {     /* disarm all sprites */
        MOVE(0x142 + i * 8, 0);
        MOVE(0x144 + i * 8, 0);
        MOVE(0x146 + i * 8, 0);
    }
    cop_pal = (UWORD)(c - copper);
    for (bank = 0; bank < 8; bank++) {
        MOVE(BPLCON3, (bank << 13) | 0x0c00);
        for (i = 0; i < 32; i++) MOVE(COLOR00 + i * 2, 0);
        MOVE(BPLCON3, (bank << 13) | 0x0e00);   /* LOCT: low nibbles */
        for (i = 0; i < 32; i++) MOVE(COLOR00 + i * 2, 0);
    }
    cop_grad = (UWORD)(c - copper);
    for (y = 0; y < SCR_H; y++) {
        WORD line = 0x2c + y;
        if (line == 256) { *c++ = 0xffdf; *c++ = 0xfffe; }   /* past line 255 */
        if (y == PLAY_H) {
            /* end of the previous line, after its last bitplane fetch */
            *c++ = (UWORD)((((line - 1) & 0xff) << 8) | 0xdf);
            *c++ = 0xfffe;
            cop_hudptr = (UWORD)(c - copper);
            for (i = 0; i < PLANES; i++) {
                MOVE(BPL1PTH + i * 4, 0);
                MOVE(BPL1PTH + i * 4 + 2, 0);
            }
        }
        *c++ = (UWORD)(((line & 0xff) << 8) | 0x07);
        *c++ = 0xfffe;
        MOVE(BPLCON3, (gb << 13) | 0x0c00);
        MOVE(COLOR00 + gi * 2, 0);
        MOVE(BPLCON3, (gb << 13) | 0x0e00);
        MOVE(COLOR00 + gi * 2, 0);
    }
    MOVE(BPLCON3, 0x0c00);
    *c++ = 0xffff;
    *c++ = 0xfffe;
#undef MOVE
}

static void show_buffer(UBYTE *buf, WORD dy)
{
    UWORD i;
    set_ptrs(cop_bplptr, buf + (LONG)dy * LINEB);
    set_ptrs(cop_hudptr, buf + (LONG)PLAY_H * LINEB);
    for (i = 0; i < PLANES; i++)
        REG32(BPL1PTH + i * 4) = (ULONG)(buf + (LONG)dy * LINEB + i * ROWB);
}

int gfx_init(void)
{
    UWORD i;
    chipsize = BUFSIZE * 3 + 16 + COP_WORDS * 2;
    chipmem = AllocMem(chipsize, MEMF_CHIP | MEMF_CLEAR);
    if (!chipmem)
        return 0;
    {
        UBYTE *p = (UBYTE *)(((ULONG)chipmem + 7) & ~7UL);   /* 64-bit align */
        screens[0] = p;
        screens[1] = p + BUFSIZE;
        bgbuf = p + BUFSIZE * 2;
        copper = (UWORD *)(p + BUFSIZE * 3);
    }
    for (i = 0; i < 128; i++) cur_pal[i] = pal_rgb[i];
    cur = 1;
    frontbuf = screens[0];
    backbuf = screens[1];
    build_copper();
    write_palette();
    write_gradient();
    set_ptrs(cop_bplptr, frontbuf);
    set_ptrs(cop_hudptr, frontbuf + (LONG)PLAY_H * LINEB);
    return 1;
}

void gfx_free(void)
{
    if (chipmem)
        FreeMem(chipmem, chipsize);
    chipmem = 0;
}

void gfx_start_display(void)
{
    REG32(COP1LC) = (ULONG)copper;
    REG16(COPJMP1) = 0;
    REG16(DMACON) = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER;
}

/* called from VBL interrupt */
void gfx_vbl(void)
{
    static UWORD flame_t;
    WORD dy = 0;
    if (shake_t > 0) {
        shake_t--;
        dy = (shake_t & 1) ? shake_amp : 0;
        if (shake_t == 0) dy = 0;
        swap_pending |= 2;               /* force pointer refresh */
    }
    if (swap_pending) {
        show_buffer(frontbuf, dy);
        swap_pending = 0;
    }
    /* torch flicker: rotate the three flame colours */
    if (++flame_t >= 5) {
        ULONG t = cur_pal[COL_FLAME0];
        flame_t = 0;
        if (frame_count & 32) {
            cur_pal[COL_FLAME0] = cur_pal[COL_FLAME0 + 1];
            cur_pal[COL_FLAME0 + 1] = cur_pal[COL_FLAME0 + 2];
            cur_pal[COL_FLAME0 + 2] = t;
        } else {
            cur_pal[COL_FLAME0] = cur_pal[COL_FLAME0 + 2];
            cur_pal[COL_FLAME0 + 2] = cur_pal[COL_FLAME0 + 1];
            cur_pal[COL_FLAME0 + 1] = t;
        }
        write_color(COL_FLAME0); write_color(COL_FLAME0 + 1); write_color(COL_FLAME0 + 2);
        write_color(COL_FLAME0 + 128); write_color(COL_FLAME0 + 129); write_color(COL_FLAME0 + 130);
    }
}

void gfx_shake(WORD frames, WORD amp)
{
    if (frames > shake_t) shake_t = frames;
    shake_amp = amp;
}

void gfx_swap(void)
{
    wait_blit();
    frontbuf = backbuf;
    cur ^= 1;
    backbuf = screens[cur];
    swap_pending = 1;
    while (swap_pending) ;
}

void gfx_fade(WORD level)
{
    fade_level = level;
    write_palette();
    write_gradient();
}

void gfx_set_biome(WORD b)
{
    WORD i;
    for (i = 0; i < ENV_COLS; i++) cur_pal[ENV_START + i] = biome_pal[b][i];
    write_palette();
}

/* single-plane shadow: sets bitplane 7 under a 16x16 1-plane mask (darkens) */
void gfx_shadow(UBYTE *buf, WORD x, WORD y, UWORD sh)
{
    UWORD shift = x & 15;
    UBYTE *d = buf + (LONG)y * LINEB + 7 * ROWB + ((x >> 4) << 1);
    WORD h = 16;
    if (y + h > PLAY_H) h = PLAY_H - y;
    if (h <= 0) return;
    wait_blit();
    REG16(BLTCON0) = (shift << 12) | 0x0bfa;      /* A,C,D : D = A | C */
    REG16(BLTCON1) = 0;
    REG16(BLTAFWM) = 0xffff;
    REG16(BLTALWM) = 0x0000;
    REG16(BLTAMOD) = (UWORD)-2;
    REG16(BLTCMOD) = LINEB - 4;
    REG16(BLTDMOD) = LINEB - 4;
    REG32(BLTAPT) = (ULONG)(shadow_masks + sh * 16);
    REG32(BLTCPT) = (ULONG)d;
    REG32(BLTDPT) = (ULONG)d;
    REG16(BLTSIZE) = (h << 6) | 2;
}

/* ------------------------------------------------------------------ blits */
void gfx_tile(UBYTE *buf, WORD x, WORD y, UWORD tile)
{
    wait_blit();
    REG16(BLTCON0) = 0x09f0;
    REG16(BLTCON1) = 0;
    REG16(BLTAFWM) = 0xffff;
    REG16(BLTALWM) = 0xffff;
    REG16(BLTAMOD) = 0;
    REG16(BLTDMOD) = ROWB - 2;
    REG32(BLTAPT) = (ULONG)(tile_gfx + tile * TILE_WORDS);
    REG32(BLTDPT) = (ULONG)(buf + y * LINEB + (x >> 3));
    REG16(BLTSIZE) = ((16 * PLANES) << 6) | 1;
}

static const UWORD *variant_gfx(UWORD v)
{
    return v == VAR_WHITE ? tile_gfx_white : v == VAR_RED ? tile_gfx_red : tile_gfx;
}

void gfx_stamp(UBYTE *buf, WORD x, WORD y, UWORD tile, UWORD variant)
{
    UWORD shift = x & 15;
    wait_blit();
    REG16(BLTCON0) = (shift << 12) | 0x0fca;
    REG16(BLTCON1) = shift << 12;
    REG16(BLTAFWM) = 0xffff;
    REG16(BLTALWM) = 0x0000;
    REG16(BLTAMOD) = (UWORD)-2;
    REG16(BLTBMOD) = (UWORD)-2;
    REG16(BLTCMOD) = ROWB - 4;
    REG16(BLTDMOD) = ROWB - 4;
    REG32(BLTAPT) = (ULONG)(tile_mask + tile * TILE_WORDS);
    REG32(BLTBPT) = (ULONG)(variant_gfx(variant) + tile * TILE_WORDS);
    {
        UBYTE *d = buf + y * LINEB + ((x >> 4) << 1);
        REG32(BLTCPT) = (ULONG)d;
        REG32(BLTDPT) = (ULONG)d;
    }
    REG16(BLTSIZE) = ((16 * PLANES) << 6) | 2;
}

static void restore_rect(UBYTE *dst, WORD x, WORD y)
{
    ULONG off = (ULONG)y * LINEB + ((x >> 4) << 1);
    wait_blit();
    REG16(BLTCON0) = 0x09f0;
    REG16(BLTCON1) = 0;
    REG16(BLTAFWM) = 0xffff;
    REG16(BLTALWM) = 0xffff;
    REG16(BLTAMOD) = ROWB - 4;
    REG16(BLTDMOD) = ROWB - 4;
    REG32(BLTAPT) = (ULONG)(bgbuf + off);
    REG32(BLTDPT) = (ULONG)(dst + off);
    REG16(BLTSIZE) = ((16 * PLANES) << 6) | 2;
}

/* ------------------------------------------------------------- dirty list */
#define MAXBOBS 64
typedef struct { WORD x, y; } Dirty;
static Dirty dirty[2][MAXBOBS];
static UWORD ndirty[2];

void gfx_reset_dirty(void)
{
    ndirty[0] = ndirty[1] = 0;
}

void bobs_begin(void)
{
    UWORD i, n = ndirty[cur];
    Dirty *d = dirty[cur];
    for (i = 0; i < n; i++)
        restore_rect(backbuf, d[i].x, d[i].y);
    ndirty[cur] = 0;
}

void bob_draw(WORD x, WORD y, UWORD tile, UWORD variant)
{
    UWORD n = ndirty[cur];
    if (n >= MAXBOBS) return;
    if (x < 0) x = 0;
    if (x > SCR_W - 16) x = SCR_W - 16;
    if (y < 0) y = 0;
    if (y > PLAY_H - 16) y = PLAY_H - 16;
    dirty[cur][n].x = x;
    dirty[cur][n].y = y;
    ndirty[cur] = n + 1;
    gfx_stamp(backbuf, x, y, tile, variant);
}

void bob_draw_shadowed(WORD x, WORD y, UWORD tile, UWORD variant)
{
    if (x < 0) x = 0;
    if (x > SCR_W - 16) x = SCR_W - 16;
    if (y < 0) y = 0;
    if (y > PLAY_H - 16) y = PLAY_H - 16;
    gfx_shadow(backbuf, x, y, SH_DROP);
    bob_draw(x, y, tile, variant);
}

/* ------------------------------------------------------------ area blits */
void gfx_copy_area(UBYTE *src, UBYTE *dst, WORD y0, WORD h)
{
    while (h > 0) {
        WORD n = h > 128 ? 128 : h;          /* 128 lines * 8 planes = 1024 rows */
        ULONG off = (ULONG)y0 * LINEB;
        wait_blit();
        REG16(BLTCON0) = 0x09f0;
        REG16(BLTCON1) = 0;
        REG16(BLTAFWM) = 0xffff;
        REG16(BLTALWM) = 0xffff;
        REG16(BLTAMOD) = 0;
        REG16(BLTDMOD) = 0;
        REG32(BLTAPT) = (ULONG)(src + off);
        REG32(BLTDPT) = (ULONG)(dst + off);
        REG16(BLTSIZE) = (((n * PLANES) & 1023) << 6) | (ROWB / 2);
        y0 += n;
        h -= n;
    }
    wait_blit();
}

void gfx_clear_area(UBYTE *dst, WORD y0, WORD h)
{
    while (h > 0) {
        WORD n = h > 128 ? 128 : h;
        wait_blit();
        REG16(BLTCON0) = 0x0100;
        REG16(BLTCON1) = 0;
        REG16(BLTDMOD) = 0;
        REG32(BLTDPT) = (ULONG)(dst + (ULONG)y0 * LINEB);
        REG16(BLTSIZE) = (((n * PLANES) & 1023) << 6) | (ROWB / 2);
        y0 += n;
        h -= n;
    }
    wait_blit();
}

/* ------------------------------------------------------------ CPU drawing */
/* solid rectangle via blitter: per plane, D = A(mask) ? value : C */
void gfx_rect(UBYTE *buf, WORD x, WORD y, WORD w, WORD h, UWORD col)
{
    WORD w0, w1, words, p;
    UWORD fwm, lwm;
    UBYTE *base;
    if (w <= 0 || h <= 0) return;
    w0 = x >> 4;
    w1 = (x + w - 1) >> 4;
    words = w1 - w0 + 1;
    fwm = 0xffff >> (x & 15);
    lwm = 0xffff << (15 - ((x + w - 1) & 15));
    base = buf + (ULONG)y * LINEB + w0 * 2;
    for (p = 0; p < PLANES; p++) {
        wait_blit();
        REG16(BLTCON0) = 0x0300 | ((col & (1 << p)) ? 0xfa : 0x0a);   /* C,D: A|C or !A&C */
        REG16(BLTCON1) = 0;
        REG16(BLTAFWM) = fwm;
        REG16(BLTALWM) = lwm;
        REG16(BLTADAT) = 0xffff;
        REG16(BLTCMOD) = LINEB - words * 2;
        REG16(BLTDMOD) = LINEB - words * 2;
        REG32(BLTCPT) = (ULONG)(base + p * ROWB);
        REG32(BLTDPT) = (ULONG)(base + p * ROWB);
        REG16(BLTSIZE) = (h << 6) | words;
    }
    wait_blit();
}

/* text: x must be a multiple of 8. bg < 0 = transparent */
void gfx_text(UBYTE *buf, WORD x, WORD y, const char *s, UWORD fg, WORD bg)
{
    UBYTE *base = buf + (ULONG)y * LINEB + (x >> 3);
    wait_blit();
    while (*s) {
        UBYTE ch = (UBYTE)*s++;
        const UBYTE *g;
        WORD r, p;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 127) ch = '?';
        g = font8 + (ch - 32) * 8;
        for (r = 0; r < 8; r++) {
            UBYTE bits = g[r];
            UBYTE *d = base + r * LINEB;
            for (p = 0; p < PLANES; p++) {
                UBYTE v = d[p * ROWB];
                if (bg >= 0) {
                    v = 0;
                    if (bg & (1 << p)) v = (UBYTE)~bits;
                } else {
                    v &= ~bits;
                }
                if (fg & (1 << p)) v |= bits;
                d[p * ROWB] = v;
            }
        }
        base++;
        if (((ULONG)(base - buf) % LINEB) >= ROWB) break;   /* right edge */
    }
}

void gfx_text_all(WORD x, WORD y, const char *s, UWORD fg, WORD bg)
{
    gfx_text(screens[0], x, y, s, fg, bg);
    gfx_text(screens[1], x, y, s, fg, bg);
}

void gfx_rect_all(WORD x, WORD y, WORD w, WORD h, UWORD col)
{
    gfx_rect(screens[0], x, y, w, h, col);
    gfx_rect(screens[1], x, y, w, h, col);
}

/* copy one 16x16 cell (x multiple of 16) from background to both screens */
void gfx_copy_cell(WORD x, WORD y)
{
    UWORD s;
    ULONG off = (ULONG)y * LINEB + (x >> 3);
    for (s = 0; s < 2; s++) {
        wait_blit();
        REG16(BLTCON0) = 0x09f0;
        REG16(BLTCON1) = 0;
        REG16(BLTAFWM) = 0xffff;
        REG16(BLTALWM) = 0xffff;
        REG16(BLTAMOD) = ROWB - 2;
        REG16(BLTDMOD) = ROWB - 2;
        REG32(BLTAPT) = (ULONG)(bgbuf + off);
        REG32(BLTDPT) = (ULONG)(screens[s] + off);
        REG16(BLTSIZE) = ((16 * PLANES) << 6) | 1;
    }
}

void gfx_stamp_both(WORD x, WORD y, UWORD tile, UWORD variant)
{
    gfx_stamp(screens[0], x, y, tile, variant);
    gfx_stamp(screens[1], x, y, tile, variant);
    wait_blit();
}

/* double size text, x multiple of 8, into both screens (transparent bg) */
void gfx_text2x_all(WORD x, WORD y, const char *s, UWORD col)
{
    WORD scr;
    wait_blit();
    for (scr = 0; scr < 2; scr++) {
        const char *q = s;
        WORD cx = x;
        while (*q && cx <= SCR_W - 16) {
            UBYTE ch = (UBYTE)*q++;
            const UBYTE *g;
            WORD r, p, b;
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            if (ch < 32 || ch > 127) ch = '?';
            g = font8 + (ch - 32) * 8;
            for (r = 0; r < 16; r++) {
                UBYTE bits = g[r >> 1];
                UWORD w = 0;
                UBYTE *d = screens[scr] + (ULONG)(y + r) * LINEB + (cx >> 3);
                for (b = 0; b < 8; b++)
                    if (bits & (0x80 >> b)) w |= 0xc000 >> (b * 2);
                for (p = 0; p < PLANES; p++) {
                    UBYTE hi = (UBYTE)(w >> 8), lo = (UBYTE)w;
                    if (col & (1 << p)) { d[p * ROWB] |= hi; d[p * ROWB + 1] |= lo; }
                    else { d[p * ROWB] &= ~hi; d[p * ROWB + 1] &= ~lo; }
                }
            }
            cx += 16;
        }
    }
}

/* ------------------------------------------------------------ strings */
char *fmt_int(char *dst, LONG v)
{
    char tmp[12];
    WORD n = 0;
    char *d = dst;
    if (v < 0) { *d++ = '-'; v = -v; }
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *d++ = tmp[--n];
    *d = 0;
    return d;
}

WORD str_len(const char *s)
{
    WORD n = 0;
    while (*s++) n++;
    return n;
}
