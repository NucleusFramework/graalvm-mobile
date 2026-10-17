# machoplat.py <platform-number> <file.o>...: rewrites LC_BUILD_VERSION in place (2 = iOS, 7 = iOS simulator)
import struct, sys
plat = int(sys.argv[1])
for path in sys.argv[2:]:
    data = bytearray(open(path, 'rb').read())
    magic, _, _, _, ncmds, _, _, _ = struct.unpack_from('<IiiIIIII', data, 0)
    assert magic == 0xfeedfacf, path
    off = 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, off)
        if cmd == 0x32:  # LC_BUILD_VERSION: platform, minos, sdk
            struct.pack_into('<III', data, off + 8, plat, 15 << 16, 18 << 16)
        off += size
    open(path, 'wb').write(data)
