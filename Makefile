# BARE! — a synthesizer on bare metal. `make run` boots it in QEMU.
#   ARCH=i386   (default) 32-bit, runs on every PC since the Pentium Pro, BIOS or UEFI
#   ARCH=x86_64 64-bit build
NAME    := bare
ARCH    ?= i386
CC      := clang
LD      := ld.lld
BUILD   := build/$(ARCH)
LIMINE  := /usr/share/limine
VERSION := $(shell date +%y%m%d%H%M)   # baked into app.o; the image/update tools read it back from the ELF

CFLAGS  := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
           -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIC -fno-PIE \
           -ffunction-sections -fdata-sections -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
           -DBUILD_VERSION=$(VERSION) -Icore -Iarch/x86 -Iarch/x86/$(ARCH) -Iboot/$(ARCH)
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -z noexecstack --gc-sections -T boot/$(ARCH)/linker.lds

ifeq ($(ARCH),x86_64)
CFLAGS  += --target=x86_64-unknown-none-elf -mno-red-zone -mcmodel=kernel
LDFLAGS += -m elf_x86_64
BOOT_PROTO := limine
else
CFLAGS  += --target=i686-unknown-none-elf -m32 -march=i686
LDFLAGS += -m elf_i386
endif

SRC_C   := $(wildcard core/*.c arch/x86/*.c arch/x86/drivers/*.c arch/x86/$(ARCH)/*.c boot/$(ARCH)/*.c)
SRC_S   := $(wildcard arch/x86/$(ARCH)/*.S boot/$(ARCH)/*.S)
OBJ     := $(patsubst %.c,$(BUILD)/%.o,$(SRC_C)) $(patsubst %.S,$(BUILD)/%.o,$(SRC_S))
# Doom's engine (third_party/doom, GPL-2.0-or-later) and its glue (third_party/doom/bare): the kernel's code generation,
# its own warnings left alone, no debug information (the kernel slots are 4 MiB), a small C library's headers first.
# Its variables are moved to sections of their own (doom_data, doom_bss) so a new start can put them back as they were.
DOOM_C  := $(wildcard third_party/doom/*.c third_party/doom/bare/*.c)
DOOM_CFLAGS = $(filter-out -I% -g -fdata-sections -std=%,$(CFLAGS)) -std=gnu99 -w -nostdlibinc \
              -Ithird_party/doom/bare/libc -Ithird_party/doom -Ithird_party/doom/bare -Icore -DBARE_DOOM
OBJ     += $(patsubst %.c,$(BUILD)/%.o,$(DOOM_C))
KERNEL  := $(BUILD)/$(NAME).elf
ISO     := $(BUILD)/$(NAME).iso
IMG     := $(BUILD)/$(NAME).img
UPD     := $(BUILD)/$(NAME).upd

# QEMU: q35 chipset like a modern laptop, Intel HDA + PC speaker, serial log on stdout.
QEMU      := qemu-system-x86_64
KVM       := $(shell test -w /dev/kvm && echo -enable-kvm -cpu host)
QEMU_BASE := -M q35,pcspk-audiodev=snd0 -m 512M \
             -device intel-hda -device hda-duplex,audiodev=snd0 -monitor unix:$(BUILD)/monitor.sock,server,nowait

all: $(ISO)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DOOM_CFLAGS) -MMD -MP -c $< -o $@
	llvm-objcopy --rename-section .data=doom_data --rename-section .bss=doom_bss $@

# the version stamp lives in app.o: recompile it whenever any other object changes
$(BUILD)/core/app.o: $(filter-out $(BUILD)/core/app.o,$(OBJ))

$(KERNEL): $(OBJ) boot/$(ARCH)/linker.lds
	$(LD) $(LDFLAGS) $(OBJ) -o $@

core/font.h: tools/psf2c.py
	python3 tools/psf2c.py $@ t8x16:/usr/share/kbd/consolefonts/ter-u16n.psf.gz t12x24:/usr/share/kbd/consolefonts/ter-u24n.psf.gz
core/inst_builtin.c: tools/txt2c.py $(wildcard instruments/*.txt)
	python3 tools/txt2c.py $@ $(wildcard instruments/*.txt)

# The i386 ISO and image also carry the x86_64 kernel: Limine boots it on UEFI with a 64-bit CPU (see limine.conf).
ifeq ($(ARCH),i386)
KERNEL64 := build/x86_64/$(NAME).elf
$(KERNEL64): FORCE
	@$(MAKE) --no-print-directory ARCH=x86_64 $(KERNEL64)
FORCE:
endif

$(ISO): $(KERNEL) $(KERNEL64) boot/$(ARCH)/limine.conf
	rm -rf $(BUILD)/iso && mkdir -p $(BUILD)/iso/boot/limine $(BUILD)/iso/EFI/BOOT
	cp $(KERNEL) $(BUILD)/iso/boot/$(NAME).elf
	$(if $(KERNEL64),cp $(KERNEL64) $(BUILD)/iso/boot/$(NAME)64.elf)
	cp boot/$(ARCH)/limine.conf $(LIMINE)/limine-bios.sys $(LIMINE)/limine-bios-cd.bin $(LIMINE)/limine-uefi-cd.bin $(BUILD)/iso/boot/limine/
	cp $(LIMINE)/BOOTX64.EFI $(LIMINE)/BOOTIA32.EFI $(BUILD)/iso/EFI/BOOT/
	cp -r LICENSE NOTICE.md LICENSES $(BUILD)/iso/
	xorriso -as mkisofs -quiet -R -r -J -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
	    -hfsplus -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
	    --protective-msdos-label $(BUILD)/iso -o $@
	limine bios-install $@ 2>/dev/null

img: $(IMG)                 ## bootable disk image with a persistent project partition (i386 only)
$(IMG): $(KERNEL) $(KERNEL64) tools/mkimage.py boot/i386/limine.img.conf LICENSE NOTICE.md
	python3 tools/mkimage.py $(KERNEL) $@ --kernel64 $(KERNEL64)

# Freedoom (BSD licence), for trying Doom: typing iddqd in BARE! plays the WAD it finds on the stick. Never in a release.
FREEDOOM := build/freedoom/freedoom-0.13.0/freedoom1.wad
freedoom: $(FREEDOOM)       ## fetch Freedoom into build/freedoom (the Doom checks use it; doom-img puts it on a stick image)
$(FREEDOOM):
	mkdir -p build/freedoom
	curl -sSL -o build/freedoom/freedoom-0.13.0.zip https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip
	echo "3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59  build/freedoom/freedoom-0.13.0.zip" | sha256sum -c --quiet -
	cd build/freedoom && unzip -o -q freedoom-0.13.0.zip && touch freedoom-0.13.0/freedoom1.wad
doom-img: $(BUILD)/$(NAME)-doom.img   ## the stick image with Freedoom in its DOOM folder
$(BUILD)/$(NAME)-doom.img: $(KERNEL) $(KERNEL64) tools/mkimage.py $(FREEDOOM)
	python3 tools/mkimage.py $(KERNEL) $@ --kernel64 $(KERNEL64) --doom $(FREEDOOM)

RELEASE := $(shell sed -n 's/^\#define BARE_RELEASE "\(.*\)".*/\1/p' core/app.h)
DIST    := build/dist/$(NAME)-$(RELEASE)
dist: $(IMG) $(ISO) $(UPD)  ## release files in build/dist/bare-RELEASE/: stick image, ISO, update files, notes, sums
	rm -rf $(DIST) && mkdir -p $(DIST)
	cp $(IMG) $(DIST)/$(NAME)-$(RELEASE).img
	cp $(ISO) $(DIST)/$(NAME)-$(RELEASE).iso
	cp $(UPD) $(DIST)/BARE.UPD
	cp $(UPD) $(DIST)/HOMEBREW.UPD
	cp -r CHANGES.md LICENSE NOTICE.md LICENSES $(DIST)/
	cd $(DIST) && sha256sum *.img *.iso BARE.UPD HOMEBREW.UPD CHANGES.md > SHA256SUMS
	@ls -l $(DIST)

upd: $(UPD)                 ## update file: drop it as BARE.UPD on any FAT drive
$(UPD): $(KERNEL) $(KERNEL64) tools/hb.py
	python3 tools/hb.py mkupd $(KERNEL) $@ $(KERNEL64)

run-img: $(IMG)             ## boot the disk image as a USB stick in QEMU (BIOS + int13h path)
	$(QEMU) $(QEMU_BASE) $(KVM) -audiodev pipewire,id=snd0 -serial stdio -display gtk,zoom-to-fit=on \
	    -device qemu-xhci -drive if=none,id=stick,format=raw,file=$(IMG) -device usb-storage,drive=stick -boot menu=off

run: $(ISO)                 ## boot the ISO in QEMU with sound through PipeWire
	$(QEMU) $(QEMU_BASE) $(KVM) -cdrom $(ISO) -boot d -audiodev pipewire,id=snd0 -serial stdio -display gtk,zoom-to-fit=on

run-p2: $(ISO)              ## boot as an emulated Pentium II (no KVM): proves the i686 build
	$(QEMU) $(QEMU_BASE) -cdrom $(ISO) -boot d -cpu pentium2 -audiodev pipewire,id=snd0 -serial stdio -display gtk,zoom-to-fit=on

usb: $(IMG)                 ## flash a USB stick, keeping its project partition: make usb DEV=/dev/sdX
	@test -n "$(DEV)" || (echo "usage: make usb DEV=/dev/sdX" && false)
	sudo python3 tools/hb.py flash $(IMG) $(DEV)

clean:
	rm -rf build

# ---- host test harness: core/ as Linux programs (test/host.h) ----
include test/host.mk
SHOT_RES := 1280x800 1024x768 800x600 1366x768 1280x1024 1920x1080 3840x2160
test: $(HOST_OUT)/check $(BUILD)/$(NAME).img   ## pass/fail checks on the host, with a scratch copy of the stick image
	@cp $(BUILD)/$(NAME).img $(HOST_OUT)/check.img && truncate -s +64M $(HOST_OUT)/check.img && $(HOST_OUT)/check $(HOST_OUT)/check.img
	@echo "--- again, as a 32 MB PC (28 MiB left after the kernel)"
	@cp $(BUILD)/$(NAME).img $(HOST_OUT)/check.img && truncate -s +64M $(HOST_OUT)/check.img && HOST_MEM=28 $(HOST_OUT)/check $(HOST_OUT)/check.img > $(HOST_OUT)/check-32mb.txt \
	  && tail -1 $(HOST_OUT)/check-32mb.txt || (grep -B2 FAIL $(HOST_OUT)/check-32mb.txt; tail -1 $(HOST_OUT)/check-32mb.txt; false)
video: $(HOST_OUT)/video     ## a 70-second showcase played by a script, build/host/out/showcase.mp4 (needs ffmpeg)
	@mkdir -p $(HOST_OUT)/out && $(HOST_OUT)/video $(HOST_OUT)/out/showcase.mp4
bench: $(HOST_OUT)/bench     ## DSP cost in cycles per frame
	$(HOST_OUT)/bench
render: $(HOST_OUT)/render   ## offline renders + level/timing figures, WAVs in build/host/out/wav
	@mkdir -p $(HOST_OUT)/out/wav && $(HOST_OUT)/render $(HOST_OUT)/out/wav
shots: $(HOST_OUT)/shots     ## every page at every supported resolution, PNGs in build/host/out/shots
	@mkdir -p $(HOST_OUT)/out/shots
	@for r in $(SHOT_RES); do $(HOST_OUT)/shots $$(echo $$r | tr x ' ') $(HOST_OUT)/out/shots $${r}_ || exit 1; done
	@command -v magick >/dev/null && for f in $(HOST_OUT)/out/shots/*.ppm; do magick $$f $${f%.ppm}.png && rm $$f; done || true

-include $(OBJ:.o=.d)
theme-shots: $(HOST_OUT)/shots   ## every page in every colour scheme at 1280x800, a contact sheet each: build/host/out/themes
	@rm -rf $(HOST_OUT)/out/themes && mkdir -p $(HOST_OUT)/out/themes
	@for t in 0 1 2 3 4 5 6 7 8 9; do HOST_THEME=$$t $(HOST_OUT)/shots 1280 800 $(HOST_OUT)/out/themes $${t}_ >/dev/null || exit 1; done
	@cd $(HOST_OUT)/out/themes && for f in *.ppm; do magick $$f $${f%.ppm}.png && rm $$f; done
	@python3 tools/theme_sheets.py $(HOST_OUT)/out/themes

.PHONY: all img upd dist run run-img run-p2 usb clean test video bench render shots theme-shots freedoom doom-img FORCE
