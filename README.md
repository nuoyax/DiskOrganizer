# DiskOrganizer

Windows 磁盘整理与清理工具。Qt6 / C++17，默认 **/MT + 静态 Qt** 打成单文件，兼容 Windows 8+。

## 功能

| 模块 | 说明 |
|------|------|
| 磁盘概览 | 存储池环形图、驱动器空间对比、卷信息表；快捷「清理 / 分析」 |
| 垃圾清理 | 临时文件、回收站、浏览器缓存等分类扫描与清理；分类卡与明细勾选联动 |
| 重复文件 | 按大小预筛 + 内容哈希精确比对，智能勾选冗余副本 |
| 大文件 | 按磁盘 / 体积 / 类型 / 修改时间筛选；系统锁定文件置灰；右键打开目录与详情 |
| 空间分析 | 目录占比、树状图（Treemap）可视化 |
| 碎片整理 | 机械盘整理 / SSD TRIM 优化，簇图状态展示 |

## 扫描加速

整卷扫描优先走 NTFS **USN / $MFT** 直读（接近 Everything 路线），失败时再回退：

1. **MFT / USN 快速路径** — 枚举记录并解析大小、修改时间；需管理员读卷权限  
2. **多线程目录遍历** — 一级子目录分片 + `QtConcurrent`  
3. **大文件过滤** — 并行过滤 + `nth_element` TopN  

无管理员权限时自动降级，不影响基本使用。

## 环境要求

- Windows 8 及以上（x64）
- 构建：CMake ≥ 3.21、MSVC、Qt 6.5（Core / Gui / Widgets / Concurrent / Svg）
- 运行：建议以管理员启动，以启用 USN/MFT 加速

## 构建

本地静态 Qt（推荐，产物无 Qt/CRT DLL）：

```bash
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<Qt静态库路径>
cmake --build build
```

共享 Qt（动态链接，需关闭静态运行时）：

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 \
      -DDISKORGANIZER_STATIC_RUNTIME=OFF \
      -DCMAKE_PREFIX_PATH=<Qt路径>
cmake --build build --config Release
```

默认产物：`build/DiskOrganizer.exe`。

也可使用仓库内的 `build_with_msvc.bat`（需本机已配置 vcvars 与 CMake 路径）。

## 使用说明

- 清理 / 删除默认进入**回收站**（可在设置中调整）
- `pagefile.sys`、`hiberfil.sys`、`swapfile.sys` 等系统锁定文件不可勾选删除
- 大文件列表支持右键：打开所在文件夹、查看详情、复制路径、单独删除等
- 概览表双击磁盘行，或点「分析」，进入空间分析

## 更多

- 版本变更见 [CHANGELOG.md](CHANGELOG.md)
- English: [README_EN.md](README_EN.md)
