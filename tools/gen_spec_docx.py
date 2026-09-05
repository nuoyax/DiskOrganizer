# -*- coding: utf-8 -*-
"""生成《DiskOrganizer 产品规格说明书.docx》"""
from docx import Document
from docx.shared import Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml.ns import qn

doc = Document()

# 全局中文字体
style = doc.styles['Normal']
style.font.name = 'Calibri'
style.font.size = Pt(10.5)
style.element.rPr.rFonts.set(qn('w:eastAsia'), '微软雅黑')

def set_cn(run, size=None, bold=None, color=None):
    run.font.name = 'Calibri'
    run._element.rPr.rFonts.set(qn('w:eastAsia'), '微软雅黑')
    if size: run.font.size = Pt(size)
    if bold is not None: run.font.bold = bold
    if color: run.font.color.rgb = RGBColor(*color)

def h(level, text):
    p = doc.add_heading('', level=level)
    r = p.add_run(text)
    set_cn(r, size={1:16,2:14,3:12}.get(level,11), bold=True, color=(0x1F,0x4E,0x79))
    return p

def para(text, bold=False):
    p = doc.add_paragraph()
    r = p.add_run(text)
    set_cn(r, bold=bold)
    return p

def bullet(text):
    p = doc.add_paragraph(style='List Bullet')
    r = p.add_run(text); set_cn(r)
    return p

def table(headers, rows, widths=None):
    t = doc.add_table(rows=1, cols=len(headers))
    t.style = 'Light Grid Accent 1'
    for i, htxt in enumerate(headers):
        cell = t.rows[0].cells[i]
        cell.text = ''
        r = cell.paragraphs[0].add_run(htxt); set_cn(r, bold=True)
    for row in rows:
        cells = t.add_row().cells
        for i, v in enumerate(row):
            cells[i].text = ''
            r = cells[i].paragraphs[0].add_run(str(v)); set_cn(r)
    return t

# ============ 封面 ============
for _ in range(6): doc.add_paragraph()
p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = p.add_run('DiskOrganizer 磁盘整理助手'); set_cn(r, size=28, bold=True, color=(0x1F,0x4E,0x79))
p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = p.add_run('产品规格说明书'); set_cn(r, size=20, bold=True)
p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = p.add_run('版本 v1.0.0  |  Qt6 + C++17  |  Windows 8+  |  静态编译'); set_cn(r, size=12)
for _ in range(4): doc.add_paragraph()
p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = p.add_run('编制：halo    日期：2026-09-05'); set_cn(r, size=11)
doc.add_page_break()

# ============ 修订历史 ============
h(1, '修订历史')
table(['版本', '日期', '作者', '变更说明'], [['1.0.0', '2026-09-05', 'halo', '初稿：完整功能规格与验收标准']])
doc.add_page_break()

# ============ 1 概述 ============
h(1, '1 概述')
h(2, '1.1 产品定位')
para('DiskOrganizer（磁盘整理助手）是一款面向 Windows 桌面用户的本地磁盘清理与整理工具，'
     '帮助用户可视化分析磁盘空间占用、安全清理垃圾文件、查找并合并重复文件、定位超大文件与老旧文件、'
     '并对机械硬盘执行碎片整理，从而释放空间、提升系统运行效率。')
h(2, '1.2 目标用户')
bullet('普通个人用户：一键清理垃圾、查看磁盘占用排行。')
bullet('进阶用户：重复文件合并、大文件追踪、空间 Treemap 分析。')
bullet('系统维护人员：定期自动清理、导出报告、碎片整理。')
h(2, '1.3 核心价值')
bullet('安全优先：默认删除进回收站；系统关键项标记保护，二次确认。')
bullet('可视化：空间占用一目了然（目录树排行 + 扩展名分布 + Treemap）。')
bullet('高性能：多线程并发扫描，百万级文件秒级响应；取消即时生效。')
bullet('零依赖部署：Qt6 静态链接 + MSVC 静态运行时，单 exe 免安装，兼容 Win8+。')

