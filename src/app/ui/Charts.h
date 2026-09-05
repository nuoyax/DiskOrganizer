#pragma once
#include <QWidget>
#include <QList>
#include <QPair>
#include <QString>

class QPaintEvent;

namespace DiskOrganizer {

// 自绘扁平化饼图（图例 + 占比标签）
class PieChart : public QWidget {
    Q_OBJECT
public:
    explicit PieChart(QWidget* parent = nullptr);
    void setData(const QList<QPair<QString, double>>& slices); // 名称 + 数值(自动归一化)

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QList<QPair<QString, double>> m_slices;
    double m_total = 0;
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

} // namespace DiskOrganizer
