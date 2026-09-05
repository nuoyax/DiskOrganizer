# -*- coding: utf-8 -*-
"""生成《DiskOrganizer 技术架构书.docx》"""
from docx import Document
from docx.shared import Pt, RGBColor, Inches
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml.ns import qn

doc = Document()
style = doc.styles['Normal']
style.font.name = 'Calibri'
style.font.size = Pt(10.5)
style.element.rPr.rFonts.set(qn('w:eastAsia'), '微软雅黑')

def set_cn(run, size=None, bold=None, color=None, mono=False):
    run.font.name = 'Consolas' if mono else 'Calibri'
    run._element.rPr.rFonts.set(qn('w:eastAsia'), '微软雅黑')
    if size: run.font.size = Pt(size)
    if bold is not None: run.font.bold = bold
    if color: run.font.color.rgb = RGBColor(*color)

def h(level, text):
    p = doc.add_heading('', level=level)
    r = p.add_run(text)
    set_cn(r, size={1:16,2:14,3:12}.get(level,11), bold=True, color=(0x1F,0x4E,0x79))

def para(text, bold=False):
    p = doc.add_paragraph(); r = p.add_run(text); set_cn(r, bold=bold); return p

def bullet(text):
    p = doc.add_paragraph(style='List Bullet'); r = p.add_run(text); set_cn(r)

def code(text):
    p = doc.add_paragraph()
    r = p.add_run(text); set_cn(r, size=9, mono=True, color=(0x33,0x33,0x33))
    p.paragraph_format.left_indent = Inches(0.25)

def table(headers, rows):
    t = doc.add_table(rows=1, cols=len(headers)); t.style = 'Light Grid Accent 1'
    for i, htxt in enumerate(headers):
        c = t.rows[0].cells[i]; c.text=''
        r = c.paragraphs[0].add_run(htxt); set_cn(r, bold=True)
    for row in rows:
        cs = t.add_row().cells
        for i, v in enumerate(row):
            cs[i].text=''; r = cs[i].paragraphs[0].add_run(str(v)); set_cn(r)

# 封面
for _ in range(6): doc.add_paragraph()
p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = p.add_run('DiskOrganizer 磁盘整理助手'); set_cn(r, size=28, bold=True, color=(0x1F,0x4E,0x79))
p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = p.add_run('技术架构书'); set_cn(r, size=20, bold=True)
p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = p.add_run('v1.0.0 | Qt6 静态编译 | Win8+ 兼容'); set_cn(r, size=12)
for _ in range(4): doc.add_paragraph()
p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = p.add_run('编制：halo    日期：2026-09-05'); set_cn(r, size=11)
doc.add_page_break()

h(1, '1 架构总览')
h(2, '1.1 技术选型')
table(['项', '选型', '理由'],
      [['语言', 'C++17', '性能与系统 API 访问能力；Qt6 要求 C++17'],
       ['UI 框架', 'Qt 6.5.x Widgets', '6.5 LTS 是最后官方支持 Win7/8 的版本；静态链接成熟'],
       ['构建', 'CMake ≥ 3.21 + MSVC 2022', 'Qt6 官方构建体系；/MP 并行编译'],
       ['并发', 'QtConcurrent + QThread', '扫描/哈希任务并行化'],
       ['持久化', 'QSettings (INI)', '零依赖，免注册表污染（可选注册表后端）'],
       ['哈希', 'QCryptographicHash SHA-1', 'Qt 内置，无需第三方库'],
       ['测试', 'Qt Test (QTest)', '与 Qt 静态链接无额外依赖']])
h(2, '1.2 分层架构')
para('四层架构，依赖方向自上而下单向依赖，禁止反向：')
code('''┌─────────────────────────────────────────────┐
│  UI 层 (src/app/ui)                          │
│  MainWindow / CleanPage / DuplicatePage /    │
│  DefragPage / SpaceAnalyzerPage / Settings   │
├─────────────────────────────────────────────┤
│  应用编排层 (src/app)                        │
│  main.cpp — 页面装配、导航、托盘、提权        │
├─────────────────────────────────────────────┤
│  服务层 (src/services)                       │
│  Scanner / Cleaner / DuplicateFinder /       │
│  BigFileFinder / SpaceAnalyzer / Defrag /    │
│  Settings / Report                           │
├─────────────────────────────────────────────┤
│  模型与工具层 (src/models, src/util)          │
│  FileInfo / DiskItem / SizeFormatter /       │
│  HashUtil / FileSystemUtil                   │
└─────────────────────────────────────────────┘''')
bullet('UI 层只消费服务层信号，不直接做 IO。')
bullet('服务层为纯 QObject + 信号槽，可独立于 UI 做 QTest 单元测试。')
bullet('models/util 为无状态纯函数/纯数据，跨平台可移植。')

