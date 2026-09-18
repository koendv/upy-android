# Third-party assets

## Vendored native code (compiled into libupy_engine.so)

This project embeds MicroPython and parts of OpenMV's `imlib` machine-vision
library, both regenerated/copied in via `native-bringup/micropython_embed.mk`
and `native-bringup/apply-overrides.sh`.
These files are unmodified upstream copies, not patched.

### MicroPython

- Source: https://github.com/micropython/micropython
- Copyright: 2013-2026 Damien P. George and contributors
- License: MIT

### OpenMV imlib / py_image

- Source: https://github.com/openmv/openmv
- License: MIT, with one exception below (zbar.c). OpenMV's own GPL-licensed
  files (AGAST, LSD) are excluded entirely — their `IMLIB_ENABLE_*` feature
  flags are never defined, so no GPL code is compiled in.

### zbar (barcode/QR decoding) — LGPL-2.1-or-later

`app/src/main/cpp/micropython_embed/openmv/zbar.c` (vendored via OpenMV's
own tree above) is the ZBar Bar Code Reader library, statically compiled
into `libupy_engine.so` (`IMLIB_ENABLE_BARCODES` is defined — this is
active, shipped code, not an unused file):

- Source: http://sourceforge.net/projects/zbar (as cited in the vendored
  file's own header)
- Copyright: 2008-2010 Jeff Brown <spadix@users.sourceforge.net>
- License: GNU Lesser General Public License, version 2.1 or later

Since this is a static link, this project's complete source (including this
file, unmodified) is made available — see the source archive/repository
this NOTICE ships alongside — satisfying LGPL's rebuild/relink provisions.
The LGPL-2.1 license text: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html

### ulab (numpy/scipy-like numerical computing)

- Source: https://github.com/v923z/micropython-ulab
- Copyright: 2019-2021 Zoltán Vörös and contributors
- License: MIT
- Vendored at commit 01ad8a5, `code/` subdirectory only.

## App icon

`app/src/main/res/drawable/ic_launcher_foreground.xml` is derived from the
MicroPython logo:

- Source: https://commons.wikimedia.org/wiki/File:MicroPython_new_logo.svg
- Copyright: micropython.org
- License: MIT

Used here as a community prototype/datapoint icon (see project README/forum
post context) — this project is an independent, unofficial Android port and
is not affiliated with or endorsed by the MicroPython project.

## Terminal command history

`app/src/main/java/eu/kdvelectronics/upyandroid/managers/TerminalHistoryManager.kt`
is adapted, unchanged apart from the package name, from:

- Source: https://github.com/Ma7moud3ly/micro-repl
- Author: Ma7moud3ly
- License: MIT

Everything else in this app (the AIDL-based transport, the separate-process
engine, the UI, the VFS) was written independently — micro-repl's raw-REPL/
USB-serial design was not reused.

## File explorer and code editor

The file explorer and code editor screens (added 2026-09-16) follow the
same functionality as micro-repl's own explorer/editor screens, rewritten
against local sandboxed storage (`java.io.File`, rooted at the same
directory MicroPython's VFS mounts as "/") instead of micro-repl's
remote-board REPL round-trip — no source code copied.

The code editor itself uses a separate library, not micro-repl's own code:

- Library: Nemo Code Editor
- Source: https://github.com/Ma7moud3ly/nemo-editor
- Author: Ma7moud3ly
- License: MIT
- Used via its published Maven artifact (`io.github.ma7moud3ly:nemo-editor`),
  not vendored — same version micro-repl itself depends on.
