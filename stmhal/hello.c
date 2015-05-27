/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
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

#include <stdint.h>
#include <string.h>

#include "py/nlr.h"
#include "py/obj.h"
#include "py/objtuple.h"
#include "py/objstr.h"
#include "genhdr/mpversion.h"
#include "lib/fatfs/ff.h"
#include "lib/fatfs/diskio.h"
#include "timeutils.h"
#include "rng.h"
#include "file.h"
#include "sdcard.h"
#include "fsusermount.h"
#include "portmodules.h"

/// \module os - basic "operating system" services
///
/// The `os` module contains functions for filesystem access and `urandom`.
///
/// The filesystem has `/` as the root directory, and the available physical
/// drives are accessible from here.  They are currently:
///
///     /flash      -- the internal flash filesystem
///     /sd         -- the SD card (if it exists)
///
/// On boot up, the current directory is `/flash` if no SD card is inserted,
/// otherwise it is `/sd`.

STATIC mp_obj_t hello_who(mp_obj_t name_in) {
    const char *name;
    name = mp_obj_str_get_str(name_in);
    return mp_obj_new_str(name, strlen(name), false);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(hello_who_obj, hello_who);


STATIC const mp_map_elem_t hello_module_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_hello) },

    { MP_OBJ_NEW_QSTR(MP_QSTR_who),(mp_obj_t)&hello_who_obj},
};

STATIC MP_DEFINE_CONST_DICT(hello_module_globals, hello_module_globals_table);

const mp_obj_module_t mp_module_hello = {
    .base = { &mp_type_module },
    .name = MP_QSTR_hello,
    .globals = (mp_obj_dict_t*)&hello_module_globals,
};
