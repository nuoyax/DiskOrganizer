#include "TreemapWidget.h"
#include "util/SizeFormatter.h"
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace DiskOrganizer {

namespace {
QColor paletteAt(int i) {
    static const QColor colors[] = {
        QColor(0x8B, 0x5C, 0xF6), QColor(0xF4, 0x3F, 0x5E), QColor(0x10, 0xB9, 0x81),
        QColor(0x0E, 0xA5, 0xE9), QColor(0x14, 0xB8, 0xA6), QColor(0xF5, 0x9E, 0x0B),
        QColor(0x4F, 0x46, 0xE5), QColor(0xEC, 0x48, 0x99),
    };
    return colors[i % 8];
}
} // namespace

TreemapWidget::TreemapWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 240);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

void TreemapWidget::setRoot(const TreemapNode& root) {
    m_root = root;
    m_current = root;
    layoutCells();
    update();
    emit focusChanged(m_current.path, m_current.size);
}

void TreemapWidget::setDepth(int depth) {
    m_depth = qBound(1, depth, 6);
    layoutCells();
    update();
}

void TreemapWidget::layoutCells() {
    m_cells.clear();
    if (m_current.size <= 0 && m_current.children.isEmpty()) return;
    layoutSquarified(m_current, QRectF(rect()).adjusted(4, 4, -4, -4), m_depth, m_cells);
}

void TreemapWidget::layoutSquarified(const TreemapNode& node, const QRectF& rect,
                                    int depthLeft, QList<Cell>& out) {
    if (rect.width() < 4 || rect.height() < 4) return;

    QList<const TreemapNode*> kids;
    qint64 sum = 0;
    for (const auto& c : node.children) {
        if (c.size <= 0) continue;
        kids.append(&c);
        sum += c.size;
    }
    if (kids.isEmpty() || depthLeft <= 1 || sum <= 0) {
        Cell cell;
        cell.rect = rect;
        cell.node = &node;
        out.append(cell);
        return;
    }

    std::sort(kids.begin(), kids.end(),
              [](const TreemapNode* a, const TreemapNode* b) { return a->size > b->size; });

    const bool horizontal = rect.width() >= rect.height();
    double offset = 0;
    const double span = horizontal ? rect.width() : rect.height();
    for (const TreemapNode* kid : kids) {
        const double frac = double(kid->size) / double(sum);
        const double slice = span * frac;
        QRectF childRect;
        if (horizontal)
            childRect = QRectF(rect.x() + offset, rect.y(), slice, rect.height()).adjusted(1, 1, -1, -1);
        else
            childRect = QRectF(rect.x(), rect.y() + offset, rect.width(), slice).adjusted(1, 1, -1, -1);
        offset += slice;
        layoutSquarified(*kid, childRect, depthLeft - 1, out);
    }
}

const TreemapWidget::Cell* TreemapWidget::hitTest(const QPoint& pos) const {
    for (int i = m_cells.size() - 1; i >= 0; --i) {
        if (m_cells[i].rect.contains(pos)) return &m_cells[i];
    }
    return nullptr;
}

void TreemapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0xF7, 0xF5, 0xFC));

    if (m_cells.isEmpty()) {
        p.setPen(QColor(0x6C, 0x7A, 0x77));
        p.drawText(rect(), Qt::AlignCenter, tr("扫描后显示空间层级图"));
        return;
    }

    for (int i = 0; i < m_cells.size(); ++i) {
        const Cell& cell = m_cells[i];
        if (!cell.node) continue;
        QColor fill = cell.node->color.isValid() ? cell.node->color : paletteAt(i);
        if (i == m_hover) fill = fill.lighter(112);
        p.setPen(QPen(QColor(0xFF, 0xFF, 0xFF), 1.5));
        p.setBrush(fill);
        p.drawRoundedRect(cell.rect, 6, 6);

        if (cell.rect.width() < 48 || cell.rect.height() < 28) continue;
        p.setPen(Qt::white);
        QFont f = font();
        f.setBold(true);
        f.setPointSizeF(qMax(8.0, font().pointSizeF() * 0.95));
        p.setFont(f);
        const QString label = cell.node->label.isEmpty()
            ? QFileInfo(cell.node->path).fileName()
            : cell.node->label;
        p.drawText(cell.rect.adjusted(8, 6, -8, -6),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   label);
        if (cell.rect.height() > 42) {
            QFont fs = font();
            fs.setPointSizeF(qMax(7.5, font().pointSizeF() * 0.85));
            p.setFont(fs);
            p.setPen(QColor(255, 255, 255, 220));
            p.drawText(cell.rect.adjusted(8, 22, -8, -6),
                       Qt::AlignLeft | Qt::AlignTop,
                       formatSize(cell.node->size));
        }
    }
}

void TreemapWidget::mousePressEvent(QMouseEvent* event) {
    const Cell* hit = hitTest(event->pos());
    if (!hit || !hit->node) return;
    // 有子节点则下钻，否则发激活信号
    if (!hit->node->children.isEmpty() && hit->node->path != m_current.path) {
        m_current = *hit->node;
        layoutCells();
        update();
        emit focusChanged(m_current.path, m_current.size);
    }
    emit pathActivated(hit->node->path);
}

void TreemapWidget::mouseMoveEvent(QMouseEvent* event) {
    int idx = -1;
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].rect.contains(event->pos())) { idx = i; break; }
    }
    if (idx != m_hover) {
        m_hover = idx;
        update();
        if (idx >= 0 && m_cells[idx].node) {
            QToolTip::showText(event->globalPosition().toPoint(),
                QString("%1\n%2").arg(m_cells[idx].node->path,
                                      formatSize(m_cells[idx].node->size)), this);
        }
    }
}

void TreemapWidget::leaveEvent(QEvent*) {
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
}

} // namespace DiskOrganizer
