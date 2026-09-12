#include "Charts.h"
#include "util/SizeFormatter.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

namespace DiskOrganizer {

// 扁平化分类色板
static const QColor kPalette[] = {
    QColor(0x4B, 0x41, 0xE1), QColor(0x14, 0xB8, 0xA6), QColor(0xFF, 0xA7, 0x26),
    QColor(0xE5, 0x48, 0x4D), QColor(0x7B, 0x61, 0xFF), QColor(0x2E, 0xB8, 0x59),
    QColor(0xF2, 0x4E, 0x8E), QColor(0x0E, 0xA5, 0xE9), QColor(0x8D, 0x6E, 0x63),
    QColor(0x6B, 0x7F, 0xA8), QColor(0xB4, 0xC6, 0x3C), QColor(0xFF, 0x70, 0x43),
};
static const int kPaletteSize = 12;

// ============ PieChart ============
PieChart::PieChart(QWidget* parent) : QWidget(parent) {
    setMinimumSize(260, 220);
}

void PieChart::setData(const QList<QPair<QString, double>>& slices) {
    m_slices = slices;
    m_total = 0;
    for (const auto& s : m_slices) m_total += s.second;
    update();
}

void PieChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_slices.isEmpty() || m_total <= 0) {
        p.setPen(QColor(0x6C, 0x7A, 0x77));
        p.drawText(rect(), Qt::AlignCenter, tr("暂无数据"));
        return;
    }

    const QRectF pieRect(10, 10, 190, 190);
    const QPointF center = pieRect.center();
    const double radius = pieRect.width() / 2;
    double startAngle = 90 * 16;

    int idx = 0;
    for (const auto& slice : m_slices) {
        const double frac = slice.second / m_total;
        const double span = frac * 360 * 16;
        p.setPen(Qt::NoPen);
        p.setBrush(kPalette[idx % kPaletteSize]);
        p.drawPie(pieRect, int(startAngle), int(-span));
        startAngle -= span;
        ++idx;
    }

    // 中心镂空（甜甜圈效果）
    p.setBrush(palette().window().color());
    p.setPen(Qt::NoPen);
    p.drawEllipse(center, radius * 0.55, radius * 0.55);
    p.setPen(QColor(0x18, 0x14, 0x45));
    p.setFont(font());
    p.drawText(QRectF(center.x() - radius * 0.5, center.y() - 14,
                      radius, 28), Qt::AlignCenter, tr("占比"));

    // 图例
    const double lx = pieRect.right() + 18;
    double ly = 16;
    idx = 0;
    p.setFont(font());
    for (const auto& slice : m_slices) {
        if (ly > height() - 12) break;
        p.setPen(Qt::NoPen);
        p.setBrush(kPalette[idx % kPaletteSize]);
        p.drawRoundedRect(QRectF(lx, ly + 2, 12, 12), 3, 3);
        p.setPen(QColor(0x18, 0x14, 0x45));
        const double pct = slice.second / m_total * 100;
        p.drawText(QRectF(lx + 18, ly - 2, width() - lx - 14, 20),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1  %2%").arg(slice.first).arg(pct, 0, 'f', 1));
        ly += 22;
        ++idx;
    }
}

// ============ BarChart ============
BarChart::BarChart(QWidget* parent) : QWidget(parent) {
    setMinimumSize(260, 180);
}

void BarChart::setData(const QList<QPair<QString, double>>& bars) {
    m_bars = bars;
    m_max = 0;
    for (const auto& b : m_bars) m_max = qMax(m_max, b.second);
    update();
}

void BarChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_bars.isEmpty() || m_max <= 0) {
        p.setPen(QColor(0x6C, 0x7A, 0x77));
        p.drawText(rect(), Qt::AlignCenter, tr("暂无数据"));
        return;
    }

    p.setFont(font());
    const QFontMetrics fm = p.fontMetrics();

    // 值文本宽度（右侧预留）：取最宽值文本
    double valueW = 0;
    for (const auto& b : m_bars)
        valueW = qMax(valueW, double(fm.horizontalAdvance(DiskOrganizer::formatSize(b.second))));
    valueW += 8;

    // 标签宽度：取最宽标签，超长省略，上限 40% 宽度
    double labelW = 0;
    for (const auto& b : m_bars)
        labelW = qMax(labelW, double(fm.horizontalAdvance(b.first)));
    labelW = qMin(labelW + 12, width() * 0.4);
    labelW = qMax(labelW, 60.0);

    const double barAreaRight = width() - valueW - 6;
    const double rowH = double(height() - 8) / m_bars.size();
    const double barH = qMin(rowH * 0.55, 18.0);

    int idx = 0;
    double y = 6;
    for (const auto& bar : m_bars) {
        // 标签超宽省略
        QString label = bar.first;
        if (fm.horizontalAdvance(label) > labelW - 8)
            label = fm.elidedText(label, Qt::ElideMiddle, int(labelW - 8));
        p.setPen(QColor(0x6C, 0x7A, 0x77));
        p.drawText(QRectF(0, y, labelW - 8, rowH),
                   Qt::AlignRight | Qt::AlignVCenter, label);

        const double w = (barAreaRight - labelW) * (bar.second / m_max);
        QColor c(0x4B, 0x41, 0xE1);
        c.setAlphaF(0.35 + 0.65 * (bar.second / m_max));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(labelW, y + (rowH - barH) / 2, qMax(4.0, w), barH), 4, 4);

        p.setPen(QColor(0x18, 0x14, 0x45));
        p.drawText(QRectF(barAreaRight + 6, y, valueW, rowH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   DiskOrganizer::formatSize(bar.second));
        y += rowH;
        ++idx;
    }
}

} // namespace DiskOrganizer
