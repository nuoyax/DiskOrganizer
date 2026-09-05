#include "DuplicatePage.h"
#include <QLabel>
#include <QVBoxLayout>

namespace DiskOrganizer {
DuplicatePage::DuplicatePage(QWidget* parent) : PageBase(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("重复文件查找，待实现")));
}
} // namespace DiskOrganizer
