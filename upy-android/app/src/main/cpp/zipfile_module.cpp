// upy-android native zipfile module: three functions over java.util.zip,
// see zipfile_help_text below. Not the CPython zipfile API.

#include <cstdlib>
#include <cstring>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"
}

#include "zipfile_jni_bridge.h"
#include "zipfile_module.h"

namespace {

// err is a malloc'd string from zipfile_jni_bridge.cpp: a Java exception
// as text. A missing file gives ENOENT, everything else EIO.
void raise_os_error_free(char *err) {
    bool missing = strstr(err, "FileNotFoundException") != nullptr || strstr(err, "NoSuchFileException") != nullptr;
    int errno_ = missing ? MP_ENOENT : MP_EIO;
    mp_obj_t args[2] = {
        MP_OBJ_NEW_SMALL_INT(errno_),
        mp_obj_new_str(err, strlen(err)),
    };
    free(err);
    nlr_raise(mp_obj_exception_make_new(&mp_type_OSError, 2, 0, args));
}

// zipfile.info(zip) -> [(name, size), ...]
mp_obj_t zipfile_info(mp_obj_t zip_in) {
    const char *zip = mp_obj_str_get_str(zip_in);
    char **entries = nullptr;
    size_t count = 0;
    char *err = nullptr;
    if (!zipfile_bridge_info(zip, &entries, &count, &err)) {
        raise_os_error_free(err);
    }
    mp_obj_t list = mp_obj_new_list(0, nullptr);
    for (size_t i = 0; i < count; i++) {
        char *space = strchr(entries[i], ' ');
        long long size = strtoll(entries[i], nullptr, 10);
        const char *name = space ? space + 1 : "";
        mp_obj_t pair[2] = {
            mp_obj_new_str(name, strlen(name)),
            mp_obj_new_int_from_ll(size),
        };
        mp_obj_list_append(list, mp_obj_new_tuple(2, pair));
        free(entries[i]);
    }
    free(entries);
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(zipfile_info_obj, zipfile_info);

// zipfile.read(zip, dest) -> None
mp_obj_t zipfile_read(mp_obj_t zip_in, mp_obj_t dest_in) {
    char *err = nullptr;
    if (!zipfile_bridge_read(mp_obj_str_get_str(zip_in), mp_obj_str_get_str(dest_in), &err)) {
        raise_os_error_free(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(zipfile_read_obj, zipfile_read);

// zipfile.write(zip, path) -> None
mp_obj_t zipfile_write(mp_obj_t zip_in, mp_obj_t path_in) {
    char *err = nullptr;
    if (!zipfile_bridge_write(mp_obj_str_get_str(zip_in), mp_obj_str_get_str(path_in), &err)) {
        raise_os_error_free(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(zipfile_write_obj, zipfile_write);

const char zipfile_help_text[] =
    "zipfile: read and write zip archives. Three functions, not the CPython zipfile API.\n"
    "  info(zip)         -> [(name, size), ...] in archive order; a folder entry ends in '/'\n"
    "  read(zip, dest)   -> None; extracts every entry into the folder dest, which is created\n"
    "  write(zip, path)  -> None; replaces zip with the file or folder tree path\n"
    "paths: relative to the files folder, like open(); a path that leaves it raises OSError\n"
    "read: an entry name with a leading '/' or a '..' part is refused and nothing is extracted: on Android 14\n"
    "  and newer info and read raise OSError (the archive is refused when opened); on older Android info\n"
    "  lists the names and read raises OSError. A wrong CRC stops read with OSError; entries before it\n"
    "  stay. Existing files are overwritten.\n"
    "write: entry names are relative to the parent of path ('data/a.txt' for write(z, 'data')).\n"
    "  Files only: an empty folder is not stored. The zip itself is skipped. Deflate compression.\n"
    "errors: OSError, ENOENT for a missing file, otherwise EIO with the reason\n"
    "limits: cannot be interrupted; no append, no passwords; times, permissions and links are not kept\n"
;
mp_obj_t zipfile_help() {
    return mp_obj_new_str(zipfile_help_text, strlen(zipfile_help_text));
}
static MP_DEFINE_CONST_FUN_OBJ_0(zipfile_help_obj, zipfile_help);

const mp_rom_map_elem_t zipfile_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_zipfile)},
    {MP_ROM_QSTR(MP_QSTR_info), MP_ROM_PTR(&zipfile_info_obj)},
    {MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&zipfile_read_obj)},
    {MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&zipfile_write_obj)},
    {MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&zipfile_help_obj)},
};
MP_DEFINE_CONST_DICT(zipfile_module_globals, zipfile_module_globals_table);

}  // namespace

extern "C" const mp_obj_module_t zipfile_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *) &zipfile_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_zipfile, zipfile_module);

extern "C" void zipfile_bridge_init(void *jni_env) {
    zipfile_bridge_init_impl(jni_env);
}
