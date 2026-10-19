# test: required
#
# crc.help() and the statements in it that a script can check.
import binascii

import crc


def raises(exception, call):
    try:
        call()
    except exception:
        return True
    return False


text = crc.help()
print("help:", text.startswith("module: crc"))
print("empty data:", crc.crc16(b"") == 65535 and crc.crc32(b"") == 4294967295)
print("crc32 is not binascii.crc32:", crc.crc32(b"123456789") == 0x41b403d3 and binascii.crc32(b"123456789") == 0xcbf43926)
print("crc16 of 123456789:", crc.crc16(b"123456789") == 0xe541)
print("bytearray works:", crc.crc16(bytearray(b"123456789")) == crc.crc16(b"123456789"))
print("value continues crc16:", crc.crc16(b"456789", value=crc.crc16(b"123")) == crc.crc16(b"123456789"))
print("value continues crc32:", crc.crc32(b"456789", value=crc.crc32(b"123")) == crc.crc32(b"123456789"))
print("results are int:", type(crc.crc16(b"a")) is int and type(crc.crc32(b"a")) is int)
print("data is required:", raises(TypeError, lambda: crc.crc16()))
