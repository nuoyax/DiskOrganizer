#pragma once
#include <QWidget>

namespace DiskOrganizer {

// 各功能页的通用基类：标题 + 说明 + 主操作区
class PageBase : public QWidget {
    Q_OBJECT
public:
    explicit PageBase(QWidget* parent = nullptr) : QWidget(parent) {}
    virtual ~PageBase() = default;
};

} // namespace DiskOrganizer
