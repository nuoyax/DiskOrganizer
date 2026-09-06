# -*- coding: utf-8 -*-
# MFT 直读方案验证脚本：不经 $MFT 文件句柄，纯卷句柄读
# 1) FSCTL_GET_NTFS_VOLUME_DATA 拿 MFT 起始 LCN
# 2) 读 record 0，解析 $DATA runlist → 碎片表
# 3) 按碎片表全量读 MFT，统计 FILE 记录 / 目录 / >100MB 文件
# 目标：独立验证 C++ 实现的方案是否可行、数量是否与 PowerShell 一致
import ctypes, sys
from ctypes import wintypes

k32 = ctypes.WinDLL("kernel32", use_last_error=True)

GENERIC_READ = 0x80000000
FILE_SHARE_READ = 1
FILE_SHARE_WRITE = 2
OPEN_EXISTING = 3
FILE_FLAG_NO_BUFFERING = 0x20000000
IOCTL_GET_NTFS_VOL_DATA = 0x00090064
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

# 64 位下必须设 argtypes，否则句柄/指针被截断
k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                            wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD,
                            wintypes.HANDLE]
k32.CreateFileW.restype = wintypes.HANDLE
k32.DeviceIoControl.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID,
                                wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
                                ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
k32.DeviceIoControl.restype = wintypes.BOOL
k32.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                         ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
k32.ReadFile.restype = wintypes.BOOL
k32.SetFilePointer.argtypes = [wintypes.HANDLE, ctypes.c_long,
                               ctypes.POINTER(wintypes.LONG), wintypes.DWORD]
k32.SetFilePointer.restype = wintypes.DWORD

# NTFS_VOLUME_DATA_BUFFER (64-bit pack)
class NTFS_VOLUME_DATA(ctypes.Structure):
    _pack_ = 8
    _fields_ = [("VolumeSerialNumber", ctypes.c_ulonglong),
                ("NumberSectors", ctypes.c_longlong),
                ("TotalClusters", ctypes.c_longlong),
                ("FreeClusters", ctypes.c_longlong),
                ("TotalReserved", ctypes.c_longlong),
                ("BytesPerSector", ctypes.c_ulong),
                ("BytesPerCluster", ctypes.c_ulong),
                ("BytesPerFileRecordSegment", ctypes.c_ulong),
                ("ClustersPerFileRecordSegment", ctypes.c_ulong),
                ("MftValidDataLength", ctypes.c_longlong),
                ("MftStartLcn", ctypes.c_longlong),
                ("Mft2StartLcn", ctypes.c_longlong),
                ("MftZoneStart", ctypes.c_longlong),
                ("MftZoneEnd", ctypes.c_longlong),
                # ReadOnly 字段对（官方结构比 64 位对齐多 8 字节，sizeof 必须 88）
                ("pad", ctypes.c_ulonglong)]

def open_volume(drive):
    path = "\\\\.\\%s:" % drive.rstrip(":")
    h = k32.CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        None, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, None)
    if h == INVALID_HANDLE_VALUE:
        raise OSError("open volume %s err=%d" % (path, ctypes.get_last_error()))
    return h

def read_at(h, offset, size):
    # 对齐：NO_BUFFERING 要求扇区对齐读，多读再切
    sect = 512
    aligned = offset // sect * sect
    extra = int(offset - aligned)
    total = ((extra + size) + sect - 1) // sect * sect
    buf = ctypes.create_string_buffer(total)
    ov = ctypes.c_void_p()  # 简化：同步读（脚本验证用）
    li_low = wintypes.LONG(aligned & 0xFFFFFFFF)
    li_high = wintypes.LONG(aligned >> 32)
    k32.SetFilePointer(h, li_low, ctypes.byref(li_high), 0)
    got = wintypes.DWORD()
    if not k32.ReadFile(h, buf, total, ctypes.byref(got), None):
        raise OSError("read err=%d" % ctypes.get_last_error())
    raw = bytes(buf.raw)[:extra + size]
    return raw

def parse_runlist(run):
    exts = []  # (vcn_off, lcn, clusters)
    vcn = 0; lcn = 0
    i = 0
    while i < len(run) and run[i] != 0:
        header = run[i]; i += 1
        sz_len = header & 0xF; off_len = header >> 4
        if sz_len == 0 or sz_len > 8: break
        clusters = int.from_bytes(run[i:i+sz_len], "little"); i += sz_len
        delta = 0
        if off_len:
            delta = int.from_bytes(run[i:i+off_len], "little", signed=True); i += off_len
        lcn += delta
        if clusters:
            exts.append((vcn, lcn, clusters))
        vcn += clusters
    return exts

