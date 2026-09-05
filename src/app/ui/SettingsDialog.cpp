#include "SettingsDialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace DiskOrganizer {


SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("设置"));
    resize(480, 400);
    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(tr("扫描排除目录："), this));
    m_excludeList = new QListWidget(this);
    layout->addWidget(m_excludeList);
    auto* exRow = new QHBoxLayout;
    m_excludeEdit = new QLineEdit(this);
    auto* browse = new QPushButton(tr("…"), this);
    browse->setFixedWidth(32);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("排除目录"));
        if (!dir.isEmpty()) m_excludeEdit->setText(dir);
    });
    auto* addBtn = new QPushButton(tr("添加"), this);
    auto* delBtn = new QPushButton(tr("移除"), this);
    connect(addBtn, &QPushButton::clicked, this, &SettingsDialog::addExclude);
    connect(delBtn, &QPushButton::clicked, this, &SettingsDialog::removeExclude);
    exRow->addWidget(m_excludeEdit, 1);
    exRow->addWidget(browse);
    exRow->addWidget(addBtn);
    exRow->addWidget(delBtn);
    layout->addLayout(exRow);

    m_recycleCheck = new QCheckBox(tr("清理默认删除到回收站"), this);
    m_symlinkCheck = new QCheckBox(tr("跟随符号链接（不推荐：可能死循环）"), this);
    layout->addWidget(m_recycleCheck);
    layout->addWidget(m_symlinkCheck);

    // 载入
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "DiskOrganizer", "DiskOrganizer");
    m_excludeList->addItems(s.value("scan/excludePaths").toStringList());
    m_recycleCheck->setChecked(s.value("clean/toRecycleBin", true).toBool());
    m_symlinkCheck->setChecked(s.value("scan/followSymlinks", false).toBool());

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::save() {
    QStringList excludes;
    for (int i = 0; i < m_excludeList->count(); ++i)
        excludes << m_excludeList->item(i)->text();
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "DiskOrganizer", "DiskOrganizer");
    s.setValue("scan/excludePaths", excludes);
    s.setValue("clean/toRecycleBin", m_recycleCheck->isChecked());
    s.setValue("scan/followSymlinks", m_symlinkCheck->isChecked());
    s.sync();
    accept();
}

void SettingsDialog::addExclude() {
    const QString dir = m_excludeEdit->text().trimmed();
    if (dir.isEmpty()) return;
    if (!m_excludeList->findItems(dir, Qt::MatchExactly).isEmpty()) return; // 去重
    m_excludeList->addItem(dir);
    m_excludeEdit->clear();
}

void SettingsDialog::removeExclude() {
    const auto selected = m_excludeList->selectedItems();
    for (auto* item : selected) delete item;
}

} // namespace DiskOrganizer

void showSettingsDialog(QWidget* parent) {
    DiskOrganizer::SettingsDialog dlg(parent);
    dlg.exec();
}
