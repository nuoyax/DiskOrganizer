#include "SettingsDialog.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

namespace DiskOrganizer {
SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("设置"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("常规 / 排除目录 / 白名单 / 清理策略（待实现）")));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
} // namespace DiskOrganizer
