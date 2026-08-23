cmake_minimum_required(VERSION 3.24)
project(EvgeniumWaydroidMapper VERSION 0.22.4 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Quick Qml Concurrent WaylandCompositor)
qt_add_executable(evgenium-waydroid-mapper
    app/main.cpp app/AppLog.cpp app/AppLog.h
    app/DiagnosticsCollector.cpp app/DiagnosticsCollector.h
    app/MainWindow.cpp app/MainWindow.h
    app/CenterVision.cpp app/CenterVision.h
    app/IntegratedView.cpp app/IntegratedView.h)
set_source_files_properties(app/qml/IntegratedView.qml PROPERTIES
    QT_RESOURCE_ALIAS IntegratedView.qml)
qt_add_resources(evgenium-waydroid-mapper "integrated_qml"
    PREFIX "/"
    FILES app/qml/IntegratedView.qml)
target_link_libraries(evgenium-waydroid-mapper PRIVATE
    Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::Concurrent Qt6::WaylandCompositor)
install(TARGETS evgenium-waydroid-mapper RUNTIME DESTINATION bin)
install(FILES packaging/evgenium-waydroid-mapper.desktop.in
    DESTINATION share/applications)
install(FILES assets/evgenium-waydroid-mapper.svg
    DESTINATION share/icons/hicolor/scalable/apps)
install(PROGRAMS scripts/device-profile.py DESTINATION scripts)
