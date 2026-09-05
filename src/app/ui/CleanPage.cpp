#include "CleanPage.h"
#include <QLabel>
#include <QVBoxLayout>

namespace DiskOrganizer {
CleanPage::CleanPage(QWidget* parent) : PageBase(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("垃圾清理（扫描可清理项 → 选择 → 删除），待实现")));
}
} // namespace DiskOrganizer
