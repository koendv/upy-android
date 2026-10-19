// crc.help(). Added to the module by patches/openmv/py_crc.patch.
#include <string.h>
#include "py/obj.h"
#include "py/runtime.h"

static const char crc_help_text[] =
    "module: crc (OpenMV's crc module)\n"
    "functions:\n"
    "  crc16(data, *, value): CRC-16 with polynomial 0xF94F\n"
    "  crc32(data, *, value): CRC-32 with polynomial 0xFA567D89\n"
    "  data: bytes, bytearray or any other buffer, not str; returns an int\n"
    "  value: the result of the previous call, to continue over more data\n"
    "  crc16(b'') is 65535 and crc32(b'') is 4294967295\n"
    "these are not the common CRCs: crc.crc32(b'123456789') is 0x41b403d3, binascii.crc32(b'123456789') is 0xcbf43926\n"
    "see_also: image.help()\n"
;

mp_obj_t crc_help(void) {
    return mp_obj_new_str(crc_help_text, strlen(crc_help_text));
}
MP_DEFINE_CONST_FUN_OBJ_0(crc_help_obj, crc_help);
