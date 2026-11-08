cmake_minimum_required(VERSION 3.24)

project(squiddy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Qml Quick DBus)
find_package(ECM 6.13 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})
find_package(KF6 6.13 REQUIRED COMPONENTS CoreAddons)
find_package(MauiKit4 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PWQUALITY REQUIRED IMPORTED_TARGET pwquality)
pkg_check_modules(POLKIT REQUIRED IMPORTED_TARGET polkit-gobject-1)

include(GNUInstallDirs)
qt_standard_project_setup()

qt_add_executable(squiddy
    src/main.cpp
    src/InstallationPlan.cpp
    src/InstallationPlan.h
    src/SystemDiscovery.cpp
    src/SystemDiscovery.h
    src/StoragePreflight.cpp
    src/PrivilegedInstallerClient.cpp
    src/PrivilegedInstallerClient.h
)

set_source_files_properties(qml/main.qml PROPERTIES
    QT_RESOURCE_ALIAS Main.qml
    QT_QML_SOURCE_TYPENAME Main
)

qt_add_qml_module(squiddy
    URI org.nitrux.squiddy
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/StepCard.qml
        qml/WelcomeStep.qml
        qml/LocaleStep.qml
        qml/KeyboardStep.qml
        qml/UserStep.qml
        qml/PartitioningStep.qml
        qml/SummaryStep.qml
        qml/DebugSimulationStep.qml
)

qt_add_executable(squiddy-installer-helper
    src/privileged/main.cpp
    src/privileged/InstallerHelper.cpp
    src/privileged/InstallerHelper.h
    src/privileged/InstallationExecutor.cpp
    src/privileged/InstallationExecutor.h
    src/InstallationPlan.cpp
    src/InstallationPlan.h
    src/SystemDiscovery.cpp
    src/SystemDiscovery.h
    src/StoragePreflight.cpp
)

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    target_compile_options(squiddy PRIVATE -march=x86-64-v3 -mtune=generic)
    target_compile_options(squiddy-installer-helper PRIVATE -march=x86-64-v3 -mtune=generic)
endif()

target_link_libraries(squiddy
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::DBus
        KF6::CoreAddons
        MauiKit4
        PkgConfig::PWQUALITY
)

target_link_libraries(squiddy-installer-helper
    PRIVATE
        Qt6::Core
        Qt6::DBus
        PkgConfig::PWQUALITY
        PkgConfig::POLKIT
)

install(TARGETS squiddy
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS squiddy-installer-helper
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
)

configure_file(
    data/org.nitrux.Squiddy.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.nitrux.Squiddy.service
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.nitrux.Squiddy.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system-services
)
install(FILES data/org.nitrux.squiddy.policy
    DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions
)
install(FILES data/org.nxos.squiddy.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES README.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

set(CPACK_PACKAGE_NAME "squiddy")
set(CPACK_PACKAGE_VENDOR "Nitrux Latinoamericana S.C.")
set(CPACK_PACKAGE_CONTACT "uri_herrera@nxos.org")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Squiddy - Nitrux installer")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://nxos.org")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Uri Herrera <uri_herrera@nxos.org>")
set(CPACK_DEBIAN_PACKAGE_SECTION "admin")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "dbus, dosfstools, efibootmgr, f2fs-tools, libpwquality1, locales, mauikit (>= 4.0.0), polkitd, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-window, squashfs-tools, tzdata, udev, util-linux, xfsprogs, xkb-data")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "dwarfs-tools")
include(CPack)

configure_file(
    cmake_uninstall.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    IMMEDIATE
    @ONLY
)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
)
