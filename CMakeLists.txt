cmake_minimum_required(VERSION 3.24)
project(EvgeniumWaydroidMapper VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_add_executable(evgenium-waydroid-mapper
    app/main.cpp app/MainWindow.cpp app/MainWindow.h
    app/MapperOverlay.cpp app/MapperOverlay.h)
target_link_libraries(evgenium-waydroid-mapper PRIVATE Qt6::Widgets)
install(TARGETS evgenium-waydroid-mapper RUNTIME DESTINATION bin)

