/* upy-android native bring-up smoke test.
 * Based on MicroPython's ports/embed/examples/embedding/main.c, extended to
 * exercise the crash-mitigation requirements from project memory:
 *  - MICROPY_STACK_CHECK actually catching deep recursion (requirement #1)
 *  - a real GC heap size, not the 8KB demo size (requirement #6)
 */

#include "port/micropython_embed.h"

// Standalone-test-only root path (the real engine gets this from Kotlin's
// Context.filesDir instead -- see engine_jni.cpp).
#define TEST_ROOT_PATH "/data/local/tmp/upy_vfs_test"

static const char *example_1 =
    "print('hello world!', list(x + 1 for x in range(10)), end='eol\\n')";

static const char *example_2 =
    "for i in range(10):\n"
    "    print('iter {:08}'.format(i))\n"
    "\n"
    "try:\n"
    "    1//0\n"
    "except Exception as er:\n"
    "    print('caught exception', repr(er))\n"
    "\n"
    "import gc\n"
    "print('run GC collect')\n"
    "gc.collect()\n"
    "\n"
    "print('finish')\n"
;

// Deliberately unbounded recursion. With MICROPY_STACK_CHECK wired up
// correctly (mp_cstack_init_with_top with a real stack_size), this must
// raise a catchable RuntimeError instead of overflowing the native stack.
static const char *example_3 =
    "def f(n):\n"
    "    return f(n + 1)\n"
    "try:\n"
    "    f(0)\n"
    "except RuntimeError as er:\n"
    "    print('stack check caught', repr(er))\n"
;

// VFS test: write a file, read it back, list the directory, then import
// it as a module -- exercises open()/os.listdir()/import all landing on
// real files under the mounted root (see mp_embed_mount_vfs).
static const char *example_4 =
    "with open('hello.txt', 'w') as f:\n"
    "    f.write('hello from vfs')\n"
    "with open('hello.txt') as f:\n"
    "    print('read back:', f.read())\n"
    "print('listdir:', os.listdir('/'))\n"
    "with open('mymod.py', 'w') as f:\n"
    "    f.write('greeting = \"hi from mymod\"')\n"
    "import mymod\n"
    "print('import:', mymod.greeting)\n"
    "try:\n"
    "    open('/etc/hosts')\n"
    "    print('ESCAPED JAIL -- BUG')\n"
    "except OSError as er:\n"
    "    print('jailed correctly:', repr(er))\n"
;

// math module test: not overridden in mpconfigport.h, so MICROPY_PY_MATH
// should already be on by default at our CORE_FEATURES rom level -- this
// checks whether that's actually true (verify, don't assume).
static const char *example_5 =
    "import math\n"
    "print('math:', math.sqrt(16), math.pi, math.floor(3.7))\n"
;

// random module test: exercises the seed-on-import path
// (MICROPY_PY_RANDOM_SEED_INIT_FUNC -> mp_android_random_seed_init ->
// arc4random_buf) plus the EXTRA_FUNCS surface (randint/choice/random).
// randint(1, 1) forces a known value (range of exactly one option) as a
// sanity check that isn't just "did it crash".
static const char *example_6 =
    "import random\n"
    "print('random:', random.randint(1, 1), random.choice([42]), 0.0 <= random.random() < 1.0)\n"
;

// json module test: round-trip dumps/loads, exercises objstringio/stream
// plumbing underneath (no separate port support needed, see mpconfigport.h).
static const char *example_7 =
    "import json\n"
    "s = json.dumps({'a': 1, 'b': [1, 2, 3]})\n"
    "print('json:', s, json.loads(s) == {'a': 1, 'b': [1, 2, 3]})\n"
;

// re module test: exercises the vendored lib/re1.5 engine plus
// MATCH_GROUPS/MATCH_SPAN_START_END/RE_SUB (all forced on explicitly,
// default off even at CORE_FEATURES).
static const char *example_8 =
    "import re\n"
    "m = re.match(r'(\\w+)@(\\w+)', 'foo@bar')\n"
    "print('re:', m.group(0), m.group(1), m.group(2), m.span(0))\n"
    "print('re sub:', re.sub(r'\\d+', '#', 'a1b22c333'))\n"
;

