# HARFBUZZ

HARFBUZZ_VERSION := 0.9.17
HARFBUZZ_URL := http://www.freedesktop.org/software/harfbuzz/release/harfbuzz-$(HARFBUZZ_VERSION).tar.bz2

ifdef HAVE_MACOSX
PKGS += harfbuzz
EXTRA_LDFLAGS = -framework CoreFoundation -liconv -framework Carbon
endif

ifeq ($(call need_pkg,"harfbuzz"),)
PKGS_FOUND += harfbuzz
endif

$(TARBALLS)/harfbuzz-$(HARFBUZZ_VERSION).tar.bz2:
	$(call download,$(HARFBUZZ_URL))

.sum-harfbuzz: harfbuzz-$(HARFBUZZ_VERSION).tar.bz2

harfbuzz: harfbuzz-$(HARFBUZZ_VERSION).tar.bz2 .sum-harfbuzz
	$(UNPACK)
	$(UPDATE_AUTOCONFIG)
	$(APPLY) $(SRC)/harfbuzz/harfbuzz-aarch64.patch
	$(APPLY) $(SRC)/harfbuzz/harfbuzz-clang.patch
	$(APPLY) $(SRC)/harfbuzz/disable-backends.patch
	$(MOVE)

DEPS_harfbuzz = freetype2 $(DEPS_freetype2)

.harfbuzz: harfbuzz
	cd $< && env NOCONFIGURE=1 sh autogen.sh
	cd $< && $(HOSTVARS) CFLAGS="$(CFLAGS)" LDFLAGS="$(EXTRA_LDFLAGS)" ./configure $(HOSTCONF) ICU_CONFIG=no
	export PATH="$(PREFIX)/bin":$$PATH; cd $< && $(MAKE) install
	touch $@
