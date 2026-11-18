#ifndef SYS_H
#define SYS_H
#include <exec/types.h>

/* raw key codes */
#define KEY_ESC   0x45
#define KEY_SPACE 0x40
#define KEY_RETURN 0x44
#define KEY_UP    0x4c
#define KEY_DOWN  0x4d
#define KEY_RIGHT 0x4e
#define KEY_LEFT  0x4f
#define KEY_W     0x11
#define KEY_A     0x20
#define KEY_S     0x21
#define KEY_D     0x22
#define KEY_Q     0x10
#define KEY_E     0x12
#define KEY_P     0x19
#define KEY_LALT  0x64
#define KEY_CTRL  0x63
#define KEY_M     0x37

/* logical input bits */
#define IN_UP    0x01
#define IN_DOWN  0x02
#define IN_LEFT  0x04
#define IN_RIGHT 0x08
#define IN_FIRE  0x10
#define IN_FIRE2 0x20
#define IN_ESC   0x40
#define IN_PAUSE 0x80
#define IN_MUSIC 0x100

extern volatile ULONG frame_count;
extern volatile UBYTE keys[128];

int  sys_takeover(void);
void sys_restore(void);
UWORD input_read(void);          /* current held state */
void wait_vbl(void);
ULONG rnd(void);
void rnd_seed(ULONG s);
#define RND(n) ((WORD)(rnd() % (ULONG)(n)))

/* hook called from the VBL interrupt (graphics swap, sound) */
void gfx_vbl(void);
void sound_vbl(void);

#endif
