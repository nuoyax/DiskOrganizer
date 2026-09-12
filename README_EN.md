# DiskOrganizer

Disk cleanup and organizer for Windows. Qt6 / C++17, default **/MT + static Qt** single-file build, Windows 8+.

## Features

| Module | Description |
|--------|-------------|
| Overview | Storage-pool donut, drive comparison bars, volume table; quick Clean / Analyze |
| Junk Clean | Temp files, recycle bin, browser caches, and more; category cards sync with the detail tree |
| Duplicates | Size pre-filter + content hash; smart selection of redundant copies |
| Big Files | Filter by drive / size / type / age; locked system files grayed out; context menu for folder & details |
| Space Analyzer | Directory breakdown with treemap |
| Defrag | HDD defrag / SSD TRIM with cluster map |

## Scan acceleration

Whole-volume scans prefer NTFS **USN / $MFT** (Everything-style). Fallback order:

1. **MFT / USN fast path** — records with size & mtime; needs admin volume access  
2. **Multi-threaded walk** — first-level shards + `QtConcurrent`  
3. **Big-file filter** — parallel filter + `nth_element` TopN  

Without elevation the app degrades automatically.

## Requirements

- Windows 8+ (x64)
- Build: CMake ≥ 3.21, MSVC, Qt 6.5 (Core / Gui / Widgets / Concurrent / Svg)
- Run as administrator when possible for USN/MFT speedups

## Build

Static Qt (recommended — no Qt/CRT DLLs):

```bash
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<path-to-qt-static>
cmake --build build
```

Shared Qt:

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 \
      -DDISKORGANIZER_STATIC_RUNTIME=OFF \
      -DCMAKE_PREFIX_PATH=<path-to-qt>
cmake --build build --config Release
```

Output: `build/DiskOrganizer.exe`.

## Notes

- Deletes go to the **recycle bin** by default (configurable)
- Locked system files (`pagefile.sys`, `hiberfil.sys`, `swapfile.sys`) cannot be selected for deletion
- Big Files: right-click for open folder, details, copy path, delete one
- Overview: double-click a drive row or use Analyze to open Space Analyzer

## See also

- [CHANGELOG.md](CHANGELOG.md)
- 中文: [README.md](README.md)
