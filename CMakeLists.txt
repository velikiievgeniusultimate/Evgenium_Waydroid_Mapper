cmake_minimum_required(VERSION 3.24)
project(EvgeniumWaydroidMapper VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Quick Qml WaylandCompositor)
qt_add_executable(evgenium-waydroid-mapper
    app/main.cpp app/MainWindow.cpp app/MainWindow.h
    app/MapperOverlay.cpp app/MapperOverlay.h
    app/IntegratedView.cpp app/IntegratedView.h)
set_source_files_properties(app/qml/IntegratedView.qml PROPERTIES
    QT_RESOURCE_ALIAS IntegratedView.qml)
qt_add_resources(evgenium-waydroid-mapper "integrated_qml"
    PREFIX "/"
    FILES app/qml/IntegratedView.qml)
target_link_libraries(evgenium-waydroid-mapper PRIVATE
    Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::WaylandCompositor)
install(TARGETS evgenium-waydroid-mapper RUNTIME DESTINATION bin)
