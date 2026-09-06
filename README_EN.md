# DiskOrganizer — Disk Cleanup & Organizer

Qt6 / C++17 / MSVC /MT fully-static single executable, Windows 8+.

## Features

| Module | Description |
|--------|-------------|
| Overview | Disk pie chart + space bar chart + drive info table |
| Junk Clean | 12 junk categories (temp files / caches / recycle bin…), checkboxes, live pie chart |

| Duplicates | Size pre-filter + exact hash comparison |

| Big Files | Scan by drive / size threshold / extension group; searchable combo with 12 file-type groups; small files & small dirs pruned for speed |

| Space Analyzer | Directory tree breakdown, colored treemap |

| Defrag | HDD defrag / SSD TRIM optimize |

## Scan Acceleration Architecture (3 tiers)

1. **USN Journal / MFT fast path**: whole-volume NTFS scans use `FSCTL_ENUM_USN_DATA` to enumerate MFT records directly (same route as Everything) — a 1M-file volume in ~1-3 s; FRN→path resolution is cached. Falls back automatically on non-NTFS volumes or missing privileges.
2. **Multi-threaded parallel traversal** (fallback): first-level subdirectories sharded + `QtConcurrent::blockingMapped`, one thread per core, atomic progress counters, instant cancellation.
3. **Big-file filter acceleration**: chunked parallel filtering + `std::nth_element` TopN selection (O(n)).

> Note: whole-volume scans read the raw $MFT and parse the `$FILE_NAME` attribute first, **providing both file size and modification time**; on failure it falls back to pure USN enumeration (no size/mtime), then to parallel traversal.

## Build

Requirements: CMake ≥ 3.21, MSVC, Qt 6.5 static libs (Core/Gui/Widgets/Concurrent/Svg).

```bash
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<path-to-qt-static>
cmake --build build
```

Output: `build/DiskOrganizer.exe` — **zero Qt / CRT DLL dependencies** (`/MT` runtime + static Qt); copy to any Win8+ machine and run.

## Notes

- The USN fast path requires **administrator privileges** (volume handle access); it silently falls back to parallel traversal otherwise.
- Delete/clean operations go to the recycle bin (permanent delete configurable).
