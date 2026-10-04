# Android-specific tuned-down extmod.mk. Real MicroPython ports
# (unix, stm32, esp32, rp2, ...) get extmod modules and lib/re1.5 by
# including $(MICROPYTHON_TOP)/extmod/extmod.mk and compiling straight
# from the monorepo tree. ports/embed/embed.mk deliberately does not
# pull that in (most extmod modules, networking, flash filesystems,
# bluetooth, ssl, don't make sense for "embed MicroPython in an
# arbitrary host app"), so this project has to opt the few modules it
# actually uses back in itself: json, os, random, re, time, binascii,
# hashlib, deflate, and the VFS/POSIX-file layer.
#
# Every file below was confirmed byte-identical to its real upstream
# path before this was written. This file's whole point is to fetch
# them fresh from the pinned $(MICROPYTHON_TOP) clone at generation
# time instead of vendoring copies into this repo's own git history
# (my-overrides/extmod/ no longer exists as of this file).
#
# lib/re1.5/*.c are not separate compile units here, matching real
# upstream: extmod/modre.c #includes them textually (a quoted #include
# resolves relative to the including file's own directory first,
# before any -I path), so they only need to exist at extmod/lib/re1.5/
# alongside the copied modre.c. Never compiled separately, never in
# SRC_QSTR. Same for lib/uzlib (moddeflate.c, modbinascii.c's crc32) and
# lib/crypto-algorithms (modhashlib.c's sha256).

ANDROID_EXTMOD_FILES = \
	misc.h \
	modasyncio.c \
	modbinascii.c \
	moddeflate.c \
	modhashlib.c \
	modheapq.c \
	modjson.c \
	modos.c \
	modrandom.c \
	modre.c \
	modselect.c \
	modtime.c \
	modtime.h \
	moductypes.c \
	vfs.c \
	vfs.h \
	vfs_posix.c \
	vfs_posix.h \
	vfs_posix_file.c \
	vfs_reader.c

ANDROID_RE15_FILES = \
	charclass.c \
	compilecode.c \
	re1.5.h \
	recursiveloop.c

ANDROID_UZLIB_FILES = \
	adler32.c \
	crc32.c \
	defl_static.c \
	header.c \
	lz77.c \
	tinflate.c \
	uzlib.h \
	uzlib_conf.h

ANDROID_CRYPTO_FILES = \
	sha256.c \
	sha256.h

# Same ordering requirement as micropython_embed.mk's own SRC_QSTR
# entries (this file is included before `include embed.mk` there):
# mkrules.mk captures $(SRC_QSTR) as a prerequisite list at *parse*
# time, so this append must land before that include.
SRC_QSTR += $(addprefix $(MICROPYTHON_TOP)/extmod/,$(filter %.c,$(ANDROID_EXTMOD_FILES)))

# Accumulates onto embed.mk's own `all: micropython-embed-package`.
# GNU Make merges prerequisite lists declared across multiple
# `target: prereqs` lines for the same target, so this doesn't need to
# come after that line, just before `all` is actually built (i.e.
# after every include is processed, which parse-time accumulation
# already guarantees).
all: android-extmod-package

# Depends on micropython-embed-package (not just "all") so this runs
# after embed.mk's own recipe wipes and recreates $(PACKAGE_DIR)/extmod.
# Running before that would have these files deleted immediately.
.PHONY: android-extmod-package
android-extmod-package: micropython-embed-package
	$(ECHO) "- extmod (android)"
	$(Q)for f in $(ANDROID_EXTMOD_FILES); do \
		$(CP) $(MICROPYTHON_TOP)/extmod/$$f $(PACKAGE_DIR)/extmod/$$f; \
	done
	$(Q)$(MKDIR) -p $(PACKAGE_DIR)/extmod/lib/re1.5
	$(Q)for f in $(ANDROID_RE15_FILES); do \
		$(CP) $(MICROPYTHON_TOP)/lib/re1.5/$$f $(PACKAGE_DIR)/extmod/lib/re1.5/$$f; \
	done
	$(Q)$(MKDIR) -p $(PACKAGE_DIR)/extmod/lib/uzlib
	$(Q)for f in $(ANDROID_UZLIB_FILES); do \
		$(CP) $(MICROPYTHON_TOP)/lib/uzlib/$$f $(PACKAGE_DIR)/extmod/lib/uzlib/$$f; \
	done
	$(Q)$(MKDIR) -p $(PACKAGE_DIR)/extmod/lib/crypto-algorithms
	$(Q)for f in $(ANDROID_CRYPTO_FILES); do \
		$(CP) $(MICROPYTHON_TOP)/lib/crypto-algorithms/$$f $(PACKAGE_DIR)/extmod/lib/crypto-algorithms/$$f; \
	done
	$(ECHO) "- extmod/sources.cmake"
	$(Q)echo "set(MPY_EXTMOD_SOURCES" > $(PACKAGE_DIR)/extmod/sources.cmake
	$(Q)for f in $(filter %.c,$(ANDROID_EXTMOD_FILES)); do \
		echo "    micropython_embed/extmod/$$f" >> $(PACKAGE_DIR)/extmod/sources.cmake; \
	done
	$(Q)echo ")" >> $(PACKAGE_DIR)/extmod/sources.cmake
