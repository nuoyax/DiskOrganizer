#pragma once
#include <QComboBox>
#include <QLineEdit>
#include <QListView>
#include <QAction>
#include "Icons.h"

namespace DiskOrganizer {

// 可搜索下拉框：输入即过滤（不区分大小写、子串匹配），弹出时恢复全部。
// 箭头用 QLineEdit trailingAction 绘制（QSS 伪元素箭头在 editable 模式下不可靠）。
class SearchableComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit SearchableComboBox(QWidget* parent = nullptr) : QComboBox(parent) {
        setEditable(true);
        setInsertPolicy(QComboBox::NoInsert);
        lineEdit()->setPlaceholderText(QComboBox::tr("输入以筛选…"));
        lineEdit()->setFrame(false);
        connect(lineEdit(), &QLineEdit::textEdited, this, &SearchableComboBox::applyFilter);

        // 下拉箭头（chevron）作为行编辑的尾部动作，点击 = showPopup
        m_arrowAction = lineEdit()->addAction(
            Icons::tinted(QString::fromUtf8(chevronPath), QColor(0x8A, 0x93, 0xA8), 16),
            QLineEdit::TrailingPosition);
        m_arrowAction->setToolTip(QComboBox::tr("展开"));
        connect(m_arrowAction, &QAction::triggered, this, [this] {
            showPopup();
        });
    }

    void applyFilter(const QString& text) {
        const QString t = text.trimmed();
        for (int i = 0; i < count(); ++i) {
            const bool match = t.isEmpty() || itemText(i).contains(t, Qt::CaseInsensitive)
                || itemData(i).toString().contains(t, Qt::CaseInsensitive);
            if (auto* lv = qobject_cast<QListView*>(view()))
                lv->setRowHidden(i, !match);
        }
    }

    QString filterText() const { return lineEdit()->text(); }

protected:
    void showPopup() override {
        for (int i = 0; i < count(); ++i)
            if (auto* lv = qobject_cast<QListView*>(view()))
                lv->setRowHidden(i, false);
        QComboBox::showPopup();
        lineEdit()->selectAll();
    }

private:
    inline static const char* chevronPath =
        "M7.41 8.59L12 13.17l4.59-4.58L18 10l-6 6-6-6 1.41-1.41z";
    QAction* m_arrowAction = nullptr;
};

} // namespace DiskOrganizer
