# release packaging makefile

VER := $(shell head -n 1 ../common/version.h | \
	sed 's/.*"\(.*\)\.\(.*\)".*/\1\2/g')
BUILD := $(shell git describe HEAD | grep -- - | \
	sed -e 's/.*\-\(.*\)\-.*/\1/')
ifneq "$(BUILD)" ""
VER := $(VER)_$(BUILD)
endif

include ../../config.mak

all: rel

../../tools/textfilter: ../../tools/textfilter.c
	make -C ../../tools/ textfilter

readme.txt: ../../tools/textfilter ../base_readme.txt ../../ChangeLog
	../../tools/textfilter ../base_readme.txt $@ GP2X

CODE940 = code940/pico940_v3.bin
# rebuild the image when any embedded source changes
CODE940_SRC := $(wildcard code940/*.c code940/*.h code940/*.s \
	code940/uClibc/*.c code940/uClibc/*.h code940/uClibc/*.s) \
	../../pico/sound/ym2612.c ../../pico/sound/ym2612.h \
	../../pico/sound/ym2413.c ../../pico/sound/ym2413.h \
	../../pico/sound/emu2413/emu2413.c ../../pico/sound/emu2413/emu2413.h \
	../../pico/misc_arm.s ../common/mp3_sync.c

PicoDrive: ../../picodrive
	$(STRIP) $^ -o $@

rel: PicoDrive PicoDrive.gpe $(CODE940) readme.txt ../game_def.cfg \
		PicoDrive.png PicoDrive_s.png PicoDrive_t.png \
		warm_2.4.25.o warm_2.4.26-open2x.o warm_2.6.24.ko \
		../../pico/carthw.cfg
	mkdir -p out/PicoDrive/skin/
	cp $^ out/PicoDrive/
	cp PicoDrive.ini out/
	cp skin/*.png out/PicoDrive/skin/
	cp skin/*.txt out/PicoDrive/skin/
	#mkdir out/bin_to_cso_mp3
	#cp ../../tools/bin_to_cso_mp3/* out/bin_to_cso_mp3/
	cd out && zip -9 -r ../../../PicoDrive_$(VER).zip *
	rm -rf out

# Without Helix, source-only checkouts use the checked-in image.
ifneq ($(wildcard ../common/helix/pub/mp3dec.h),)
$(CODE940): $(CODE940_SRC)
	$(MAKE) -C code940/
else
$(CODE940):
	@echo "error: $@ is missing; unpack the Helix decoder in ../common/helix/" >&2
	@exit 1
endif