# ============ 2 运行环境 ============
h(1, '2 运行环境与系统需求')
h(2, '2.1 系统需求')
table(['项目', '最低要求', '推荐配置'],
      [['操作系统', 'Windows 8 / 8.1 / 10 / 11（x64）', 'Windows 10/11 x64'],
       ['CPU', 'x64 双核', '四核及以上'],
       ['内存', '2 GB', '4 GB+'],
       ['磁盘空间', '安装 50 MB（单文件约 20-30 MB）', '—'],
       ['运行库', '无（静态链接）', '无'],
       ['权限', '普通用户（清理系统目录/碎片整理需管理员）', '管理员']])
h(2, '2.2 兼容性约束')
bullet('Win8 兼容：Qt 6.5 为最后一个官方支持 Windows 7/8 的 LTS；本产品锁定 Qt 6.5.x 静态库。')
bullet('静态编译：/MT 运行时 + Qt 静态插件（platforms/qwindows、imageformats、styles），不依赖 vcredist。')
bullet('高 DPI：启用 Qt6 Per-Monitor DPI，兼容 125%/150%/200% 缩放。')
bullet('中文路径/UNC 路径：全 Unicode（QString/UTF-16），支持网络驱动器扫描（只读）。')

# ============ 3 功能规格 ============
h(1, '3 功能规格')
para('产品由 7 大功能模块组成，模块划分如下（F-xx 为功能编号，对应测试用例集）：')
table(['模块', '编号', '名称', '优先级'],
      [['M1', 'F1', '磁盘概览', 'P0'],
       ['M2', 'F2', '垃圾清理', 'P0'],
       ['M3', 'F3', '重复文件查找', 'P0'],
       ['M4', 'F4', '大文件/老旧文件', 'P1'],
       ['M5', 'F5', '空间分析', 'P1'],
       ['M6', 'F6', '碎片整理与优化', 'P2'],
       ['M7', 'F7', '通用功能（设置/报告/关于）', 'P0']])

h(2, '3.1 M1 磁盘概览（F1）')
para('功能描述：启动即列出所有本地与可移动磁盘的容量、已用/可用空间、文件系统类型、使用率色条。')
table(['功能点', '编号', '规格', '验收标准'],
      [['磁盘枚举', 'F1.1', '枚举所有已挂载卷（内置/USB/移动硬盘），显示盘符、卷标、总容量、可用空间、文件系统', '与资源管理器数值一致，误差 < 1%'],
       ['使用率色条', 'F1.2', '使用率 <70% 绿 / 70-90% 黄 / >90% 红', '阈值切换正确刷新'],
       ['双击下钻', 'F1.3', '双击某磁盘跳转至空间分析页并开始扫描该盘', '跳转后扫描自动开始'],
       ['刷新', 'F1.4', '手动刷新 + 热插拔事件自动刷新（WM_DEVICECHANGE）', '插入 U 盘后 3 秒内出现在列表']])
h(3, '3.1.1 业务规则')
bullet('未就绪卷（光驱空盘、断开的网络盘）灰显并标注"不可用"。')
bullet('SSD 磁盘标注 SSD 徽标，引导用户使用"优化"而非"碎片整理"。')

h(2, '3.2 M2 垃圾清理（F2）')
para('功能描述：扫描系统中可安全清理的垃圾文件，分类展示大小，由用户勾选后删除。')
table(['功能点', '编号', '规格', '验收标准'],
      [['垃圾类别扫描', 'F2.1', '支持 12 类：临时文件、回收站、浏览器缓存（Chrome/Edge/Firefox）、系统日志、Windows 更新残留、缩略图缓存、Prefetch、转储文件、安装缓存（谨慎项）、空目录、0 字节文件、自定义规则', '12 类全部可扫描且分类正确'],
       ['分类视图', 'F2.2', '按类别分组列表：路径、大小、安全等级（安全/谨慎）；全选/反选/按类选', '勾选状态与预计释放字节数联动'],
       ['删除方式', 'F2.3', '默认进回收站；可选永久删除；删除前弹确认框显示总计大小', '永久删除需二次确认（输入确认）'],
       ['清理报告', 'F2.4', '完成后显示释放空间、失败项数；失败项可导出 CSV', '数值与删除项总和一致'],
       ['自定义规则', 'F2.5', '用户可添加通配符规则（如 *.tmp、D:/logs/*/*.log），支持启用/禁用', '规则立即生效并持久化'],
       ['系统保护', 'F2.6', 'Windows 目录、Program Files、用户文档默认排除；谨慎项默认不勾选', '即使全选也不触碰保护路径']])
