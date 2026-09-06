# -*- coding: utf-8 -*-
# 看 record 12-15（meta：$Volume/$AttrDef 等）：属性从哪开始，0x30 在哪
import ctypes
from ctypes import wintypes
exec(open(r'D:\agent3\DiskOrganizer\tools\verify_mft_read.py', encoding='utf-8').read().split('def main(')[0])
h = open_volume("D:")
vd = NTFS_VOLUME_DATA()
if not k32.DeviceIoControl(h, IOCTL_GET_NTFS_VOL_DATA, None, 0, ctypes.byref(vd), ctypes.sizeof(vd), ctypes.byref(wintypes.DWORD()), None):
    raise OSError(ctypes.get_last_error())
rec_size = vd.BytesPerFileRecordSegment; bpc = vd.BytesPerCluster
off0 = vd.MftStartLcn * bpc
for idx in (12,):
    rec = read_at(h, off0 + idx * rec_size, rec_size)
    if rec[:4] != b"FILE":
        print(idx, "bad magic", rec[:4]); continue
    rec = apply_fixup(rec)
    flags = int.from_bytes(rec[0x16:0x18], "little")
    print("rec", idx, "flags=", flags, "attrOff=", int.from_bytes(rec[0x14:0x16], "little"))
    o = int.from_bytes(rec[0x14:0x16], "little")
    while o + 16 <= rec_size:
        at = int.from_bytes(rec[o:o+4], "little"); al = int.from_bytes(rec[o+4:o+8], "little")
        if at == 0xFFFFFFFF or al < 16: break
        print("  type=%08x len=%d nonres=%d off=%d" % (at, al, rec[o+8], o))
        o += al