h(1, '2 目录结构')
code('''DiskOrganizer/
├── CMakeLists.txt            # 根构建脚本（/MT、_WIN32_WINNT=0x0602）
├── src/
│   ├── CMakeLists.txt        # core 静态库 + app 子目录
│   ├── models/               # FileInfo / DiskItem
│   ├── services/             # 8 个服务（见 §3）
│   ├── util/                 # SizeFormatter / HashUtil / FileSystemUtil
│   └── app/                  # main.cpp + ui/ 各页面
├── tests/                    # QTest 单元/集成测试
├── tools/                    # 文档生成脚本等辅助工具
└── docs/                     # 规格书 / 架构书 / 测试用例''')

h(1, '3 核心模块设计')
h(2, '3.1 ScannerService（扫描引擎）')
bullet('接口：startScan(rootPaths) / cancel()；信号 progress / fileScanned / finished。')
bullet('实现：QtConcurrent::mappedReduced 按一级子目录分片并行遍历；每分片独立 QDirIterator（flat，避免递归栈溢出）。')
bullet('进度：按已遍历条目数/估算总数上报；取消采用原子标志，分片内每 256 项检查一次。')
bullet('健壮性：符号链接默认不跟随（防环）；长路径以 \\\\?\\ 前缀包装支持 >260 字符。')
h(2, '3.2 CleanerService（清理引擎）')
bullet('12 类垃圾目录映射表（%TEMP%、RecycleBin shell API、SoftwareDistribution\\Download 等）。')
bullet('删除走 IFileOperation（COM），天然进回收站并复用系统冲突对话框；永久删除用 QFile::remove 批处理。')
bullet('占用文件：尝试 MOVEFILE_DELAY_UNTIL_REBOOT 记录（可选），否则计入失败项。')
bullet('保护清单硬编码于 ProtectPaths 常量表：Windows、Program Files、ProgramData 关键子树、用户 Documents。')
h(2, '3.3 DuplicateFinder（重复文件引擎）')
bullet('流水线：size 分组 → (size≥阈值) 首部 4KB 部分哈希 → SHA-1 全量哈希 → 组装 DuplicateGroup。')
bullet('每级之间用 QHash<key, QList<index>>，只对候选继续下一级，I/O 最小化。')
bullet('哈希计算并行化：QtConcurrent::blockingMapped + 有界信号量限制并发句柄数（防句柄耗尽）。')
h(2, '3.4 SpaceAnalyzer')
bullet('基于 ScannerService 产出的 FileInfo 列表做内存聚合（祖先目录累加、扩展名桶），纯 CPU 运算，O(N·depth)。')
bullet('Treemap 用 squarified 算法在自绘 QWidget（paintEvent）实现，点击事件命中测试下钻。')
h(2, '3.5 DefragService')
bullet('包装 defrag.exe（/A 分析、/D 整理、/O 优化），QProcess 逐行解析输出汇报进度。')
bullet('SSD 检测：PowerShell Get-PhysicalDisk.MediaType，失败时降级为"未知，按 HDD 处理"。')
bullet('提权：检测 IsUserAnAdmin()；无权限时 ShellExecute runas 重启自身并传 --defrag 参数。')
h(2, '3.6 SettingsService / ReportService')
bullet('设置：QSettings IniFormat（%APPDATA%/DiskOrganizer/DiskOrganizer.ini），结构体 AppSettings 承载。')
bullet('报告：TXT/CSV/HTML 三种模板；UTF-8 带 BOM 导出，确保 Excel 打开中文不乱码。')

