/* Direct hardware access for DELVE (AGA, bare metal after system takeover) */
#ifndef HW_H
#define HW_H
#include <exec/types.h>

#define CUSTOM_BASE 0xdff000
#define REG16(o) (*(volatile UWORD *)(CUSTOM_BASE + (o)))
#define REG32(o) (*(volatile ULONG *)(CUSTOM_BASE + (o)))

/* custom register offsets */
#define DMACONR  0x002
#define VPOSR    0x004
#define VHPOSR   0x006
#define JOY0DAT  0x00a
#define JOY1DAT  0x00c
#define POTGOR   0x016
#define INTENAR  0x01c
#define INTREQR  0x01e
#define ADKCONR  0x010
#define BLTCON0  0x040
#define BLTCON1  0x042
#define BLTAFWM  0x044
#define BLTALWM  0x046
#define BLTCPT   0x048
#define BLTBPT   0x04c
#define BLTAPT   0x050
#define BLTDPT   0x054
#define BLTSIZE  0x058
#define BLTCMOD  0x060
#define BLTBMOD  0x062
#define BLTAMOD  0x064
#define BLTDMOD  0x066
#define BLTADAT  0x074
#define COP1LC   0x080
#define COPJMP1  0x088
#define DIWSTRT  0x08e
#define DIWSTOP  0x090
#define DDFSTRT  0x092
#define DDFSTOP  0x094
#define DMACON   0x096
#define INTENA   0x09a
#define INTREQ   0x09c
#define ADKCON   0x09e
#define AUD0LC   0x0a0
#define AUDLEN   0x004   /* + channel base */
#define AUDPER   0x006
#define AUDVOL   0x008
#define BPL1PTH  0x0e0
#define BPLCON0  0x100
#define BPLCON1  0x102
#define BPLCON2  0x104
#define BPLCON3  0x106
#define BPL1MOD  0x108
#define BPL2MOD  0x10a
#define BPLCON4  0x10c
#define SPR0PTH  0x120
#define SPR0DATA 0x144
#define COLOR00  0x180
#define FMODE    0x1fc
#define POTGO    0x034

/* DMACON bits */
#define DMAF_SETCLR  0x8000
#define DMAF_BBUSY   0x4000
#define DMAF_BLTPRI  0x0400
#define DMAF_MASTER  0x0200
#define DMAF_RASTER  0x0100
#define DMAF_COPPER  0x0080
#define DMAF_BLITTER 0x0040
#define DMAF_SPRITE  0x0020
#define DMAF_AUDIO   0x000f

/* INTENA bits */
#define INTF_SETCLR 0x8000
#define INTF_INTEN  0x4000
#define INTF_VERTB  0x0020

/* CIA-A */
#define CIAA_PRA (*(volatile UBYTE *)0xbfe001)
#define CIAA_SDR (*(volatile UBYTE *)0xbfec01)
#define CIAA_ICR (*(volatile UBYTE *)0xbfed01)
#define CIAA_CRA (*(volatile UBYTE *)0xbfee01)

static inline void wait_blit(void)
{
    (void)REG16(DMACONR);              /* A1000/Agnus first-read quirk */
    while (REG16(DMACONR) & DMAF_BBUSY)
        ;
}

#endif
