#include "Charts.h"
#include "util/SizeFormatter.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QPropertyAnimation>
#include <QHoverEvent>
#include <QSizePolicy>
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

QColor diskSegmentColor(int index) {
    // 与 stitch 稿磁盘环形图一致：红/琥珀/青/靛 循环
    static const QColor diskColors[] = {
        QColor(0xEF, 0x44, 0x44), QColor(0xF5, 0x9E, 0x0B),
        QColor(0x14, 0xB8, 0xA6), QColor(0x4F, 0x46, 0xE5),
    };
    return diskColors[index % 4];
}

// ============ PieChart ============
PieChart::PieChart(QWidget* parent) : QWidget(parent) {
    setMinimumSize(280, 200);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
}

void PieChart::setShowLegend(bool show) {
    m_showLegend = show;
    if (show) {
        setMinimumSize(280, 200);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    } else {
        // 仅环形：固定尺寸，绝不抢图例列宽度
        setFixedSize(200, 200);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    updateGeometry();
    update();
}

void PieChart::setData(const QList<QPair<QString, double>>& slices) {
    m_slices = slices;
    m_total = 0;
    for (const auto& s : m_slices) m_total += s.second;
    // 数据更新时环形展开动效（0→1，250ms）
    auto* anim = new QPropertyAnimation(this, "progress", this);
    anim->setDuration(250);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void PieChart::setCenterLabel(const QString& small, const QString& big, const QString& sub) {
    m_cSmall = small;
    m_cBig = big;
    m_cSub = sub;
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
    const double prog = qBound(0.0, m_progress, 1.0);

    int idx = 0;
    for (const auto& slice : m_slices) {
        const double frac = slice.second / m_total;
        const double span = frac * 360 * 16 * prog;
        const bool hovered = (idx == m_hoverSlice);
        p.setPen(Qt::NoPen);
        p.setBrush(DiskOrganizer::diskSegmentColor(idx));
        // 悬停切片外扩 6px 高亮
        if (hovered && prog >= 1.0) {
            const double mid = qDegreesToRadians((startAngle - span / 2) / 16.0);
            const double off = 6.0;
            QRectF hr = pieRect.translated(off * std::cos(mid), -off * std::sin(mid));
            p.drawPie(hr, int(startAngle), int(-span));
        } else {
            p.drawPie(pieRect, int(startAngle), int(-span));
        }
        startAngle -= span;
        ++idx;
    }

    // 中心镂空（甜甜圈效果）— 卡片白底，避免吃到父级灰底发脏
    p.setBrush(QColor(0xFF, 0xFF, 0xFF));
    p.setPen(Qt::NoPen);
    p.drawEllipse(center, radius * 0.62, radius * 0.62);
    // 中心三行文案（剩余可用 / 大数字 / 百分比）随动画淡入
    if (prog > 0.85) {
        p.setOpacity((prog - 0.85) / 0.15);
    p.setPen(QColor(0x6C, 0x7A, 0x77));
    QFont fSmall = font();
    fSmall.setPointSizeF(qMax(7.0, font().pointSizeF() * 0.78));
    p.setFont(fSmall);
    p.drawText(QRectF(center.x() - radius * 0.6, center.y() - 30, radius * 1.2, 16),
               Qt::AlignHCenter | Qt::AlignVCenter, m_cSmall);
    QFont fBig = font();
    fBig.setPointSizeF(qMax(11.0, font().pointSizeF() * 1.45));
    fBig.setBold(true);
    p.setFont(fBig);
    p.setPen(QColor(0x18, 0x14, 0x45));
    p.drawText(QRectF(center.x() - radius * 0.6, center.y() - 14, radius * 1.2, 28),
               Qt::AlignHCenter | Qt::AlignVCenter, m_cBig);
    p.setFont(fSmall);
    p.setPen(QColor(0x00, 0x6B, 0x5F));
    p.drawText(QRectF(center.x() - radius * 0.6, center.y() + 14, radius * 1.2, 16),
               Qt::AlignHCenter | Qt::AlignVCenter, m_cSub);
        p.setOpacity(1.0);
    }

    // 图例（概览页关闭，改用外部 QLabel，避免双份图例）
    if (m_showLegend) {
        const double lx = pieRect.right() + 18;
        double ly = 16;
        idx = 0;
        p.setFont(font());
        for (const auto& slice : m_slices) {
            if (ly > height() - 12) break;
            p.setPen(Qt::NoPen);
            p.setBrush(DiskOrganizer::diskSegmentColor(idx));
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
}

bool PieChart::event(QEvent* ev) {
    // 命中检测：落在环形带内 → 记录切片索引并重绘（外扩高亮）
    if (ev->type() == QEvent::HoverMove || ev->type() == QEvent::HoverEnter) {
        const QPointF pos = static_cast<QHoverEvent*>(ev)->position();
    const QRectF pieRect(10, 10, 190, 190);
    const QPointF c = pieRect.center();
    const double dist = std::hypot(pos.x() - c.x(), pos.y() - c.y());
    int hit = -1;
    if (dist <= pieRect.width() / 2 && dist >= pieRect.width() / 2 * 0.62) {
        // 画布从 90° 顺时针展开，与 paintEvent 一致
        double ang = qRadiansToDegrees(std::atan2(c.y() - pos.y(), pos.x() - c.x()));
        double sweep = 90.0 - ang;
        while (sweep < 0) sweep += 360.0;
        double acc = 0;
        for (int i = 0; i < m_slices.size(); ++i) {
            acc += m_slices[i].second / m_total * 360.0;
            if (sweep <= acc) { hit = i; break; }
        }
    }
    if (hit != m_hoverSlice) {
        m_hoverSlice = hit;
        update();
    }
    }
    return QWidget::event(ev);
}

void PieChart::leaveEvent(QEvent*) {
    if (m_hoverSlice != -1) {
        m_hoverSlice = -1;
        update();
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

// ============ SegmentedBar ============
SegmentedBar::SegmentedBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(12);
    setMinimumWidth(80);
}

void SegmentedBar::setSegments(const QList<QPair<QColor, double>>& segments) {
    m_segs = segments;
    update();
}

void SegmentedBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xE4, 0xE1, 0xF2));
    p.drawRoundedRect(r, 5, 5);

    double total = 0;
    for (const auto& s : m_segs) total += qMax(0.0, s.second);
    if (total <= 0) return;

    QPainterPath clip;
    clip.addRoundedRect(r, 5, 5);
    p.setClipPath(clip);
    double x = r.left();
    for (const auto& s : m_segs) {
        if (s.second <= 0) continue;
        const double w = r.width() * (s.second / total);
        p.setBrush(s.first);
        p.drawRect(QRectF(x, r.top(), w, r.height()));
        x += w;
    }
}

} // namespace DiskOrganizer
