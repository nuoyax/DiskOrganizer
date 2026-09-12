#pragma once
#include <QLayout>
#include <QRect>
#include <QStyle>

namespace DiskOrganizer {

// 流式布局：子项按行排列，宽度不足时自动换行（弹性换行）
// 参考 Qt 官方 examples/widgets/layouts/flowlayout
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int margin = 0, int spacing = -1)
        : QLayout(parent) {
        setContentsMargins(margin, margin, margin, margin);
        setSpacing(spacing < 0 ? 8 : spacing);
    }
    explicit FlowLayout(int margin, int hSpacing, int vSpacing)
        : m_hSpace(hSpacing), m_vSpace(vSpacing) {
        setContentsMargins(margin, margin, margin, margin);
    }
    ~FlowLayout() override { QLayoutItem* item; while ((item = takeAt(0))) delete item; }

    void addItem(QLayoutItem* item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem* itemAt(int i) const override { return m_items.value(i); }
    QLayoutItem* takeAt(int i) override { return i >= 0 && i < m_items.size() ? m_items.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }

    // addWidget 由 QLayout::addWidget → addItem 落到 m_items，无需重写

    void setGeometry(const QRect& rect) override {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize size;
        for (const QLayoutItem* item : m_items) size = size.expandedTo(item->minimumSize());
        const QMargins m = contentsMargins();
        size += QSize(m.left() + m.right(), m.top() + m.bottom());
        return size;
    }
    int horizontalSpacing() const { return m_hSpace >= 0 ? m_hSpace : smartSpacing(); }
    int verticalSpacing() const { return m_vSpace >= 0 ? m_vSpace : smartSpacing(); }

private:
    int doLayout(const QRect& rect, bool testOnly) const {
        const QMargins m = contentsMargins();
        const int x0 = rect.x() + m.left();
        const int y0 = rect.y() + m.top();
        int x = x0, y = y0, lineHeight = 0;
        const int hSpace = horizontalSpacing(), vSpace = verticalSpacing();
        for (QLayoutItem* item : m_items) {
            int nextX = x + item->sizeHint().width() + hSpace;
            if (nextX - hSpace > rect.right() - m.right() && lineHeight > 0) {
                x = x0;
                y = y + lineHeight + vSpace;
                nextX = x + item->sizeHint().width() + hSpace;
                lineHeight = 0;
            }
            if (!testOnly) item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
            x = nextX;
            lineHeight = qMax(lineHeight, item->sizeHint().height());
        }
        return y + lineHeight - y0 + m.bottom();
    }
    int smartSpacing() const {
        QWidget* parent = parentWidget();
        return parent ? parent->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, parent) : 6;
    }

    QList<QLayoutItem*> m_items;
    int m_hSpace = -1, m_vSpace = -1;
};

} // namespace DiskOrganizer
