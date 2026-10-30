CROSS_COMPILE ?= arm-linux-gnueabihf-
USER_CC ?= $(CROSS_COMPILE)gcc
BUILD ?= build/arm
CORE := $(BUILD)/fceumm_libretro.so
INCLUDE := vendor/fceumm/src/drivers/libretro/libretro-common/include
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror
.PHONY: all host clean
all: $(BUILD)/nes $(BUILD)/nes-input
$(BUILD)/core/Makefile:
	mkdir -p $(BUILD)
	cp -a vendor/fceumm $(BUILD)/core
$(CORE): $(BUILD)/core/Makefile
	$(MAKE) -C $(BUILD)/core platform=unix CC="$(USER_CC)" -j4
	cp $(BUILD)/core/fceumm_libretro.so $@
$(BUILD)/nes: nes.c pcm.inc input.inc $(CORE)
	$(USER_CC) $(CFLAGS) -I$(INCLUDE) -o $@ nes.c -L$(BUILD) -l:fceumm_libretro.so -Wl,-rpath,'$$ORIGIN' -lm
$(BUILD)/nes-input: input_test.c
	mkdir -p $(BUILD)
	$(USER_CC) $(CFLAGS) -o $@ $<
host:
	$(MAKE) USER_CC=cc BUILD=build/host all
clean:
	rm -rf build
.PHONY: check dist
check: host
	cc $(CFLAGS) -I$(INCLUDE) -o build/host/test_port tests/test_port.c -Lbuild/host -l:fceumm_libretro.so -Wl,-rpath,'$$ORIGIN' -lm
	./build/host/test_port
	cc $(CFLAGS) -I$(INCLUDE) -o build/host/test_pcm tests/test_pcm.c -Lbuild/host -l:fceumm_libretro.so -Wl,-rpath,'$$ORIGIN' -lm
	./build/host/test_pcm
	cc $(CFLAGS) -I$(INCLUDE) -o build/host/test_renderer tests/test_renderer.c -Lbuild/host -l:fceumm_libretro.so -Wl,-rpath,'$$ORIGIN' -lm
	./build/host/test_renderer
	cc $(CFLAGS) -I$(INCLUDE) -o build/host/test_input tests/test_input.c -Lbuild/host -l:fceumm_libretro.so -Wl,-rpath,'$$ORIGIN' -lm
	./build/host/test_input
	! ./build/host/nes --frames 0 missing.nes
	! ./build/host/nes /nonexistent/game.nes

dist: all
	mkdir -p dist/nes/roms
	cp $(BUILD)/nes $(BUILD)/nes-input $(CORE) README.md UPSTREAM.md dist/nes/
	cp vendor/fceumm/Copying dist/nes/COPYING
	tar -czf dist/nes-armhf.tar.gz -C dist nes
