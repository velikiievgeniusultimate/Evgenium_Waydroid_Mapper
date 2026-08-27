cmake_minimum_required(VERSION 3.24)
project(EvgeniumWaydroidMapper VERSION 0.26.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Quick Qml Concurrent WaylandCompositor)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WaylandProtocolsDataDir wayland-protocols pkgdatadir)
find_program(WaylandScanner wayland-scanner REQUIRED)

set(PointerConstraintsXml
    "${WaylandProtocolsDataDir}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml")
set(GeneratedProtocolDir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PointerConstraintsHeader
    "${GeneratedProtocolDir}/pointer-constraints-unstable-v1-client-protocol.h")
set(PointerConstraintsCode
    "${GeneratedProtocolDir}/pointer-constraints-unstable-v1-protocol.c")
file(MAKE_DIRECTORY "${GeneratedProtocolDir}")
add_custom_command(
    OUTPUT "${PointerConstraintsHeader}" "${PointerConstraintsCode}"
    COMMAND "${WaylandScanner}" client-header
            "${PointerConstraintsXml}" "${PointerConstraintsHeader}"
    COMMAND "${WaylandScanner}" private-code
            "${PointerConstraintsXml}" "${PointerConstraintsCode}"
    DEPENDS "${PointerConstraintsXml}"
    VERBATIM)
qt_add_executable(evgenium-waydroid-mapper
    app/main.cpp app/AppLog.cpp app/AppLog.h
    app/DiagnosticsCollector.cpp app/DiagnosticsCollector.h
    app/MainWindow.cpp app/MainWindow.h
    app/WaydroidVersionManager.cpp app/WaydroidVersionManager.h
    app/WaydroidAppDataManager.cpp app/WaydroidAppDataManager.h
    app/CenterVision.cpp app/CenterVision.h
    app/IntegratedView.cpp app/IntegratedView.h
    app/WaylandPointerConfiner.cpp app/WaylandPointerConfiner.h
    "${PointerConstraintsHeader}" "${PointerConstraintsCode}")
set_source_files_properties("${PointerConstraintsCode}" PROPERTIES LANGUAGE C)
set_source_files_properties(app/qml/IntegratedView.qml PROPERTIES
    QT_RESOURCE_ALIAS IntegratedView.qml)
qt_add_resources(evgenium-waydroid-mapper "integrated_qml"
    PREFIX "/"
    FILES app/qml/IntegratedView.qml)
target_link_libraries(evgenium-waydroid-mapper PRIVATE
    Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::Concurrent Qt6::WaylandCompositor
    PkgConfig::WaylandClient)
target_include_directories(evgenium-waydroid-mapper PRIVATE
    "${GeneratedProtocolDir}")

# Arch Linux Qt 6 is built with indirect external access enabled. If an x86_64
# EWM executable is compiled with GCC's default direct external access, the
# linker may emit R_X86_64_COPY relocations for Qt protected data symbols such
# as QComboBox::staticMetaObject. Modern glibc correctly rejects that mix before
# main() with GNU_PROPERTY_1_NEEDED_INDIRECT_EXTERN_ACCESS. Force GOT-based
# access for external symbols on x86_64 so the portable release has no COPY
# relocations and remains compatible with Arch's Qt packages.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    target_compile_options(evgenium-waydroid-mapper PRIVATE
        -mno-direct-extern-access)
endif()

install(TARGETS evgenium-waydroid-mapper RUNTIME DESTINATION bin)
install(FILES packaging/evgenium-waydroid-mapper.desktop.in
    DESTINATION share/applications)
install(FILES assets/evgenium-waydroid-mapper.svg
    DESTINATION share/icons/hicolor/scalable/apps)
install(PROGRAMS scripts/device-profile.py scripts/waydroid-instance-manager.py
    scripts/waydroid-app-data.py DESTINATION scripts)
