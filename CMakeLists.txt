cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

project(dodo-store
    VERSION 0.4.0
    DESCRIPTION "A Flatpak GUI store built with Qt6, QML and DTK"
    LANGUAGES CXX
)

set(QT_VERSION_MAJOR 6)
set(DTK_NAME_SUFFIX 6)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS
    Core
    Gui
    Network
    Qml
    Quick
    QuickControls2
    LinguistTools
)

find_package(Dtk${DTK_NAME_SUFFIX}Core REQUIRED)
find_package(Dtk${DTK_NAME_SUFFIX}Gui REQUIRED)
find_package(Dtk${DTK_NAME_SUFFIX}Declarative REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FLATPAK REQUIRED IMPORTED_TARGET flatpak)

find_package(AppStreamQt REQUIRED)

add_executable(dodo-store
    src/main.cpp

    src/backend/flatpakbackend.h
    src/backend/flatpakbackend.cpp
    src/backend/flatpakworker.h
    src/backend/flatpakworker.cpp
    src/backend/appstreampoolbridge.h
    src/backend/appstreampoolbridge.cpp
    src/backend/storeapiclient.h
    src/backend/storeapiclient.cpp

    src/models/appitem.h
    src/models/appitem.cpp
    src/models/applistmodel.h
    src/models/applistmodel.cpp
    src/models/appfilterproxymodel.h
    src/models/appfilterproxymodel.cpp

    src/qml.qrc
)

# Compile the .ts catalogs to .qm. They are installed as data files and loaded
# at runtime by DGuiApplicationHelper::loadTranslator(), which searches
# <datadir>/dodo-store/translations (and, for running from a build tree,
# <applicationDirPath>/translations). Qt's own catalogs (qt/qtbase) are loaded
# by the same call.
qt_add_translations(dodo-store
    TS_FILES
        translations/dodo-store_pt_BR.ts
        translations/dodo-store_zh_CN.ts
    QM_FILES_OUTPUT_VARIABLE dodo_store_qm_files
)
add_dependencies(dodo-store ${PROJECT_NAME}_lrelease)

install(FILES ${dodo_store_qm_files}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dodo-store/translations)

# Make the catalogs discoverable when running straight from the build tree.
# A custom target (rather than POST_BUILD on the executable) re-copies them
# whenever lrelease regenerates a .qm, without forcing a relink.
add_custom_target(dodo-store-translations ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/translations"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${dodo_store_qm_files} "${CMAKE_CURRENT_BINARY_DIR}/translations"
    DEPENDS ${dodo_store_qm_files}
    COMMENT "Copying translation catalogs next to the binary")
add_dependencies(dodo-store dodo-store-translations)

target_link_libraries(dodo-store PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Qml
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::QuickControls2
    Dtk${DTK_NAME_SUFFIX}::Core
    Dtk${DTK_NAME_SUFFIX}::Gui
    Dtk${DTK_NAME_SUFFIX}::Declarative
    AppStreamQt
    PkgConfig::FLATPAK
)

include(GNUInstallDirs)
install(TARGETS dodo-store RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Desktop integration (so the app shows up in the launcher after install).
install(FILES packaging/dodo-store.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES packaging/icons/hicolor/scalable/apps/dodo-store.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)

# DConfig metadata backing the store API settings (Config in QML,
# Dtk::Core::DConfig in C++). appId is the executable name.
install(FILES packaging/dsg/configs/dodo-store/dodo-store.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dsg/configs/dodo-store)

# Keep a copy in the build tree: run with DSG_DATA_DIRS=<build>/dsg to use it.
add_custom_target(dodo-store-dconfig ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/dsg/configs/dodo-store"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/dsg/configs/dodo-store/dodo-store.json"
            "${CMAKE_CURRENT_BINARY_DIR}/dsg/configs/dodo-store/dodo-store.json")

enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# Packaging (CPack / .deb)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "dodo-store")
set(CPACK_PACKAGE_VENDOR "dodo-store authors")
set(CPACK_PACKAGE_CONTACT "dodo-store authors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "A Flatpak graphical application store built with Qt6, QML and DTK")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "Dodo Store is a Flatpak graphical application store built with Qt6, CMake, QML and DTK. It browses, searches, installs, uninstalls and updates user-level Flatpak applications, and manages Flatpak remotes.")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "dodo-store authors")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Let dpkg-shlibdeps derive the dependencies of the linked shared libraries.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# Runtime dependencies that dpkg-shlibdeps cannot see because the QML engine
# loads them dynamically (QML modules and the Qt Quick Controls style).
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qml6-module-qtquick, qml6-module-qtquick-window, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-templates, qml6-module-qtqml, qml6-module-qtqml-workerscript, qml6-module-qtquick-controls2-styles-chameleon, libdtk6declarative, libqt6svg6")

# libflatpak is linked directly, but most users expect the flatpak CLI too.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "flatpak")

# Use the native Debian architecture (e.g. amd64) rather than the raw uname
# machine value.
if(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
    find_program(DPKG_ARCH_EXECUTABLE dpkg)
    if(DPKG_ARCH_EXECUTABLE)
        execute_process(COMMAND ${DPKG_ARCH_EXECUTABLE} --print-architecture
            OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
endif()

include(CPack)
