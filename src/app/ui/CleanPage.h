#pragma once
#include "PageBase.h"

namespace DiskOrganizer {
class CleanPage : public PageBase {
    Q_OBJECT
public:
    explicit CleanPage(QWidget* parent = nullptr);
};
} // namespace DiskOrganizer
