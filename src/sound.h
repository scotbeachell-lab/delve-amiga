#ifndef SOUND_H
#define SOUND_H
#include <exec/types.h>
extern UBYTE sound_on, music_on;
void sound_init(void);
void sound_stop(void);
void sound_play(UWORD id, UWORD prio);
void music_play(UWORD id, WORD transpose);   /* no-op if already playing */
void music_stop(void);
void music_toggle(void);
#endif
