#pragma once
#include <QWidget>

namespace DiskOrganizer {

// 各功能页的通用基类
class PageBase : public QWidget {
    Q_OBJECT
public:
    explicit PageBase(QWidget* parent = nullptr) : QWidget(parent) {}
    virtual ~PageBase() = default;
};

} // namespace DiskOrganizer
