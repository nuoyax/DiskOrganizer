#pragma once
#include <QIcon>
#include <QPixmap>
#include <QColor>
#include <QPainter>
#include <QSvgRenderer>
#include <QWidget>

namespace DiskOrganizer {

// 内嵌 SVG 图标工具：24x24 path 数据 → 任意颜色/尺寸 QIcon（无外部资源依赖）
namespace Icons {

inline QIcon tinted(const QString& pathData, const QColor& color, int size = 22) {
    const QByteArray svg =
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
        "<path d='" + pathData.toUtf8() + "' fill='" + color.name(QColor::HexRgb).toUtf8() + "'/></svg>";
    QSvgRenderer renderer(svg);
    QPixmap pm(size * 2, size * 2);          // 2x 高清
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p, QRectF(0, 0, pm.width(), pm.height()));
    p.end();
    QIcon ic;
    ic.addPixmap(pm);
    return ic;
}

// Material Symbols 风格 24x24 path
namespace P {
    // 扫描/雷达
    inline const char* scan =
        "M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.42"
        " 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8zm0-12.5c-2.49 0-4.5 2.01-4.5"
        " 4.5s2.01 4.5 4.5 4.5 4.5-2.01 4.5-4.5-2.01-4.5-4.5-4.5z";
    // 扫帚（清理）
    inline const char* broom =
        "M19.36 2.64l1.42 1.42-7.07 7.07-1.41-1.41 7.06-7.08zM11.03 11.03l1.94 1.94-5.13"
        " 5.13c-.7.7-1.7.99-2.84.99-.86 0-1.8-.17-2.77-.51.34.34 2.11 2.11 2.77 2.77 1.14"
        " 0 2.14-.29 2.84-.99l5.13-5.13 1.94 1.94 2.83-2.83-5.66-5.66-2.89 3.35z";
    // 垃圾桶
    inline const char* trash =
        "M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z";
    // 重复文件（双份）
    inline const char* duplicate =
        "M16 1H4c-1.1 0-2 .9-2 2v14h2V3h12V1zm3 4H8c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h11c1.1"
        " 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm0 16H8V7h11v14z";
    // 大文件（文档+放大镜）
    inline const char* bigfile =
        "M14 2H6c-1.1 0-2 .9-2 2v16c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V8l-6-6zm4"
        " 18H6V4h7v5h5v11zm-2.5-4.5c0-1.38-1.12-2.5-2.5-2.5s-2.5 1.12-2.5 2.5 1.12 2.5 2.5 2.5 2.5-1.12 2.5-2.5z";
    // 饼图/空间
    inline const char* pie =
        "M11 2v20c-5.07-.5-9-4.79-9-10s3.93-9.5 9-10zm2.03 0v8.99H22c-.47-4.74-4.24-8.52-8.97-8.99zm0"
        " 11.01V22c4.74-.47 8.5-4.25 8.97-8.99h-8.97z";
    // 磁盘/碎片整理
    inline const char* disk =
        "M6 2h12c1.1 0 2 .9 2 2v16c0 1.1-.9 2-2 2H6c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2zm6"
        " 4a6 6 0 1 0 0 12 6 6 0 0 0 0-12zm0 4a2 2 0 1 1 0 4 2 2 0 0 1 0-4z";
    // 齿轮
    inline const char* settings =
        "M19.43 12.98c.04-.32.07-.64.07-.98s-.03-.66-.07-.98l2.11-1.65c.19-.15.24-.42.12-.64l-2-3.46c-.12-.22-.39-.3-.61-.22l-2.49"
        " 1c-.52-.4-1.08-.73-1.69-.98l-.38-2.65C14.46 2.18 14.25 2 14 2h-4c-.25 0-.46.18-.49.42l-.38 2.65c-.61.25-1.17.59-1.69.98l-2.49-1c-.23-.09-.49"
        " 0-.61.22l-2 3.46c-.13.22-.07.49.12.64l2.11 1.65c-.04.32-.07.65-.07.98s.03.66.07.98l-2.11 1.65c-.19.15-.24.42-.12.64l2 3.46c.12.22.39.3.61.22l2.49-1c.52.4"
        " 1.08.73 1.69.98l.38 2.65c.03.24.24.42.49.42h4c.25 0 .46-.18.49-.42l.38-2.65c.61-.25 1.17-.59 1.69-.98l2.49 1c.23.09.49 0 .61-.22l2-3.46c.12-.22.07-.49-.12-.64l-2.11-1.65zM12"
        " 15.5c-1.93 0-3.5-1.57-3.5-3.5s1.57-3.5 3.5-3.5 3.5 1.57 3.5 3.5-1.57 3.5-3.5 3.5z";
    // 刷新
    inline const char* refresh =
        "M17.65 6.35A7.958 7.958 0 0 0 12 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99"
        " 0 0 1 12 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z";
    // 文件夹
    inline const char* folder =
        "M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z";
    // 勾选
    inline const char* check =
        "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z";
    // 警告
    inline const char* warning =
        "M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z";
    // 硬盘（多盘）
    inline const char* drive =
        "M20 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2zm0"
        " 14H4v-6h16v6zM6 15.5A1.5 1.5 0 1 1 6 12a1.5 1.5 0 0 1 0 3.5z";
    // 火箭（优化）
    inline const char* rocket =
        "M12 2c3 0 7 2 7 10l-3 3-1-4-3 3v6l-2 2-2-6-6-2 2-2h6l3-3-4-1 3-3s2-3 6-3z";
}

} // namespace Icons

} // namespace DiskOrganizer
