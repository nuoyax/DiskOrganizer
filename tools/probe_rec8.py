# -*- coding: utf-8 -*-
# 验证 pathOf：模拟 C++ 输出循环，看 big 文件能否拼出路径（父链是否因剪枝断裂）
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
MIN = 100*1024*1024
# 全量解析（不剪枝），保留 isDir/parent/name/size
recs = {}
for i in range(total):
    rec = data[i*rec_size:(i+1)*rec_size]
    if rec[:4] != b"FILE": continue
    try: rec = apply_fixup(rec)
    except ValueError: continue
    flags = int.from_bytes(rec[0x16:0x18], "little")
    if not (flags & 1): continue
    isdir = bool(flags & 2)
    o = int.from_bytes(rec[0x14:0x16], "little")
    posix = None; entry = None
    while o + 16 <= rec_size:
        at = int.from_bytes(rec[o:o+4], "little"); al = int.from_bytes(rec[o+4:o+8], "little")
        if at == 0xFFFFFFFF or al < 16 or o + al > rec_size: break
        if at == 0x30 and rec[o+8] == 0 and al >= 82:
            voff = o + 0x18
            ns = rec[voff+65]; nlen = rec[voff+64]
            if nlen > 0:
                if ns != 2:
                    entry = (voff, nlen); break
                if posix is None: posix = (voff, nlen)
        o += al
    pick = entry or posix
    if not pick: continue
    voff, nlen = pick
    parent = int.from_bytes(rec[voff:voff+6], "little")
    real = int.from_bytes(rec[voff+40:voff+48], "little")
    name = rec[voff+66:voff+66+nlen*2].decode("utf-16le", "replace")
    recs[i] = (parent, isdir, name, real)
print("parsed nodes:", len(recs))

# 大文件：回溯父链
big_missing = 0; big_ok = 0; missing_parents = {}
def path_of(i):
    chain = []
    cur = i
    while True:
        if cur not in recs:
            return None, chain
        chain.append(cur)
        p = recs[cur][0]
        if p == cur: return None, chain
        cur = p
        if len(chain) > 256: return None, chain
for i, (parent, isdir, name, real) in recs.items():
    if isdir or real < MIN: continue
    root, chain = path_of(i)
    if root is None and chain and chain[-1] != 5:
        # 链顶父不在 recs
        top = chain[-1]
        top_parent = recs[top][0] if top in recs else None
        missing_parents[top] = missing_parents.get(top, 0) + 1
        big_missing += 1
    else:
        big_ok += 1
print("big files:", big_ok + big_missing, "ok=", big_ok, "broken_chain=", big_missing)
print("top missing parents sample:", list(missing_parents.items())[:10])
