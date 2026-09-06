# -*- coding: utf-8 -*-
# 按目录条件（flags&2）统计目录数，与 C++ nodes=5327 对比；并数一下 isDir 记录里 nameNamespace==2 的比例
import ctypes
from ctypes import wintypes
exec(open(r'D:\agent3\DiskOrganizer\tools\verify_mft_read.py', encoding='utf-8').read().split('def main(')[0])
h = open_volume("D:")
vd = NTFS_VOLUME_DATA()
if not k32.DeviceIoControl(h, IOCTL_GET_NTFS_VOL_DATA, None, 0, ctypes.byref(vd), ctypes.sizeof(vd), ctypes.byref(wintypes.DWORD()), None):
    raise OSError(ctypes.get_last_error())
rec_size = vd.BytesPerFileRecordSegment; bpc = vd.BytesPerCluster
total = vd.MftValidDataLength // rec_size
off0 = vd.MftStartLcn * bpc
rec0 = apply_fixup(read_at(h, off0, rec_size))
a = int.from_bytes(rec0[0x14:0x16], "little"); exts = []
o = a
while o + 16 <= rec_size:
    at = int.from_bytes(rec0[o:o+4], "little"); al = int.from_bytes(rec0[o+4:o+8], "little")
    if at == 0xFFFFFFFF or al < 16: break
    if at == 0x80 and rec0[o+8] != 0:
        rlo = int.from_bytes(rec0[o+0x20:o+0x22], "little")
        exts = parse_runlist(rec0[o+rlo:o+al]); break
    o += al
data = bytearray()
for v, l, c in exts:
    data += read_at(h, l * bpc, c * bpc)
data = bytes(data[:total * rec_size])
import sys
dirs = 0; dirs_ns2 = 0; files = 0; posix_skipped_big = 0
# 只统计前 32MB 里的（8192 条记录）以节省时间? 不，全量统计 dirs
for i in range(total):
    rec = data[i*rec_size:(i+1)*rec_size]
    if rec[:4] != b"FILE": continue
    try: rec = apply_fixup(rec)
    except ValueError: continue
    flags = int.from_bytes(rec[0x16:0x18], "little")
    if not (flags & 1): continue
    isdir = bool(flags & 2)
    o = int.from_bytes(rec[0x14:0x16], "little")
    while o + 16 <= rec_size:
        at = int.from_bytes(rec[o:o+4], "little"); al = int.from_bytes(rec[o+4:o+8], "little")
        if at == 0xFFFFFFFF or al < 16 or o + al > rec_size: break
        if at == 0x30 and rec[o+8] == 0 and al >= 0x18 + 66:
            voff = o + 0x18
            ns = rec[voff+65]
            if isdir:
                dirs += 1
                if ns == 2: dirs_ns2 += 1
            else:
                real = int.from_bytes(rec[voff+40:voff+48], "little")
                if ns == 2 and real >= 100*1024*1024:
                    posix_skipped_big += 1
            break
        o += al
print("dirs=", dirs, "dirs_ns2=", dirs_ns2, "posix_ns2_big_files=", posix_skipped_big)
