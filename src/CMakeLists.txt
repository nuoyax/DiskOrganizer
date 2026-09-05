qt_add_library(diskorganizer_core STATIC
    models/FileInfo.h
    models/FileInfo.cpp
    models/DiskItem.h
    services/ScannerService.h
    services/ScannerService.cpp
    services/CleanerService.h
    services/CleanerService.cpp
    services/DuplicateFinder.h
    services/DuplicateFinder.cpp
    services/BigFileFinder.h
    services/BigFileFinder.cpp
    services/DefragService.h
    services/DefragService.cpp
    services/SpaceAnalyzer.h
    services/SpaceAnalyzer.cpp
    services/SettingsService.h
    services/SettingsService.cpp
    services/ReportService.h
    services/ReportService.cpp
    util/FileSystemUtil.h
    util/SizeFormatter.h
    util/SizeFormatter.cpp
    util/HashUtil.h
)

add_subdirectory(app)

target_include_directories(diskorganizer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(diskorganizer_core PUBLIC
    Qt6::Core
    Qt6::Concurrent
)
