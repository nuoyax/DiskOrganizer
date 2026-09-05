#pragma once
#include "PageBase.h"

namespace DiskOrganizer {
class DuplicatePage : public PageBase {
    Q_OBJECT
public:
    explicit DuplicatePage(QWidget* parent = nullptr);
};
} // namespace DiskOrganizer
