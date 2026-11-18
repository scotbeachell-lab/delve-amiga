#!/usr/bin/env python3
"""Render DELVE's songs to build/music_preview.wav using the same waveforms, note
periods, tempos and envelopes as the Amiga player (approximate Paula emulation)."""
import os, sys, io, contextlib, runpy, wave, struct
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.argv = ['convert.py']
with contextlib.redirect_stdout(io.StringIO()):
    g = runpy.run_path(os.path.join(ROOT, 'tools', 'convert.py'))
SR, CLOCK, VBL = 22050, 3546895, 50
out = []
for name, L, B, tempo, lw, bw in g['song_data']:
    voices = [dict(wave=lw, peak=38, sus=24), dict(wave=bw, peak=46, sus=30)]
    for v in voices: v.update(per=400, vol=0, age=0, on=0, pos=0.0, short=0)
    steps = len(L) * 2 if name != 'BOSS' else len(L) * 3
    tick, step = tempo, -1
    for frame in range(steps * tempo):
        tick += 1
        if tick >= tempo:
            tick = 0; step = (step + 1) % len(L)
            for ch, v in enumerate(voices):
                n = (L, B)[ch][step]
                if n >= 12:
                    v['per'] = g['note_period'][n]; v['short'] = g['note_short'][n]
                    v['vol'] = v['peak']; v['age'] = 0; v['on'] = 1
                elif n == 1: v['on'] = 0
        for v in voices:
            v['age'] += 1
            if not v['on']: v['vol'] = max(0, v['vol'] - 4)
            elif v['vol'] > v['sus'] and v['age'] & 1: v['vol'] -= 1
        for _ in range(SR // VBL):
            s = 0.0
            for ch, v in enumerate(voices):
                w = g['WAVES'][v['wave']]
                n = 16 if v['short'] else 32
                base = 32 if v['short'] else 0
                per = v['per']
                if ch == 0 and v['on'] and v['age'] > 10:
                    per += (per >> 7) * (1 if v['age'] & 4 else -1)
                v['pos'] = (v['pos'] + CLOCK / per / SR) % n
                s += w[base + int(v['pos'])] * v['vol'] / 64.0
            out.append(int(max(-32767, min(32767, s * 120))))
    out += [0] * (SR // 2)
path = os.path.join(ROOT, 'build', 'music_preview.wav')
with wave.open(path, 'wb') as f:
    f.setnchannels(1); f.setsampwidth(2); f.setframerate(SR)
    f.writeframes(b''.join(struct.pack('<h', x) for x in out))
print(path, '%.1f s' % (len(out) / SR))
