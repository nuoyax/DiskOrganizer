#pragma once
#include <QComboBox>
#include <QLineEdit>
#include <QListView>
#include <QAction>
#include <QFontMetrics>
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
            Icons::tinted(QString::fromUtf8(chevronPath), QColor(0x6C, 0x7A, 0x77), 16),
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
        // 弹出列表宽度跟随内容（不截断到控件宽度，限制不超过 560）
        if (QAbstractItemView* v = view()) {
            int contentWidth = 0;
            const QFontMetrics fm(font());
            for (int i = 0; i < count(); ++i)
                contentWidth = qMax(contentWidth,
                    fm.horizontalAdvance(itemText(i)) + iconPad);
            contentWidth += 2 * v->frameWidth() + 8;
            v->setFixedWidth(qMin(qMax(contentWidth, width()), 560));
        }
    }

private:
    inline static const char* chevronPath =
        "M7.41 8.59L12 13.17l4.59-4.58L18 10l-6 6-6-6 1.41-1.41z";
    static constexpr int iconPad = 30;   // 图标 + 文字右缓冲
    QAction* m_arrowAction = nullptr;
};

} // namespace DiskOrganizer
