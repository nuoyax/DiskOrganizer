#pragma once
#include <QWidget>

namespace DiskOrganizer {

// 近似簇分布图：按顺序/碎片/系统/空闲比例填充色块网格（非真实物理簇）
class ClusterMapWidget : public QWidget {
    Q_OBJECT
public:
    explicit ClusterMapWidget(QWidget* parent = nullptr);

    // 四段占比 0–1，自动归一化；seed 影响伪随机布局
    void setRatios(double sequential, double fragmented, double system, double free,
                   quint32 seed = 1);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double m_seq = 0.55;
    double m_frag = 0.15;
    double m_sys = 0.05;
    double m_free = 0.25;
    quint32 m_seed = 1;
};

} // namespace DiskOrganizer
