# -*- coding: utf-8 -*-
# 用与 C++ 相同的窗口逐块统计：badMagic / fixupFail / unused / files>100MB
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
# 按碎片读全量（一次性，脚本简单）
data = bytearray()
for v, l, c in exts:
    data += read_at(h, l * bpc, c * bpc)
data = bytes(data[:total * rec_size])
print("bytes:", len(data), "expected:", total * rec_size)

# 模拟 C++ parseChunk：按 4096 条一块
import struct
K = 4096
agg = dict(bad=0, fixfail=0, unused=0, noname=0, small=0, big=0, nodes=0)
for base in range(0, total, K):
    chunk = data[base*rec_size:(base+K)*rec_size]
    for i in range(0, len(chunk) - rec_size + 1, rec_size):
        rec = chunk[i:i+rec_size]
        if rec[:4] != b"FILE": agg["bad"] += 1; continue
        # C++ applyFixup(修复后)：check=usa[0]；扇区尾!=check → fail；否则覆盖
        usa_off = int.from_bytes(rec[4:6], "little"); usa_cnt = int.from_bytes(rec[6:8], "little")
        ok = True
        if usa_off < 34 or usa_cnt == 0: ok = False
        else:
            check = int.from_bytes(rec[usa_off:usa_off+2], "little")
            r = bytearray(rec)
            for j in range(1, usa_cnt):
                se = j*512-2
                if se+2 > rec_size: ok = False; break
                at = int.from_bytes(r[se:se+2], "little")
                if at != check: ok = False; break
                r[se:se+2] = r[usa_off+j*2:usa_off+j*2+2]
            rec = bytes(r)
        if not ok: agg["fixfail"] += 1; continue
        flags = int.from_bytes(rec[0x16:0x18], "little")
        if not (flags & 1): agg["unused"] += 1; continue
        isdir = bool(flags & 2)
        # 找 $FILE_NAME
        o = int.from_bytes(rec[0x14:0x16], "little")
        found = False
        while o + 16 <= rec_size:
            at = int.from_bytes(rec[o:o+4], "little"); al = int.from_bytes(rec[o+4:o+8], "little")
            if at == 0xFFFFFFFF or al < 16 or o + al > rec_size: break
            if at == 0x30 and rec[o+8] == 0 and al >= 0x18 + 66:
                voff = o + 0x18
                nlen = rec[voff+64]
                if nlen > 0:
                    real = int.from_bytes(rec[voff+40:voff+48], "little")
                    if isdir:
                        agg["nodes"] += 1; found = True
                    elif real >= 100*1024*1024:
                        agg["big"] += 1; found = True
                    else:
                        agg["small"] += 1; found = True
                break
            o += al
        if not found: agg["noname"] += 1
print(agg)
