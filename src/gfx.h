#ifndef GFX_H
#define GFX_H
#include <exec/types.h>

#define SCR_W   320
#define SCR_H   256
#define ROWB    40                  /* bytes per plane row          */
#define PLANES  8
#define LINEB   (ROWB * PLANES)     /* bytes per interleaved line   */
#define BUFSIZE (LINEB * SCR_H)
#define PLAY_H  224                 /* playfield height (14 tiles)  */

/* bob variants */
#define VAR_NORMAL 0
#define VAR_WHITE  1
#define VAR_RED    2

extern UBYTE *bgbuf;       /* clean background (tiles + floor items)   */
extern UBYTE *backbuf;     /* buffer being drawn this frame            */
extern UBYTE *frontbuf;    /* buffer on screen                         */

int  gfx_init(void);
void gfx_free(void);
void gfx_start_display(void);

/* opaque 16x16 tile at pixel position (x multiple of 16) */
void gfx_tile(UBYTE *buf, WORD x, WORD y, UWORD tile);
/* transparent 16x16 tile at any pixel position, no dirty tracking */
void gfx_stamp(UBYTE *buf, WORD x, WORD y, UWORD tile, UWORD variant);

/* playfield moving objects: restore + draw with dirty tracking */
void bobs_begin(void);
void bob_draw(WORD x, WORD y, UWORD tile, UWORD variant);
void bob_draw_shadowed(WORD x, WORD y, UWORD tile, UWORD variant);   /* with drop shadow */

void gfx_copy_area(UBYTE *src, UBYTE *dst, WORD y0, WORD h);   /* whole lines */
void gfx_clear_area(UBYTE *dst, WORD y0, WORD h);
void gfx_rect(UBYTE *buf, WORD x, WORD y, WORD w, WORD h, UWORD col);
void gfx_text(UBYTE *buf, WORD x, WORD y, const char *s, UWORD fg, WORD bg);
void gfx_text_all(WORD x, WORD y, const char *s, UWORD fg, WORD bg);   /* both screens */
void gfx_rect_all(WORD x, WORD y, WORD w, WORD h, UWORD col);

void gfx_copy_cell(WORD x, WORD y);
void gfx_stamp_both(WORD x, WORD y, UWORD tile, UWORD variant);
void gfx_text2x_all(WORD x, WORD y, const char *s, UWORD col);

void gfx_swap(void);                /* show backbuf at next VBL, wait for it */
void gfx_fade(WORD level);           /* 0 = black .. 16 = full */
void gfx_set_biome(WORD b);          /* re-tint walls/floors */
void gfx_shake(WORD frames, WORD amp);
void gfx_shadow(UBYTE *buf, WORD x, WORD y, UWORD sh);
void gfx_reset_dirty(void);

/* text helpers */
char *fmt_int(char *dst, LONG v);
WORD str_len(const char *s);
#endif
