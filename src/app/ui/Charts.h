#pragma once
#include <QWidget>
#include <QList>
#include <QPair>
#include <QString>
#include <QColor>

class QPaintEvent;

namespace DiskOrganizer {

// 磁盘分类色板（概览页环形图/图例共用，与 stitch 稿一致）
QColor diskSegmentColor(int index);

// 自绘扁平化饼图（图例 + 占比标签；数据更新展开动效 + 悬停放大）
class PieChart : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress WRITE setProgress)
public:
    explicit PieChart(QWidget* parent = nullptr);
    void setData(const QList<QPair<QString, double>>& slices); // 名称 + 数值(自动归一化)
    void setCenterLabel(const QString& small, const QString& big, const QString& sub); // 环形中心文案
    void setShowLegend(bool show); // 概览页用外部图例时关闭内置图例，避免重复
    double progress() const { return m_progress; }
    void setProgress(double v) { m_progress = v; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QList<QPair<QString, double>> m_slices;
    double m_total = 0;
    QString m_cSmall, m_cBig, m_cSub; // 中心三行文案
    double m_progress = 1.0;          // 展开动画进度 0→1
    int m_hoverSlice = -1;            // 悬停切片索引
    bool m_showLegend = true;
};

// 水平条形图
class BarChart : public QWidget {
    Q_OBJECT
public:
    explicit BarChart(QWidget* parent = nullptr);
    void setData(const QList<QPair<QString, double>>& bars);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QList<QPair<QString, double>> m_bars;
    double m_max = 0;
};

// 水平分段占比条（清理/分析/碎片页共用）
class SegmentedBar : public QWidget {
    Q_OBJECT
public:
    explicit SegmentedBar(QWidget* parent = nullptr);
    void setSegments(const QList<QPair<QColor, double>>& segments); // 色 + 权重

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QList<QPair<QColor, double>> m_segs;
};

} // namespace DiskOrganizer
