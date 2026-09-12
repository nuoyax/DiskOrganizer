#include "ClusterMapWidget.h"
#include <QPainter>
#include <QRandomGenerator>

namespace DiskOrganizer {

ClusterMapWidget::ClusterMapWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(160);
}

void ClusterMapWidget::setRatios(double sequential, double fragmented, double system,
                                 double free, quint32 seed) {
    const double sum = sequential + fragmented + system + free;
    if (sum <= 0) {
        m_seq = 0.55; m_frag = 0.15; m_sys = 0.05; m_free = 0.25;
    } else {
        m_seq = sequential / sum;
        m_frag = fragmented / sum;
        m_sys = system / sum;
        m_free = free / sum;
    }
    m_seed = seed ? seed : 1;
    update();
}

void ClusterMapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(0xF7, 0xF5, 0xFC));

    const int pad = 8;
    const int cell = 7;
    const int gap = 1;
    const int cols = qMax(8, (width() - pad * 2) / (cell + gap));
    const int rows = qMax(6, (height() - pad * 2) / (cell + gap));
    const int total = cols * rows;

    const int nSeq = int(total * m_seq);
    const int nFrag = int(total * m_frag);
    const int nSys = int(total * m_sys);
    // remainder = free

    QVector<int> kinds(total, 3); // 0 seq, 1 frag, 2 sys, 3 free
    for (int i = 0; i < nSeq && i < total; ++i) kinds[i] = 0;
    for (int i = nSeq; i < nSeq + nFrag && i < total; ++i) kinds[i] = 1;
    for (int i = nSeq + nFrag; i < nSeq + nFrag + nSys && i < total; ++i) kinds[i] = 2;

    // 轻度打散：用 seed 做稳定伪随机交换，模拟簇散布
    QRandomGenerator rng(m_seed);
    for (int i = total - 1; i > 0; --i) {
        const int j = int(rng.bounded(i + 1));
        qSwap(kinds[i], kinds[j]);
    }

    static const QColor colors[] = {
        QColor(0x3B, 0x82, 0xF6), // sequential
        QColor(0xF4, 0x3F, 0x5E), // fragmented
        QColor(0x14, 0xB8, 0xA6), // system/MFT
        QColor(0xE2, 0xE8, 0xF0), // free
    };

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int idx = r * cols + c;
            p.fillRect(pad + c * (cell + gap), pad + r * (cell + gap), cell, cell,
                       colors[kinds[idx]]);
        }
    }
}

} // namespace DiskOrganizer
