# DELVE — a real-time rogue-lite dungeon crawler for the Amiga 1200 (AGA)

Art, sounds and font: [Kenney](https://kenney.nl) (CC0) — *Tiny Dungeon*, *RPG Audio*,
*Impact Sounds*, *Interface Sounds*, *Music Jingles*, *Kenney Fonts*.

## Play

```
make run          # builds, then boots FS-UAE (A1200, Kickstart 3.1) straight into the game
```

| Action | Keyboard | Joystick (port 2) |
|---|---|---|
| Move | Arrows / WASD | Stick |
| Attack | Space / Ctrl / Return | Fire |
| Drink health potion | Q / E / Left Alt | Fire 2 |
| Pause | P | |
| Music on/off | M | |
| Quit | Esc | |

## The game

- **Floors** are a grid of flip-screen rooms (3×3 growing to 4×4). Entering a room
  with monsters slams the doors shut until every monster is dead. Find the trapdoor
  in the farthest room to go deeper.
- **Gets harder every floor**: monster HP +22% and damage +14% per floor, more monsters
  per room, nastier types unlock (rats & bats → spiders → ghosts that pass through walls,
  dagger-throwing bandits → fire crabs → cyclops → fireball warlocks). Red-tinted
  **champions** have double HP. Every 5th floor has a **boss** guarding the stairs.
  From floor 3, some chests are **mimics**.
- **Loot**: gold, health potions (belt of 9), vitality (+5 max HP), strength (+1 ATK),
  elixirs (heal + revive the whole party), shields (+1 armor) and 11 weapons, from
  dagger to flame sword, including spears with long reach and magic staffs that shoot.
  Better weapons equip automatically; worse ones are sold on the spot. Treasure rooms
  hold a great chest.
- **Companions (up to 3)**: rescue them from captive rooms or hire them at camp.
  - **Bruni**, dwarf warrior: tough melee tank
  - **Lyra**, archer: arrows from range, keeps her distance
  - **Zolt**, wizard: slow but strong magic orbs
  - **Maeve**, cleric: heals whoever is most hurt
  They follow you in formation, pick their own targets, earn XP and level up. Fallen
  companions are knocked out until revived (rest at camp, or an elixir).
- **Camp** between floors: rest/revive, sharpen your weapon, reinforce armor, buy potions,
  train each companion, hire a new one. Prices scale with depth.
- You level up from kills too (+4 max HP, attack bonus, full heal).

## Tech

- Bare-metal after a clean OS takeover (returns to AmigaDOS on quit).
- AGA: 320×256 PAL, 8 bitplanes (256 colours) interleaved, 4× fetch mode (FMODE=3,
  DDFSTOP=$B8), 24-bit palette written by the copper.
- Rooms are autotiled in Tiny Dungeon's 3/4 style: stone-capped rock masses, brick
  faces with torches, banners and statue niches, arched doors, furniture.
- Lighting through the palette: colours 128-255 are a darker copy of 0-127, so
  setting bitplane 7 shades any pixel. Wall shadows are baked into rooms, and every
  character casts a real-time drop shadow with one cheap single-plane blit.
- Six biomes (sandstone, crypts, moss, ice, inferno, void) change every 3 floors by
  re-tinting only the wall/floor palette entries.
- Torch flames flicker by palette cycling; the HUD and menu panels use a per-scanline
  copper gradient; the playfield shakes on big hits (bitplane-pointer offset, with a
  copper reload so the HUD stays still).
- Effects: hit sparks, death smoke, floating damage numbers, treasure twinkles, walk
  bounce, attack lunge, visible held weapons, white hit flashes, red champions.
- Blitter cookie-cut sprites with double buffering and dirty-rectangle restore;
  blitter rect fills for the HUD; 50 FPS in normal play.
- Paula audio: channels 0-1 run a small music sequencer (synthesized pulse /
  triangle / saw / soft single-cycle waves, volume envelopes, lead vibrato) playing
  four original tracks: title, camp, the dungeon theme (transposed per biome) and a
  driving boss theme. Channels 2-3 play the Kenney sound effects and voice callouts
  ("Prepare yourself", "Fight!", "Level up", "Power up", "Game over").
  Songs are written as note text in `tools/convert.py`; `make music` renders them to
  `build/music_preview.wav` so you can listen without the emulator.
- Animation: Tiny Dungeon sprites are single frames, so `tools/convert.py` generates
  6 extra frames per character and facing (two foot-lift walk frames, breathing,
  attack lean, two squash frames), with species-specific motion for bats (flap),
  ghosts (waver), slimes (squish) and spiders (skitter). Monsters rise out of the
  floor when they spawn and flatten before dissolving into smoke when killed. Melee
  swings sweep a 3-step arc using 8-direction weapon rotations, and magic projectiles
  pulse. 530 tiles in total, all with mirrored, flash and mask variants.

## Build

Needs `podman` (uses the `stefanreinauer/amiga-gcc` image), Python 3 + Pillow, and
`ffmpeg` (only if you re-convert sounds).

```
make            # -> hd/delve (the executable; hd/ is the emulated hard drive)
make autoplay   # test build: a bot plays the game, with an FPS counter
```

`tools/convert.py` turns `assets/` into `gen/assets.c` (palette, planar tiles + masks +
flash variants, font, samples). Balance tables are in `src/data.c`.

To run on a real A1200 or another emulator, copy `hd/delve` over and run it from the
Shell. It needs Kickstart 3.0+ and an AGA chipset, and uses about 600 KB of chip RAM.