// time module test: ticks/sleep (chunked, non-busy-waiting mp_hal_delay_ms
// -- see mphalport.c) plus real gmtime/localtime/mktime (mod_time_android.c
// -- distinct UTC vs. device-local, not aliased like the esp32/rp2-style
// generic path). t0/t1 sanity-check that sleep_ms(50) actually elapsed
// roughly 50ms via ticks_diff, not just that it returned without crashing.
static const char *example_9 =
    "import time\n"
    "t0 = time.ticks_ms()\n"
    "time.sleep_ms(50)\n"
    "t1 = time.ticks_ms()\n"
    "elapsed = time.ticks_diff(t1, t0)\n"
    "print('time: elapsed_ok', 40 <= elapsed <= 500)\n"
    "print('time: ticks_cpu', time.ticks_cpu())\n"
    "g = time.gmtime(0)\n"
    "print('time: gmtime(0)', g)\n"
    "now = time.time()\n"
    "print('time: time() plausible', now > 1700000000)\n"
    "loc = time.localtime(0)\n"
    "print('time: localtime(0)', loc)\n"
    "print('time: gmtime != localtime (device not on UTC)', g != loc)\n"
    "print('time: mktime(localtime(x)) round-trip', time.mktime(loc) == 0)\n"
;

// binascii module test: hexlify/unhexlify round-trip plus base64.
static const char *example_10 =
    "import binascii\n"
    "h = binascii.hexlify(b'hi')\n"
    "print('binascii:', h, binascii.unhexlify(h) == b'hi')\n"
    "b64 = binascii.b2a_base64(b'hello', newline=False)\n"
    "print('binascii b64:', b64, binascii.a2b_base64(b64) == b'hello')\n"
;

// cmath module test: complex arithmetic + a transcendental function.
static const char *example_11 =
    "import cmath\n"
    "z = 1 + 1j\n"
    "print('cmath:', abs(z), cmath.phase(z), cmath.sqrt(-1))\n"
;

// errno module test: pairs with the VFS jail-check in example_4, which
// already raises OSError(2,) for a nonexistent path -- confirm that 2
// really is errno.ENOENT by name, not just a number that happens to work.
static const char *example_12 =
    "import errno\n"
    "try:\n"
    "    open('/etc/hosts')\n"
    "except OSError as er:\n"
    "    print('errno:', er.errno == errno.ENOENT, errno.errorcode[er.errno])\n"
;

// help('modules') test: py/builtinhelp.c is already core-compiled, this
// is purely a config-flag addition (MICROPY_PY_BUILTINS_HELP{,_MODULES}).
// Enumerates every MP_REGISTER_MODULE'd module automatically (walks
// genhdr's module registry) -- confirms ulab shows up alongside the
// stdlib additions with zero separate maintenance.
static const char *example_13 =
    "help('modules')\n"
;

// ulab module test: numpy/scipy-like numerical extension, not a core
// MicroPython module -- vendored separately (my-overrides/ulab/, see
// micropython_embed.mk). Deliberately a non-trivial array (1000 elements,
// not a 3-element toy) since ndarrays allocate from the GC heap and the
// point of this test is partly to see whether a real numeric workload fits
// in the current heap size at all, not just whether the API compiles.
static const char *example_14 =
    "import ulab\n"
    "from ulab import numpy as np\n"
    "a = np.array(range(1000))\n"
    "b = a * 2 + 1\n"
    "print('ulab sum', np.sum(b))\n"
    "print('ulab mean/std', np.mean(a), np.std(a))\n"
    "print('ulab dot', np.sum(a * a))\n"
;

// OpenMV image/imlib module test: vendored separately (my-overrides/openmv/,
// see micropython_embed.mk), same tier as ulab above -- not a core
// MicroPython module. First real end-to-end run of this module through the
// actual main.c/mpconfigport.h pipeline (not the throwaway compile-only
// spike's own standalone spike_main.c) -- same script the spike already
// proved works on this hardware (native-bringup/openmv-spike/spike_main.c),
// repeated here as the permanent regression test.
static const char *example_15 =
    "import image\n"
    "print('image module imported ok')\n"
    "img = image.Image(32, 32, image.GRAYSCALE)\n"
    "print('created image:', img.width(), img.height())\n"
    "img.set_pixel((1, 1), 200)\n"
    "print('pixel readback:', img.get_pixel((1, 1)))\n"
    "print('find_blobs on blank image:', img.find_blobs([(0, 255)]))\n"
    "for x in range(10):\n"
    "    for y in range(10):\n"
    "        _ = img.set_pixel((x, y), 255)\n"  // set_pixel returns self (chaining) -- discard, or the bare statement's REPL-style auto-print floods output 100x
    "print('find_blobs on filled square:', img.find_blobs([(200, 255)]))\n"
;

