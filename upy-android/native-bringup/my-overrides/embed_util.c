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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/persistentcode.h"
#include "py/runtime.h"
#include "shared/runtime/gchelper.h"
#include "port/micropython_embed.h"

// Set micropython virtual file system (vfs) to app private storage
// Gives scripts a clean filesystem view.
static void mp_embed_mount_vfs(const char *root_path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "import os, sys\n"
        "os.mount(os.VfsPosix('%s'), '/')\n"
        // do a chdir to private storage root, so relative paths resolve correctly.
        "os.chdir('/')\n"
        "sys.path[:] = ['/', '.frozen']\n",
        root_path);
    mp_embed_exec_str(cmd);
}

// Initialise the runtime.
//
// stack_size is size in bytes of calling thread's stack, measured from stack_top.
// root_path is the app's own private storage directory. see mp_embed_mount_vfs.
//
void mp_embed_init(void *gc_heap, size_t gc_heap_size, void *stack_top, size_t stack_size, const char *root_path) {
    // set stack top and stack_limit used by MICROPY_STACK_CHECK
    mp_cstack_init_with_top(stack_top, stack_size);
    gc_init(gc_heap, (uint8_t *)gc_heap + gc_heap_size);
    mp_init();
    mp_embed_mount_vfs(root_path);
}

// Type name of the last mp_embed_exec_str() uncaught exception, "" if none.
static char last_exception[64];

const char *mp_embed_last_exception(void) {
    return last_exception;
}

#if MICROPY_ENABLE_COMPILER
// Compile and execute the given source script (Python text).
void mp_embed_exec_str(const char *src) {
    last_exception[0] = '\0';
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Compile, parse and execute the given string.
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        // is_repl=false: this runs whole scripts as files/modules, not an
        // interactive REPL -- mp_compile()'s third param (py/compile.h)
        // is literally named is_repl and independently controls whether
        // bare top-level expression statements get auto-printed like a
        // real >>> prompt, regardless of the MP_PARSE_FILE_INPUT parse
        // mode above. Was `true` (upstream's own embedding example is a
        // REPL tool, where that's correct); left uncaught here, every
        // non-None top-level expression statement's result was silently
        // being auto-printed for the whole life of this port -- never
        // noticed because no script happened to have one until
        // find_line_segments.py's `img.draw_line(l, ...)` (returns self,
        // for chaining) as a bare loop-body statement. see session-state.
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        // Uncaught exception: print it out.
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        snprintf(last_exception, sizeof(last_exception), "%s",
            qstr_str(mp_obj_get_type((mp_obj_t)nlr.ret_val)->name));
    }
}
#endif

#if MICROPY_PERSISTENT_CODE_LOAD
void mp_embed_exec_mpy(const uint8_t *mpy, size_t len) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Execute the given .mpy data.
        mp_module_context_t *ctx = m_new_obj(mp_module_context_t);
        ctx->module.globals = mp_globals_get();
        mp_compiled_module_t cm;
        cm.context = ctx;
        mp_raw_code_load_mem(mpy, len, &cm);
        mp_obj_t f = mp_make_function_from_proto_fun(cm.rc, ctx, MP_OBJ_NULL);
        mp_call_function_0(f);
        nlr_pop();
    } else {
        // Uncaught exception: print it out.
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}
#endif

// Deinitialise the runtime.
void mp_embed_deinit(void) {
    mp_deinit();
}

#if MICROPY_ENABLE_GC
// Run a garbage collection cycle.
void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}
#endif

// Called if an exception is raised outside all C exception-catching handlers.

void nlr_jump_fail(void *val) {
    fprintf(stderr, "upy-android: fatal: nlr_jump_fail (uncaught exception with no handler)\n");
    // android: `abort();` instead of `for (;;) {}`, restart instead of hang.
    abort();
}

#ifndef NDEBUG
// Used when debugging is enabled. Same reasoning as nlr_jump_fail above.
void __assert_func(const char *file, int line, const char *func, const char *expr) {
    fprintf(stderr, "upy-android: fatal: assertion failed: %s, file %s, line %d, function %s\n",
        expr, file, line, func);
    abort();
}
#endif
