#pragma once
#include "PageBase.h"

namespace DiskOrganizer {
class DefragPage : public PageBase {
    Q_OBJECT
public:
    explicit DefragPage(QWidget* parent = nullptr);
};
} // namespace DiskOrganizer