h(1, '4 关键技术决策（ADR 摘要）')
table(['编号', '决策', '备选方案', '理由'],
      [['ADR-1', 'Qt 6.5 静态 + /MT 单文件分发', '动态 Qt + 安装包', '满足"静态编译、免依赖"；代价是体积 ~25MB 与 LGPL 义务'],
       ['ADR-2', '锁定 Qt 6.5 LTS 而非 6.7+', '跟随最新 Qt', 'Qt 6.6+ 不再支持 Win7/8；6.5 是兼容上限'],
       ['ADR-3', 'Widgets 而非 QML', 'QML/Quick', 'Win8 上 OpenGL/ANGLE 兼容风险高；Widgets 走 raster 更稳'],
       ['ADR-4', 'IFileOperation 删除', '直接 QFile::remove', '回收站语义、UNC 支持、系统一致的冲突 UI'],
       ['ADR-5', '三级比对去重', '直接全量哈希', 'I/O 量降低 90%+，百万文件场景可用'],
       ['ADR-6', 'defrag.exe 包装而非 DFRG API', '私有接口 IFsrmUtil 等', 'defrag.exe 自 Vista 起稳定，行为与系统一致，兼容 Win8']])

h(1, '5 Win8 兼容与静态编译方案')
h(2, '5.1 编译配置')
code('''# 根 CMakeLists 关键配置
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(_WIN32_WINNT=0x0602 WINVER=0x0602)
# Qt 静态配置（qtbase 配置 -static -static-runtime -opengl desktop）
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
# 静态插件自动引入（qt_import_plugins）：
#   QT_PLUGIN_PLUGIN_TYPE platforms/qwindows.dll
#   imageformats: qico qjpeg  styles: qwindowsvistastyle''')
h(2, '5.2 兼容性清单')
bullet('_WIN32_WINNT=0x0602 全局定义，编译期拦截误用新 API。')
bullet('IFileOperation：Win8 原生支持（Vista+），无需降级 SHFileOperation。')
bullet('高 DPI：Qt6 默认 per-monitor，Win8.1 以下退化为 system DPI，Qt 内部处理。')
bullet('深色主题：Win8 无系统深色，采用应用内 QPalette 自绘方案，不依赖系统。')
bullet('验证矩阵：Win8 x64 / Win8.1 / Win10 21H2 / Win11 各一轮冒烟（对应用例 TC-E2E-xx）。')

h(1, '6 线程模型')
code('''主线程(UI) ──┬── ScannerService   (QtConcurrent 线程池, N-1 workers)
             ├── CleanerService   (单 worker + COM STA 初始化)
             ├── DuplicateFinder  (线程池 + 有界并发哈希)
             └── DefragService    (QProcess + 读取线程)
规则：服务只通过 queued signal 回主线程更新 UI；取消用 std::atomic<bool>。''')

h(1, '7 错误处理与日志')
bullet('错误分级：Info / Warning / Error；qInstallMessageHandler 重定向到滚动日志（10MB × 5 份）。')
bullet('删除类操作写审计日志：时间、操作者操作（回收站/永久）、路径、大小、结果。')
bullet('异常策略：服务层捕获所有异常转为信号 error(code, message)，UI 统一弹窗+日志；不允许异常逃逸到事件循环。')

h(1, '8 测试策略')
table(['层级', '工具', '范围', '通过标准'],
      [['单元', 'QTest', 'util 全部、models 聚合、Cleaner 规则匹配、报告导出', '覆盖率 ≥ 70%（服务层）'],
       ['集成', 'QTest + 临时目录夹具', '扫描→分析→去重→清理 全链路（沙箱目录）', '全部断言通过'],
       ['端到端', '手工冒烟', 'Win8/10/11 实机 + 干净系统单文件运行', 'P0 用例 100%'],
       ['性能', '基准用例', '10 万/百万文件扫描计时', '满足 N1-N3 指标']])

h(1, '9 部署与发布')
bullet('产物：单文件 DiskOrganizer.exe（约 20-30MB，UPX 可选压缩）。')
bullet('签名：发布前用代码签名证书 signcode 签名，避免 SmartScreen 误报。')
bullet('版本信息：rc 文件嵌入 FileVersion/ProductVersion，供"关于"与升级检测。')
bullet('更新策略：v1.x 采用手动下载更新；程序内"检查更新"仅访问固定 URL 的版本清单 JSON，不自动安装。')

doc.save(r'D:\agent3\docs\DiskOrganizer技术架构书.docx')
print('saved')
