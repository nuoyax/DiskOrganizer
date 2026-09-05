#pragma once
#include "PageBase.h"

namespace DiskOrganizer {
class SpaceAnalyzerPage : public PageBase {
    Q_OBJECT
public:
    explicit SpaceAnalyzerPage(QWidget* parent = nullptr);
};
} // namespace DiskOrganizer
