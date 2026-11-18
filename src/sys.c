/* System takeover / restore, VBL interrupt, keyboard + joystick input. */
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include "hw.h"
#include "sys.h"

struct GfxBase *GfxBase;
extern struct ExecBase *SysBase;

volatile ULONG frame_count;
volatile UBYTE keys[128];

static struct View *old_view;
static UWORD old_dmacon, old_intena, old_adkcon;
static APTR *vbr;
static APTR old_level3;

/* movec vbr,d0 ; rte  -- executed in supervisor mode via Supervisor() */
__asm__(
    "    .text\n"
    "    .even\n"
    "    .globl _get_vbr_super\n"
    "_get_vbr_super:\n"
    "    movec %vbr,%d0\n"
    "    rte\n");
extern ULONG get_vbr_super(void);

static void keyboard_poll(void)
{
    UBYTE icr = CIAA_ICR;
    if (icr & 0x08) {                       /* serial data ready */
        UBYTE raw = CIAA_SDR;
        UBYTE code;
        UWORD line;
        CIAA_CRA |= 0x40;                    /* handshake: SP output */
        raw = ~raw;
        code = (UBYTE)((raw >> 1) | (raw << 7));
        if (code & 0x80)
            keys[code & 0x7f] = 0;
        else
            keys[code] = 1;
        /* hold handshake for >= 85us (about 2 scanlines) */
        line = REG16(VHPOSR) & 0xff00;
        while ((REG16(VHPOSR) & 0xff00) == line) ;
        line = REG16(VHPOSR) & 0xff00;
        while ((REG16(VHPOSR) & 0xff00) == line) ;
        CIAA_CRA &= ~0x40;
    }
}

static __attribute__((interrupt)) void level3_handler(void)
{
    if (REG16(INTREQR) & INTF_VERTB) {
        frame_count++;
        gfx_vbl();
        sound_vbl();
        keyboard_poll();
        REG16(INTREQ) = INTF_VERTB;
        REG16(INTREQ) = INTF_VERTB;
    }
}

int sys_takeover(void)
{
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!GfxBase)
        return 0;                            /* needs Kickstart 3.0+ (AGA) */
    if ((REG16(0x07c) & 0xff) != 0xf8) {     /* DENISEID: 0xf8 = Lisa (AGA) */
        UBYTE bits = GfxBase->ChipRevBits0;
        CloseLibrary((struct Library *)GfxBase);
        return 10 + bits;                    /* not an AGA machine */
    }

    vbr = 0;
    if (SysBase->AttnFlags & AFF_68010)
        vbr = (APTR *)Supervisor((ULONG (*)())get_vbr_super);

    old_view = GfxBase->ActiView;
    LoadView(NULL);
    WaitTOF();
    WaitTOF();
    OwnBlitter();
    WaitBlit();
    Forbid();

    old_dmacon = REG16(DMACONR);
    old_intena = REG16(INTENAR);
    old_adkcon = REG16(ADKCONR);

    REG16(INTENA) = 0x7fff;
    REG16(INTREQ) = 0x7fff;
    REG16(INTREQ) = 0x7fff;
    REG16(DMACON) = 0x7fff;

    old_level3 = vbr[0x6c / 4];
    vbr[0x6c / 4] = (APTR)level3_handler;

    REG16(POTGO) = 0xff00;                   /* allow reading fire button 2 */
    REG16(INTENA) = INTF_SETCLR | INTF_INTEN | INTF_VERTB;
    return 1;
}

void sys_restore(void)
{
    wait_blit();
    REG16(INTENA) = 0x7fff;
    REG16(INTREQ) = 0x7fff;
    REG16(DMACON) = 0x7fff;
    REG16(ADKCON) = 0x7fff;
    REG16(FMODE) = 0;
    REG16(BPLCON3) = 0x0c00;
    REG16(BPLCON4) = 0x0011;

    vbr[0x6c / 4] = old_level3;

    REG32(COP1LC) = (ULONG)GfxBase->copinit;
    REG16(COPJMP1) = 0;

    REG16(ADKCON) = 0x8000 | old_adkcon;
    REG16(DMACON) = 0x8000 | old_dmacon;
    REG16(INTENA) = 0x8000 | old_intena;

    Permit();
    DisownBlitter();
    LoadView(old_view);
    WaitTOF();
    WaitTOF();
    CloseLibrary((struct Library *)GfxBase);
}

void wait_vbl(void)
{
    ULONG f = frame_count;
    while (frame_count == f) ;
}

UWORD input_read(void)
{
    UWORD in = 0;
    UWORD j = REG16(JOY1DAT);
    if (j & 0x0002) in |= IN_RIGHT;
    if (j & 0x0200) in |= IN_LEFT;
    if (((j >> 1) ^ j) & 0x0001) in |= IN_DOWN;
    if (((j >> 1) ^ j) & 0x0100) in |= IN_UP;
    if (!(CIAA_PRA & 0x80)) in |= IN_FIRE;
    if (!(REG16(POTGOR) & 0x4000)) in |= IN_FIRE2;

    if (keys[KEY_UP] || keys[KEY_W]) in |= IN_UP;
    if (keys[KEY_DOWN] || keys[KEY_S]) in |= IN_DOWN;
    if (keys[KEY_LEFT] || keys[KEY_A]) in |= IN_LEFT;
    if (keys[KEY_RIGHT] || keys[KEY_D]) in |= IN_RIGHT;
    if (keys[KEY_SPACE] || keys[KEY_CTRL] || keys[KEY_RETURN]) in |= IN_FIRE;
    if (keys[KEY_Q] || keys[KEY_E] || keys[KEY_LALT]) in |= IN_FIRE2;
    if (keys[KEY_ESC]) in |= IN_ESC;
    if (keys[KEY_P]) in |= IN_PAUSE;
    if (keys[KEY_M]) in |= IN_MUSIC;
    return in;
}

static ULONG rstate = 0x12345678;
void rnd_seed(ULONG s) { rstate = s ? s : 0x9e3779b9; }
ULONG rnd(void)
{
    ULONG x = rstate;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rstate = x;
}
