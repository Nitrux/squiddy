cmake_minimum_required(VERSION 3.16)

project(squiddy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ECM 6.13 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

include(KDEInstallDirs)
include(KDECompilerSettings)
include(ECMFindQmlModule)
include(ECMSetupVersion)
include(ECMSourceVersionControl)

set(QT_NO_PRIVATE_MODULE_WARNING ON CACHE BOOL
    "MauiKit uses Qt's private WaylandClient API" FORCE)

set(QT_MAJOR_VERSION 6)
set(REQUIRED_QT_VERSION 6.9)
set(REQUIRED_KF6_VERSION 6.13.0)
set(REQUIRED_MAUIKIT_VERSION 4.0.5)

find_package(Qt6 ${REQUIRED_QT_VERSION} REQUIRED COMPONENTS Core Gui Qml Quick DBus)
find_package(KF6 ${REQUIRED_KF6_VERSION} REQUIRED COMPONENTS CoreAddons)
find_package(MauiKit4 ${REQUIRED_MAUIKIT_VERSION} REQUIRED)
ecm_find_qmlmodule(org.mauikit.controls 1.0)

if(ECM_SOURCE_UNDER_VERSION_CONTROL)
    execute_process(
        COMMAND git rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    if(NOT "$ENV{GITHUB_HEAD_REF}" STREQUAL "")
        set(GIT_BRANCH "$ENV{GITHUB_HEAD_REF}")
    elseif(NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "")
        set(GIT_BRANCH "$ENV{GITHUB_REF_NAME}")
    endif()
elseif(NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "" AND NOT "$ENV{GITHUB_SHA}" STREQUAL "")
    if(NOT "$ENV{GITHUB_HEAD_REF}" STREQUAL "")
        set(GIT_BRANCH "$ENV{GITHUB_HEAD_REF}")
    else()
        set(GIT_BRANCH "$ENV{GITHUB_REF_NAME}")
    endif()
    string(SUBSTRING "$ENV{GITHUB_SHA}" 0 7 GIT_COMMIT_HASH)
else()
    set(GIT_BRANCH "Stable")
    set(GIT_COMMIT_HASH "${PROJECT_VERSION}")
endif()

if(GIT_COMMIT_HASH STREQUAL "" AND NOT "$ENV{GITHUB_SHA}" STREQUAL "")
    string(SUBSTRING "$ENV{GITHUB_SHA}" 0 7 GIT_COMMIT_HASH)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(PWQUALITY REQUIRED IMPORTED_TARGET pwquality)
pkg_check_modules(POLKIT REQUIRED IMPORTED_TARGET polkit-gobject-1)

qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/bin")

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
    NO_IMPORT_SCAN
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

if((CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    target_compile_options(squiddy PRIVATE -march=x86-64-v3)
    target_compile_options(squiddy-installer-helper PRIVATE -march=x86-64-v3)
endif()

target_compile_definitions(squiddy PRIVATE
    GIT_BRANCH=\"${GIT_BRANCH}\"
    GIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\"
)

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

install(TARGETS squiddy ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(TARGETS squiddy-installer-helper
    RUNTIME DESTINATION ${KDE_INSTALL_LIBEXECDIR}
)

configure_file(
    data/org.nitrux.Squiddy.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.nitrux.Squiddy.service
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.nitrux.Squiddy.service
    DESTINATION ${KDE_INSTALL_DATAROOTDIR}/dbus-1/system-services
)
install(FILES data/org.nitrux.squiddy.policy
    DESTINATION ${KDE_INSTALL_DATAROOTDIR}/polkit-1/actions
)
install(FILES data/org.nxos.squiddy.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)
install(FILES README.md LICENSE
    DESTINATION ${KDE_INSTALL_DATAROOTDIR}/doc/${PROJECT_NAME}
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

if(NOT TARGET uninstall)
    configure_file(
        cmake_uninstall.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
        IMMEDIATE
        @ONLY
    )

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    )
endif()