// time.clock() test: OpenMV script compatibility (find_apriltags.py,
// lcd_shield.py, single_color_rgb565_blob_tracking.py all do
// `clock = time.clock()` once, then clock.tick()/clock.fps() in a loop
// -- see py_clock.c/SESSION_STATE.yaml). Exercises the
// MICROPY_PY_TIME_EXTRA_GLOBALS wiring in modtime_android.c, not just
// that py_clock.c compiles.
static const char *example_16 =
    "import time\n"
    "clock = time.clock()\n"
    "print('clock:', clock)\n"
    "clock.tick()\n"
    "time.sleep_ms(50)\n"
    "fps = clock.fps()\n"
    "print('fps plausible:', 0 < fps < 1000)\n"
    "clock.reset()\n"
    "clock.tick()\n"
    "time.sleep_ms(10)\n"
    "print('avg plausible:', clock.avg() > 0)\n"
;

// Barcode/QR/keypoint test: zbar.c (find_barcodes, LGPL-2.1+),
// qrcode.c/quirc (find_qrcodes, MIT), orb.c+fast.c (find_keypoints,
// both BSD-3-Clause -- agast.c dropped, see SESSION_STATE.yaml) all
// newly vendored. No real barcode/QR image data available in this
// smoke test, so find_barcodes()/find_qrcodes() are only proven to run
// cleanly (empty result, not a crash) on a blank image -- genuine
// decode-a-real-code verification is a separate, later exercise.
// find_keypoints() gets the stronger test: a filled square with real
// corners, same idea as example_15's find_blobs() test.
// corner_detector=image.CORNER_FAST is REQUIRED explicitly: py_image.c's
// allowed_args default is CORNER_AGAST (hardcoded, unconditional) --
// since agast.c is deliberately not vendored (dropped, see
// SESSION_STATE.yaml), that default now points at a detector that was
// never compiled in, so kpts silently stays empty unless FAST is
// requested by name.
// Canvas size: orb_find_keypoints() (orb.c) computes
// roi_scaled.w = image_w - PATCH_SIZE*2 (PATCH_SIZE=31) and returns
// immediately with zero keypoints -- never even calling fast_detect()
// -- whenever roi_scaled.w <= PATCH_SIZE*2, i.e. whenever image_w <=
// 124. A first attempt at 32x32, then 96x96, both silently hit this
// guard before fast_detect() ever ran -- misread as "the detector
// found nothing" when the detector was never actually invoked. 160x160
// clears it with real margin.
static const char *example_17 =
    "import image\n"
    "blank = image.Image(32, 32, image.GRAYSCALE)\n"
    "print('find_barcodes on blank:', blank.find_barcodes())\n"
    "print('find_qrcodes on blank:', blank.find_qrcodes())\n"
    "textured = image.Image(160, 160, image.GRAYSCALE)\n"
    "for x in range(30, 60):\n"
    "    for y in range(30, 60):\n"
    "        _ = textured.set_pixel((x, y), 255)\n"
    "kpts = textured.find_keypoints(threshold=10, corner_detector=image.CORNER_FAST)\n"
    "print('find_keypoints found something:', kpts is not None)\n"
;

// Real GC heap size (crash-mitigation requirement #6) -- the 8KB demo size
// is a toy value, not representative of what a REPL needs.
static char heap[2 * 1024 * 1024];

// Explicit stack size for this test, smaller than the OS-provided main
// thread stack so the check trips safely well before any real overflow.
// The real engine will pass the worker thread's actual configured size.
#define TEST_STACK_SIZE (256 * 1024)

#include <stdio.h>
#include <sys/stat.h>

static void run_and_print(const char *src) {
    mp_embed_output_clear();
    mp_embed_exec_str(src);
    printf("%s", mp_embed_output_get());
    fflush(stdout);
}

int main() {
    mkdir(TEST_ROOT_PATH, 0700);

    int stack_top;
    mp_embed_init(&heap[0], sizeof(heap), &stack_top, TEST_STACK_SIZE, TEST_ROOT_PATH);

    run_and_print(example_1);
    run_and_print(example_2);
    run_and_print(example_3);
    run_and_print(example_4);
    run_and_print(example_5);
    run_and_print(example_6);
    run_and_print(example_7);
    run_and_print(example_8);
    run_and_print(example_9);
    run_and_print(example_10);
    run_and_print(example_11);
    run_and_print(example_12);
    run_and_print(example_13);
    run_and_print(example_14);
    run_and_print(example_15);
    run_and_print(example_16);
    run_and_print(example_17);

    mp_embed_deinit();

    return 0;
}