h(3, '3.2.1 业务规则')
bullet('正在被占用的文件跳过并计入失败项，不中断整批删除。')
bullet('回收站清理调用系统 API（IFileOperation），与系统行为一致。')
bullet('扫描与删除均在后台线程执行，UI 可随时取消。')

h(2, '3.3 M3 重复文件查找（F3）')
para('功能描述：在指定目录（或整盘）中找出内容完全相同的文件，支持智能选择保留项并批量处理。')
table(['功能点', '编号', '规格', '验收标准'],
      [['三级比对', 'F3.1', '①按文件大小分组 → ②首 4KB 部分哈希粗筛 → ③全量 SHA-1 确认', '结果零误报（内容逐字节一致）'],
       ['范围选择', 'F3.2', '单目录/多目录/整盘；支持排除目录与最小文件大小阈值（默认 1MB）', '排除目录不出现在结果中'],
       ['分组展示', 'F3.3', '按组折叠展示，每组显示浪费字节数；文件可打开所在位置', '同组文件哈希一致'],
       ['智能选择', 'F3.4', '一键保留策略：最早修改时间 / 最短路径 / 指定目录优先', '每组恰好保留 1 个'],
       ['批量处理', 'F3.5', '删除到回收站 / 永久删除 / 移动到指定目录（合并）', '处理后组内剩余 1 个文件'],
       ['进度取消', 'F3.6', '进度条 + 当前文件路径 + 取消按钮', '取消后 1 秒内停止并保留已完成结果']])

h(2, '3.4 M4 大文件 / 老旧文件（F4）')
table(['功能点', '编号', '规格', '验收标准'],
      [['大文件 Top-N', 'F4.1', '默认 ≥100MB，可自定义阈值；Top 100 排行', '排行按大小降序'],
       ['老旧文件', 'F4.2', '按最后修改时间过滤（如 365 天未访问）', '时间过滤准确'],
       ['类型过滤', 'F4.3', '按扩展名筛选（视频/安装包/压缩包/自定义）', '筛选结果与扩展名一致'],
       ['快捷操作', 'F4.4', '打开位置 / 删除 / 加入排除列表', '操作即时生效']])

h(2, '3.5 M5 空间分析（F5）')
table(['功能点', '编号', '规格', '验收标准'],
      [['目录树排行', 'F5.1', '按目录聚合大小降序排列，可逐层下钻', '聚合值=子孙文件之和'],
       ['扩展名分布', 'F5.2', '按扩展名聚合：大小占比条形图 + 文件数', '占比合计 100%'],
       ['Treemap', 'F5.3', '矩形树图可视化空间占用，点击下钻，悬停显示路径与大小', '面积与大小成正比'],
       ['扫描性能', 'F5.4', '多线程遍历（QtConcurrent），百万文件级别可用；实时显示当前路径与进度', 'UI 无卡顿，取消即时'],
       ['扫描报告', 'F5.5', '导出 TXT/CSV/HTML 报告', '文件可正常打开且数据完整']])

h(2, '3.6 M6 碎片整理与优化（F6）')
table(['功能点', '编号', '规格', '验收标准'],
      [['分析', 'F6.1', '调用 defrag /A 分析碎片率并展示', '与系统 defrag 输出一致'],
       ['整理', 'F6.2', 'HDD 执行 /D 整理，需管理员权限（自动提权 UAC）', '无管理员权限时给出明确提示'],
       ['SSD 优化', 'F6.3', '检测到 SSD 时仅提供"优化（TRIM /O）"，不提供传统整理', 'SSD 盘不出现"整理"按钮'],
       ['进度显示', 'F6.4', '实时显示 defrag 输出与进度', '可中途取消']])

