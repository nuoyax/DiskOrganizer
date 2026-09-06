# -*- coding: utf-8 -*-
import ctypes
from ctypes import wintypes
exec(open(r'D:\agent3\DiskOrganizer\tools\verify_mft_read.py', encoding='utf-8').read().split('def main(')[0])
h = open_volume("D:")
vd = NTFS_VOLUME_DATA()
if not k32.DeviceIoControl(h, IOCTL_GET_NTFS_VOL_DATA, None, 0, ctypes.byref(vd), ctypes.sizeof(vd), ctypes.byref(wintypes.DWORD()), None):
    raise OSError(ctypes.get_last_error())
off = vd.MftStartLcn * vd.BytesPerCluster
print("mft byte off:", off)
raw = read_at(h, off, 1024)
print("len:", len(raw))
print("first 128:", raw[:128].hex(" "))
r = apply_fixup(raw)
print("after fixup, attrOff:", int.from_bytes(r[0x14:0x16], "little"))
o = int.from_bytes(r[0x14:0x16], "little")
for _ in range(16):
    at = int.from_bytes(r[o:o+4], "little"); al = int.from_bytes(r[o+4:o+8], "little")
    if at == 0xFFFFFFFF or al < 4: break
    print("type=%08x len=%d nonres=%d" % (at, al, r[o+8]))
    if at == 0x80:
        print("DATA hdr:", r[o:o+48].hex(" "))
        rlo = int.from_bytes(r[o+0x20:o+0x22], "little")
        print("runlistOff:", rlo, "runlist:", r[o+rlo:o+al].hex(" "))
    o += al
