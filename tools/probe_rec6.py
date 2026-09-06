# -*- coding: utf-8 -*-
# 精确复刻 C++ parseChunk 的属性遍历（AttributeHeader 是 16 字节，门槛 length >= 16 + sizeof(FileNameAttribute)=66 → 82）
# C++ 在 nameNamespace==2 时不 break，继续遍历下一个属性 → 有 DOS 名的记录没问题，
# 但只有 POSIX 名的记录 n.nameLen==0 → noName++（目录也被丢！→ 父链断裂 → 5327 nodes）
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

# C++ 风格解析（ns!=2 才收） vs 修正（任何 ns 都收，POSIX 优先回退 DOS）
def cpp_style(data):
    nodes = 0; noname = 0
    for i in range(total):
        rec = data[i*rec_size:(i+1)*rec_size]
        if rec[:4] != b"FILE": continue
        try: rec = apply_fixup(rec)
        except ValueError: continue
        flags = int.from_bytes(rec[0x16:0x18], "little")
        if not (flags & 1): continue
        o = int.from_bytes(rec[0x14:0x16], "little")
        nlen = 0
        while o + 16 <= rec_size:
            at = int.from_bytes(rec[o:o+4], "little"); al = int.from_bytes(rec[o+4:o+8], "little")
            if at == 0xFFFFFFFF or al < 16 or o + al > rec_size: break
            if at == 0x30 and rec[o+8] == 0 and al >= 82:
                voff = o + 0x18
                if rec[voff+65] != 2:
                    nlen = rec[voff+64]
                break   # C++ 取第一个 0x30 后 break（不管 ns 是否 2）
            o += al
        if nlen == 0: noname += 1
        else: nodes += 1
    return nodes, noname

def fixed_style(data):
    nodes = 0; noname = 0
    for i in range(total):
        rec = data[i*rec_size:(i+1)*rec_size]
        if rec[:4] != b"FILE": continue
        try: rec = apply_fixup(rec)
        except ValueError: continue
        flags = int.from_bytes(rec[0x16:0x18], "little")
        if not (flags & 1): continue
        o = int.from_bytes(rec[0x14:0x16], "little")
        best = None  # (ns, nlen, off)
        fallback = None
        while o + 16 <= rec_size:
            at = int.from_bytes(rec[o:o+4], "little"); al = int.from_bytes(rec[o+4:o+8], "little")
            if at == 0xFFFFFFFF or al < 16 or o + al > rec_size: break
            if at == 0x30 and rec[o+8] == 0 and al >= 82:
                voff = o + 0x18
                ns = rec[voff+65]; nlen = rec[voff+64]
                if nlen > 0:
                    if ns != 2:
                        best = (ns, nlen, voff); break
                    elif fallback is None:
                        fallback = (ns, nlen, voff)
            o += al
        pick = best or fallback
        if pick: nodes += 1
        else: noname += 1
    return nodes, noname

n1, nn1 = cpp_style(data)
print("cpp_style: nodes=", n1, "noname=", nn1)
n2, nn2 = fixed_style(data)
print("fixed_style: nodes=", n2, "noname=", nn2)