h(2, '3.7 M7 通用功能（F7）')
table(['功能点', '编号', '规格', '验收标准'],
      [['设置-扫描', 'F7.1', '排除目录列表、符号链接跟随开关（默认关）、大文件阈值', '保存后重开生效且持久化'],
      ['设置-清理', 'F7.2', '默认删除方式（回收站/永久）、谨慎项开关', '持久化正确'],
      ['设置-通用', 'F7.3', '语言（简体中文/English）、开机自启、主题（浅色/深色）', '切换后 UI 立即变化'],
      ['报告导出', 'F7.4', '所有列表视图支持导出 TXT/CSV/HTML', '编码 UTF-8，Excel 打开不乱码'],
      ['日志', 'F7.5', '滚动日志文件（%APPDATA%/DiskOrganizer/logs），级别可调', '包含操作审计（删除了什么）'],
      ['关于', 'F7.6', '版本号、开源许可（Qt LGPLv3 声明）', '静态编译的 LGPL 声明完整']])

# ============ 4 非功能 ============
h(1, '4 非功能性需求')
table(['类别', '编号', '需求', '指标'],
      [['性能', 'N1', '10 万文件目录扫描', '≤ 5 s（SSD）'],
       ['性能', 'N2', '百万文件整盘扫描', '≤ 3 min（SSD，多线程）'],
       ['性能', 'N3', 'UI 冷启动', '≤ 2 s'],
       ['性能', 'N4', '扫描期间 CPU 占用', '≤ 80%（可从设置降为后台低优先级）'],
       ['可靠性', 'N5', '删除操作', '默认可恢复（回收站）；审计日志可追溯'],
       ['可靠性', 'N6', '异常退出', '扫描中断后无残留锁文件；下次启动正常'],
       ['安全', 'N7', '系统关键目录', '硬编码保护清单，任何功能不可删除'],
       ['安全', 'N8', '权限最小化', '仅碎片整理/系统级清理请求管理员，其余以普通权限运行'],
       ['易用性', 'N9', '全中文界面', '无未翻译字符串'],
       ['兼容', 'N10', 'Win8 最小支持', '不使用 Win8.1+ 独有 API（或运行时动态加载降级）'],
       ['部署', 'N11', '静态单文件', '不依赖 vcredist / Qt DLL，目标机零安装']])

# ============ 5 约束 ============
h(1, '5 设计与实现约束')
bullet('技术栈：C++17 + Qt 6.5.x（Widgets），CMake ≥3.21，MSVC 2022。')
bullet('静态链接：Qt 静态库（-static）、/MT 运行时、静态插件；开启 release 优化 /O2 + LTO。')
bullet('UI 框架：QMainWindow + QTabWidget 页签式布局；长任务一律后台线程 + 信号槽回报。')
bullet('符合 Qt LGPLv3：以静态链接方式分发时提供目标文件/重链接说明或在官网提供源码链接。')
bullet('禁止行为：不联网上传任何用户数据；不驻留后台进程；不写注册表除设置项与自启项。')

# ============ 6 验收 ============
h(1, '6 验收标准与交付物')
h(2, '6.1 版本发布验收')
bullet('全部 P0 用例通过率 100%，P1 ≥ 95%，P2 ≥ 90%（详见《测试用例.xlsx》）。')
bullet('在 Windows 8.1 / 10 / 11 实机各完成一轮冒烟测试。')
bullet('单 exe 在干净系统（无 vcredist）可直接运行。')
h(2, '6.2 交付物')
table(['交付物', '形式'],
      [['本产品规格说明书', 'docx'],
       ['技术架构书', 'docx'],
       ['测试用例集', 'xlsx'],
       ['源码', 'git 仓库（CMake 工程）'],
       ['发布包', 'DiskOrganizer-1.0.0-win64-static.exe']])

doc.save(r'D:\agent3\docs\DiskOrganizer产品规格说明书.docx')
print('saved')
