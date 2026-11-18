# DELVE - Amiga AGA roguelike. Cross-compiled with amiga-gcc inside podman.
IMAGE  = docker.io/stefanreinauer/amiga-gcc:latest
RUN    = podman run --rm --network=none -v $(CURDIR):/work:Z -w /work $(IMAGE)
CFLAGS = -O2 -m68020 -noixemul -fomit-frame-pointer -Wall -Wno-pointer-sign -Isrc -Igen
SRC    = $(wildcard src/*.c) gen/assets.c

all: hd/delve

gen/assets.c gen/assets.h: tools/convert.py assets/tiny_dungeon.png assets/font.ttf $(wildcard assets/sfx/*.raw)
	python3 -I tools/convert.py

hd/delve: $(SRC) $(wildcard src/*.h) gen/assets.h
	$(RUN) m68k-amigaos-gcc $(CFLAGS) $(SRC) -o hd/delve
	@ls -l hd/delve

run: hd/delve
	flatpak run net.fsuae.FS-UAE $(CURDIR)/delve.fs-uae &

clean:
	rm -f hd/delve gen/assets.c gen/assets.h

music:
	python3 -I tools/music_preview.py

.PHONY: all run clean music

# test build: a bot plays the game (for automated screenshots)
autoplay: $(SRC) $(wildcard src/*.h) gen/assets.h
	$(RUN) m68k-amigaos-gcc $(CFLAGS) -DAUTOPLAY $(SRC) -o hd/delve
