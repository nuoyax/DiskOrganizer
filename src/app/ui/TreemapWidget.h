#pragma once
#include <QWidget>
#include <QList>
#include <QString>
#include <QColor>
#include <QRectF>

namespace DiskOrganizer {

struct TreemapNode {
    QString path;
    QString label;
    qint64 size = 0;
    QColor color;
    QList<TreemapNode> children;
};

// 自绘矩形树图：按目录大小递归分割，点击下钻
class TreemapWidget : public QWidget {
    Q_OBJECT
public:
    explicit TreemapWidget(QWidget* parent = nullptr);

    void setRoot(const TreemapNode& root);
    void setDepth(int depth); // 展示层数（1–6）
    int depth() const { return m_depth; }
    QString currentPath() const { return m_current.path; }

signals:
    void pathActivated(const QString& path);
    void focusChanged(const QString& path, qint64 size);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Cell {
        QRectF rect;
        const TreemapNode* node = nullptr;
    };

    void layoutCells();
    void layoutSquarified(const TreemapNode& node, const QRectF& rect, int depthLeft,
                          QList<Cell>& out);
    const Cell* hitTest(const QPoint& pos) const;

    TreemapNode m_root;
    TreemapNode m_current;
    int m_depth = 3;
    QList<Cell> m_cells;
    int m_hover = -1;
};

} // namespace DiskOrganizer
