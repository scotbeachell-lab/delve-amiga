/* Paula audio.
 * Channels 0-1: music sequencer (lead + bass), looping single-cycle waveforms with
 *               volume envelopes and vibrato, driven from the VBL interrupt.
 * Channels 2-3: one-shot sound effects and voice (all four when music is off).
 * Samples are 8-bit signed, 11025 Hz. */
#include "hw.h"
#include "sys.h"
#include "sound.h"
#include "assets.h"

#define PERIOD 322                       /* 3546895 / 11025 (PAL) */

static __chip UWORD silence[2] = { 0, 0 };

typedef struct {
    UWORD timer;      /* frames until free           */
    UBYTE quiet;      /* frames until silence loop   */
    UBYTE prio;
} Chan;
static Chan chan[4];
static UBYTE next_ch;
UBYTE sound_on = 1;
UBYTE music_on = 1;

#define AUD(ch, off) (0x0a0 + (ch) * 0x10 + (off))

/* ------------------------------------------------------------------ music */
typedef struct {
    UWORD period;      /* base period of the current note */
    WORD vol, peak, sustain;
    UWORD age;         /* ticks since note on             */
    UBYTE wave, short_wave, on, pad;
} Voice;
static const SongDef *song;
static WORD song_step, song_tick, transpose;
static Voice voice[2];
static volatile UBYTE music_playing;

static void voice_set_wave(UWORD ch, Voice *v, UBYTE shortw)
{
    const BYTE *w = waves[v->wave];
    if (shortw) { REG32(AUD(ch, 0)) = (ULONG)(w + 32); REG16(AUD(ch, AUDLEN)) = 8; }
    else { REG32(AUD(ch, 0)) = (ULONG)w; REG16(AUD(ch, AUDLEN)) = 16; }
    v->short_wave = shortw;
}

static void note_on(UWORD ch, UBYTE note)
{
    Voice *v = &voice[ch];
    WORD n = note + transpose;
    if (n < 24) n += 12;
    if (n > 100) n -= 12;
    if (note_short[n] != v->short_wave) voice_set_wave(ch, v, note_short[n]);
    v->period = note_period[n];
    v->vol = v->peak;
    v->age = 0;
    v->on = 1;
    REG16(AUD(ch, AUDPER)) = v->period;
}

static void music_tick(void)
{
    UWORD ch;
    if (!music_playing) return;
    if (++song_tick >= (WORD)song->tempo) {
        song_tick = 0;
        if (++song_step >= (WORD)song->len) song_step = 0;
        for (ch = 0; ch < 2; ch++) {
            UBYTE n = (ch ? song->bass : song->lead)[song_step];
            if (n >= 12) note_on(ch, n);
            else if (n == 1) voice[ch].on = 0;
        }
    }
    for (ch = 0; ch < 2; ch++) {
        Voice *v = &voice[ch];
        WORD out;
        v->age++;
        if (!v->on) { if (v->vol > 0) v->vol -= 4; }
        else if (v->vol > v->sustain && (v->age & 1)) v->vol--;
        if (v->vol < 0) v->vol = 0;
        out = music_on ? v->vol : 0;
        REG16(AUD(ch, AUDVOL)) = (UWORD)out;
        if (ch == 0 && v->on && v->age > 10) {      /* lead vibrato */
            WORD d = (WORD)(v->period >> 7);
            REG16(AUD(ch, AUDPER)) = v->period + ((v->age & 4) ? d : -d);
        }
    }
}

void music_play(UWORD id, WORD semis)
{
    UWORD ch;
    const SongDef *s = &songs[id];
    if (song == s && transpose == semis && music_playing) return;
    music_playing = 0;
    song = s;
    transpose = semis;
    song_step = -1;
    song_tick = s->tempo;
    for (ch = 0; ch < 2; ch++) {
        Voice *v = &voice[ch];
        v->wave = ch ? s->bass_wave : s->lead_wave;
        v->peak = ch ? 46 : 38;
        v->sustain = ch ? 30 : 24;
        v->vol = 0; v->on = 0;
        REG16(AUD(ch, AUDVOL)) = 0;
        voice_set_wave(ch, v, 0);
        REG16(AUD(ch, AUDPER)) = 400;
    }
    REG16(DMACON) = DMAF_SETCLR | DMAF_MASTER | 0x0003;
    music_playing = 1;
}

void music_stop(void)
{
    music_playing = 0;
    REG16(AUD(0, AUDVOL)) = 0;
    REG16(AUD(1, AUDVOL)) = 0;
}

void music_toggle(void)
{
    music_on ^= 1;
}

/* ------------------------------------------------------------------ effects */
void sound_init(void)
{
    UWORD i;
    REG16(DMACON) = DMAF_AUDIO;
    for (i = 0; i < 4; i++) {
        REG32(AUD(i, 0)) = (ULONG)silence;
        REG16(AUD(i, AUDLEN)) = 1;
        REG16(AUD(i, AUDPER)) = PERIOD;
        REG16(AUD(i, AUDVOL)) = 0;
        chan[i].timer = 0;
    }
}

void sound_stop(void)
{
    music_playing = 0;
    REG16(DMACON) = DMAF_AUDIO;
}

void sound_vbl(void)
{
    UWORD i;
    for (i = 2; i < 4; i++) {
        Chan *c = &chan[i];
        if (c->quiet && !--c->quiet) {
            /* queue the silent loop: takes over once the sample finishes */
            REG32(AUD(i, 0)) = (ULONG)silence;
            REG16(AUD(i, AUDLEN)) = 1;
        }
        if (c->timer) c->timer--;
    }
    music_tick();
}

void sound_play(UWORD id, UWORD prio)
{
    const SfxDef *s = &sfx_table[id];
    UWORD ch, i, line;
    if (!sound_on) return;

    /* channels 2/3: free one, else the lowest priority one */
    ch = 0xffff;
    for (i = 0; i < 2; i++)
        if (!chan[2 + ((next_ch + i) & 1)].timer) { ch = 2 + ((next_ch + i) & 1); break; }
    if (ch == 0xffff) {
        UWORD best = 0xffff;
        for (i = 0; i < 2; i++) {
            UWORD c = 2 + ((next_ch + i) & 1);
            if (chan[c].prio <= prio && chan[c].prio < best) { best = chan[c].prio; ch = c; }
        }
        if (ch == 0xffff) return;
    }
    next_ch = (UBYTE)((ch + 1) & 1);

    REG16(INTENA) = INTF_INTEN;              /* keep VBL out while we poke */
    REG16(DMACON) = 1 << ch;
    line = REG16(VHPOSR) & 0xff00;           /* let DMA stop (>= 1 line) */
    while ((REG16(VHPOSR) & 0xff00) == line) ;
    line = REG16(VHPOSR) & 0xff00;
    while ((REG16(VHPOSR) & 0xff00) == line) ;
    REG32(AUD(ch, 0)) = (ULONG)s->data;
    REG16(AUD(ch, AUDLEN)) = s->words;
    REG16(AUD(ch, AUDPER)) = PERIOD;
    REG16(AUD(ch, AUDVOL)) = 64;
    REG16(DMACON) = DMAF_SETCLR | DMAF_MASTER | (1 << ch);
    chan[ch].quiet = 2;
    chan[ch].timer = (UWORD)((ULONG)s->words * 100 / 11025) + 1;
    chan[ch].prio = (UBYTE)prio;
    REG16(INTENA) = INTF_SETCLR | INTF_INTEN;
}
