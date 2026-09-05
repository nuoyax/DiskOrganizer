#include "SpaceAnalyzerPage.h"
#include <QLabel>
#include <QVBoxLayout>

namespace DiskOrganizer {
SpaceAnalyzerPage::SpaceAnalyzerPage(QWidget* parent) : PageBase(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("空间分析（目录树 + 文件类型分布 Treemap），待实现")));
}
} // namespace DiskOrganizer
