/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2022-2023 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef MICROPY_INCLUDED_MICROPYTHON_EMBED_H
#define MICROPY_INCLUDED_MICROPYTHON_EMBED_H

#include <stddef.h>
#include <stdint.h>

// stack_size is the actual size (bytes) of the calling thread's stack,
// measured from stack_top. MICROPY_STACK_CHECK requires this to work
// (see port/embed_util.c). root_path is the app's private storage directory,
// mounted as a VfsPosix at "/" (see port/embed_util.c's mp_embed_mount_vfs).
void mp_embed_init(void *gc_heap, size_t gc_heap_size, void *stack_top, size_t stack_size, const char *root_path);
void mp_embed_deinit(void);

// Only available if MICROPY_ENABLE_COMPILER is enabled.
void mp_embed_exec_str(const char *src);

// Only available if MICROPY_PERSISTENT_CODE_LOAD is enabled.
void mp_embed_exec_mpy(const uint8_t *mpy, size_t len);

// Per-call output capture (upy-android addition, port/mphalport.c). Call
// mp_embed_output_clear() before, and mp_embed_output_get() after, each
// mp_embed_exec_str()/mp_embed_exec_mpy() call. Output includes both
// normal print() text and any uncaught-exception traceback.
//
// The only output some callers get: AdbExecProvider runs a script via
// one-shot adb shell content call, with no live channel back to the
// shell, so this return value is the whole answer.
void mp_embed_output_clear(void);
const char *mp_embed_output_get(void);

// Optional live output tap (upy-android addition, port/mphalport.c).
// Needed for long-running/infinite scripts (e.g. while True:
// print(...)): mp_embed_exec_str() only returns once the script stops,
// so without live chunks the terminal would show nothing at all while
// such a script runs, only a dump at the end. Invoked once per
// mp_hal_stdout_tx_strn_cooked() write, i.e. once per print()/
// traceback write, not once per mp_embed_exec_str() call like
// mp_embed_output_get() above. str is not null-terminated; use len.
// Pass cb == NULL to disable (the default). Set this around a single
// mp_embed_exec_str()/mp_embed_exec_mpy() call and clear the callback
// again after. Context is caller-owned and only valid for that one call.
typedef void (*mp_embed_output_chunk_cb_t)(const char *str, size_t len, void *context);
void mp_embed_set_output_chunk_cb(mp_embed_output_chunk_cb_t cb, void *context);

#endif // MICROPY_INCLUDED_MICROPYTHON_EMBED_H
