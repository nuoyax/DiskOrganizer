#include "DefragPage.h"
#include <QLabel>
#include <QVBoxLayout>

namespace DiskOrganizer {
DefragPage::DefragPage(QWidget* parent) : PageBase(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("碎片整理（调用 Windows Defrag API），待实现")));
}
} // namespace DiskOrganizer
