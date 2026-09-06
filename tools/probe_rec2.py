# -*- coding: utf-8 -*-
# 检查 $FILE_NAME 属性长度与 FileNameAttribute 布局：C++ 的 attr->length >= sizeof(AttributeHeader)+sizeof(FileNameAttribute) 门槛是否过严
import ctypes
from ctypes import wintypes
exec(open(r'D:\agent3\DiskOrganizer\tools\verify_mft_read.py', encoding='utf-8').read().split('def main(')[0])
h = open_volume("D:")
vd = NTFS_VOLUME_DATA()
if not k32.DeviceIoControl(h, IOCTL_GET_NTFS_VOL_DATA, None, 0, ctypes.byref(vd), ctypes.sizeof(vd), ctypes.byref(wintypes.DWORD()), None):
    raise OSError(ctypes.get_last_error())
rec_size = vd.BytesPerFileRecordSegment; bpc = vd.BytesPerCluster
off0 = vd.MftStartLcn * bpc
data = bytearray()
# 只读前 64MB（前 16 个簇=65536 条记录）足够统计
for i in range(16):
    data += read_at(h, off0 + i*bpc, bpc)
data = bytes(data)
lens = {}
short = 0
for i in range(len(data) // rec_size):
    rec = data[i*rec_size:(i+1)*rec_size]
    if rec[:4] != b"FILE": continue
    try: rec = apply_fixup(rec)
    except ValueError: continue
    flags = int.from_bytes(rec[0x16:0x18], "little")
    if not (flags & 1): continue
    o = int.from_bytes(rec[0x14:0x16], "little")
    while o + 16 <= rec_size:
        at = int.from_bytes(rec[o:o+4], "little"); al = int.from_bytes(rec[o+4:o+8], "little")
        if at == 0xFFFFFFFF or al < 16 or o + al > rec_size: break
        if at == 0x30 and rec[o+8] == 0:
            lens[al] = lens.get(al, 0) + 1
            # value 起始：属性头其实是 0x18？dump rec0 里 0x30 的 value 起始看起来在 +0x18
            if al < 0x18 + 66: short += 1
            break
        o += al
print("FILE_NAME attr length histogram:", sorted(lens.items()))
print("short(<0x5A):", short)
