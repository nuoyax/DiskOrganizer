# -*- coding: utf-8 -*-
# 找 noName 根因：nameNamespace 分布 + nameLen==0 的记录上 $FILE_NAME 的 ns 值
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
for i in range(16):
    data += read_at(h, off0 + i*bpc, bpc)
data = bytes(data)
ns_hist = {}
noname_samples = []
noname_count = 0
for i in range(len(data) // rec_size):
    rec = data[i*rec_size:(i+1)*rec_size]
    if rec[:4] != b"FILE": continue
    try: rec = apply_fixup(rec)
    except ValueError: continue
    flags = int.from_bytes(rec[0x16:0x18], "little")
    if not (flags & 1): continue
    o = int.from_bytes(rec[0x14:0x16], "little")
    got = False
    while o + 16 <= rec_size:
        at = int.from_bytes(rec[o:o+4], "little"); al = int.from_bytes(rec[o+4:o+8], "little")
        if at == 0xFFFFFFFF or al < 16 or o + al > rec_size: break
        if at == 0x30 and rec[o+8] == 0 and al >= 0x18 + 66:
            voff = o + 0x18
            ns = rec[voff+65]
            ns_hist[ns] = ns_hist.get(ns, 0) + 1
            nlen = rec[voff+64]
            if nlen == 0:
                noname_count += 1
                if len(noname_samples) < 5:
                    noname_samples.append((i, ns, al))
            got = True
            break
        o += al
    if not got:
        noname_count += 1
        if len(noname_samples) < 10:
            noname_samples.append((i, "NO_0x30", None))
print("nameNamespace histogram:", ns_hist)
print("noname in first 65536 recs:", noname_count, "samples:", noname_samples)
