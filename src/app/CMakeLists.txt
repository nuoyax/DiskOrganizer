qt_standard_project_setup()
qt_add_executable(DiskOrganizer
    main.cpp
    app.rc
    app.ico
    ui/MainWindow.h
    ui/MainWindow.cpp
    ui/PageBase.h
    ui/FlatStyle.h
    ui/SearchableComboBox.h
    ui/Icons.h
    ui/Charts.h
    ui/Charts.cpp
    ui/CleanPage.h
    ui/CleanPage.cpp
    ui/DuplicatePage.h
    ui/DuplicatePage.cpp
    ui/BigFilePage.h
    ui/BigFilePage.cpp
    ui/DefragPage.h
    ui/DefragPage.cpp
    ui/SpaceAnalyzerPage.h
    ui/SpaceAnalyzerPage.cpp
    ui/SettingsDialog.h
    ui/SettingsDialog.cpp
)

target_link_libraries(DiskOrganizer PRIVATE
    diskorganizer_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Svg
)

set_target_properties(DiskOrganizer PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# USN/MFT 直读需管理员：嵌入 requireAdministrator 清单（否则静默回退慢速遍历）
if(MSVC)
    # Qt 默认注入 asInvoker 片段，需覆盖 linker manifest 配置为 requireAdministrator
    target_link_options(DiskOrganizer PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
    )
endif()

install(TARGETS DiskOrganizer RUNTIME DESTINATION .)
