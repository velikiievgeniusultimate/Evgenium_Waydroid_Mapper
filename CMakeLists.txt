cmake_minimum_required(VERSION 3.24)
project(EvgeniumWaydroidMapper VERSION 0.25.2 LANGUAGES C CXX)
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
install(TARGETS evgenium-waydroid-mapper RUNTIME DESTINATION bin)
install(FILES packaging/evgenium-waydroid-mapper.desktop.in
    DESTINATION share/applications)
install(FILES assets/evgenium-waydroid-mapper.svg
    DESTINATION share/icons/hicolor/scalable/apps)
install(PROGRAMS scripts/device-profile.py DESTINATION scripts)
