qt_standard_project_setup()
qt_add_executable(DiskOrganizer
    main.cpp
    ui/MainWindow.h
    ui/MainWindow.cpp
    ui/ScanResultView.h
    ui/ScanResultView.cpp
    ui/CleanPage.h
    ui/CleanPage.cpp
    ui/DuplicatePage.h
    ui/DuplicatePage.cpp
    ui/DefragPage.h
    ui/DefragPage.cpp
    ui/SettingsDialog.h
    ui/SettingsDialog.cpp
)

target_link_libraries(DiskOrganizer PRIVATE
    diskorganizer_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
)

set_target_properties(DiskOrganizer PROPERTIES
    WIN32_EXECUTABLE TRUE
)

install(TARGETS DiskOrganizer RUNTIME DESTINATION .)
