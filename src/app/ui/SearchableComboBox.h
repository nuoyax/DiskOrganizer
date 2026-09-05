#pragma once
#include <QComboBox>
#include <QLineEdit>
#include <QListView>

namespace DiskOrganizer {

// 可搜索下拉框：输入即过滤（不区分大小写、子串匹配），弹出时恢复全部
class SearchableComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit SearchableComboBox(QWidget* parent = nullptr) : QComboBox(parent) {
        setEditable(true);
        setInsertPolicy(QComboBox::NoInsert);
        lineEdit()->setPlaceholderText(QComboBox::tr("输入以筛选…"));
        connect(lineEdit(), &QLineEdit::textEdited, this, &SearchableComboBox::applyFilter);
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

    // 让外部读取"用户输入的过滤词"（未被补全覆盖时 currentText 即输入）
    QString filterText() const { return lineEdit()->text(); }

protected:
    void showPopup() override {
        for (int i = 0; i < count(); ++i)
            if (auto* lv = qobject_cast<QListView*>(view()))
                lv->setRowHidden(i, false);
        QComboBox::showPopup();
        lineEdit()->selectAll();
    }
};

} // namespace DiskOrganizer