def apply_fixup(rec):
    """USA fixup：usa[0]=校验值；每扇区尾 2 字节==校验值，用 usa[i] (i>=1) 覆盖回原始数据"""
    if rec[:4] != b"FILE":
        return rec
    usa_off = int.from_bytes(rec[0x04:0x06], "little")
    usa_cnt = int.from_bytes(rec[0x06:0x08], "little")
    first = int.from_bytes(rec[usa_off:usa_off+2], "little")
    r = bytearray(rec)
    for i in range(1, usa_cnt):
        se = i * 512 - 2
        at = int.from_bytes(r[se:se+2], "little")
        if at != first:
            raise ValueError("fixup mismatch at sector %d" % i)
        r[se:se+2] = r[usa_off+i*2:usa_off+i*2+2]
    return bytes(r)

def main(drive="D:", min_mb=100):
    h = open_volume(drive)
    vd = NTFS_VOLUME_DATA()
    ret = wintypes.DWORD()
    if not k32.DeviceIoControl(h, IOCTL_GET_NTFS_VOL_DATA, None, 0,
                               ctypes.byref(vd), ctypes.sizeof(vd),
                               ctypes.byref(ret), None):
        raise OSError("FSCTL err=%d" % ctypes.get_last_error())
    rec_size = vd.BytesPerFileRecordSegment
    bpc = vd.BytesPerCluster
    total_records = vd.MftValidDataLength // rec_size
    print("MFT start LCN=%d recSize=%d totalRecords=%d bpc=%d" %
          (vd.MftStartLcn, rec_size, total_records, bpc))
    mft_byte_off = vd.MftStartLcn * bpc

    # record 0 → runlist
    rec0 = read_at(h, mft_byte_off, rec_size)
    assert rec0[:4] == b"FILE", "record0 magic bad: %r" % rec0[:4]
    # 应用 USA fixup 后再解析属性（每个扇区尾的原始数据保存在 USA 数组里）
    rec0 = apply_fixup(rec0)
    attr_off = int.from_bytes(rec0[0x14:0x16], "little")
    exts = []
    off = attr_off
    while off + 16 <= rec_size:
        atype = int.from_bytes(rec0[off:off+4], "little")
        alen = int.from_bytes(rec0[off+4:off+8], "little")
        if atype == 0xFFFFFFFF or alen < 16: break
        if atype == 0x80 and rec0[off+8] != 0:  # $DATA non-resident（nonres=1）
            rlo = int.from_bytes(rec0[off+0x20:off+0x22], "little")
            exts = parse_runlist(rec0[off+rlo:off+alen])
            break
        off += alen
    print("extents=%d totalClusters=%d" % (len(exts), sum(e[2] for e in exts)))

    # 全量按碎片读 + 解析
    # record header: flags@0x16(2) attrOff@0x14(2)
    n_file = n_dir = n_big = n_badmagic = n_unused = 0
    big_names = []
    mft_data = bytearray()
    for vcn_off, lcn, clusters in exts:
        mft_data += read_at(h, lcn * bpc, clusters * bpc)
    mft_data = bytes(mft_data[:total_records * rec_size])
    print("read %d bytes" % len(mft_data))
    for i in range(total_records):
        rec = mft_data[i*rec_size:(i+1)*rec_size]
        if rec[:4] != b"FILE":
            n_badmagic += 1; continue
        flags = int.from_bytes(rec[0x16:0x18], "little")
        if not (flags & 1): n_unused += 1; continue
        if flags & 2: n_dir += 1; continue
        n_file += 1
        # 找 $FILE_NAME 的 realSize（value off 0x18 + 40）
        aoff = int.from_bytes(rec[0x14:0x16], "little")
        o = aoff
        while o + 16 <= rec_size:
            atype = int.from_bytes(rec[o:o+4], "little")
            alen = int.from_bytes(rec[o+4:o+8], "little")
            if atype == 0xFFFFFFFF or alen < 16: break
            if atype == 0x30 and rec[o+8] == 0:
                real = int.from_bytes(rec[o+0x18+40:o+0x18+48], "little")
                if real >= min_mb * 1024 * 1024:
                    n_big += 1
                    if len(big_names) < 5:
                        nl = rec[o+0x18+64]
                        no = o+0x18+66
                        big_names.append(rec[no:no+nl*2].decode("utf-16le", "replace"))
                break
            o += alen
    print("files=%d dirs=%d big(>%dMB)=%d badmagic=%d unused=%d" %
          (n_file, n_dir, min_mb, n_big, n_badmagic, n_unused))
    print("sample big:", big_names)

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "D:",
         int(sys.argv[2]) if len(sys.argv) > 2 else 100)
