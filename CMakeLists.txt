# Проєкт "Монітор ресурсів ПК" (CPU, RAM, диск) для Linux і Windows.
# Відкривається в Qt Creator: File > Open Project (Ctrl+Shift+O) > CMakeLists.txt.
cmake_minimum_required(VERSION 3.16)

project(ResourceMonitor VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)          # автоматична обробка Q_OBJECT утилітою moc

# Звернення до ОС: Linux — /proc і statvfs(), Windows — WinAPI.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(RM_OS_SOURCES src/core/systemreader_linux.cpp)
elseif(WIN32)
    set(RM_OS_SOURCES src/core/systemreader_win.cpp)
else()
    message(FATAL_ERROR "Підтримуються лише Linux і Windows.")
endif()

if(MSVC)
    add_compile_options(/utf-8 /W4)   # вихідні файли в UTF-8 (українські рядки)
    set(RM_WARNINGS "")
else()
    set(RM_WARNINGS -Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)

# --- Ядро: збирання даних із /proc та statvfs(); залежить лише від Qt Core ---
add_library(monitor_core STATIC
    src/core/systemreader.h
    src/core/systemreader.cpp
    ${RM_OS_SOURCES}
    src/core/formatting.h
    src/core/formatting.cpp
    src/core/resourcemonitor.h
    src/core/resourcemonitor.cpp
)
target_include_directories(monitor_core PUBLIC src/core)
target_link_libraries(monitor_core PUBLIC Qt6::Core)
target_compile_options(monitor_core PRIVATE ${RM_WARNINGS})

# --- Графічний застосунок ---
add_executable(resource-monitor
    src/main.cpp
    src/gui/mainwindow.h
    src/gui/mainwindow.cpp
    src/gui/historychart.h
    src/gui/historychart.cpp
)
target_include_directories(resource-monitor PRIVATE src/gui)
target_link_libraries(resource-monitor PRIVATE monitor_core Qt6::Widgets)
target_compile_options(resource-monitor PRIVATE ${RM_WARNINGS})
set_target_properties(resource-monitor PROPERTIES WIN32_EXECUTABLE ON)   # без консолі на Windows

install(TARGETS resource-monitor RUNTIME DESTINATION bin)

# --- Модульні тести (Qt Test). Вимкнути: -DRM_BUILD_TESTS=OFF ---
option(RM_BUILD_TESTS "Збирати модульні тести" ON)
if(RM_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE monitor_core Qt6::Test)
    add_test(NAME test_core COMMAND test_core)
endif()

# Ярлик і значок для меню програм (Linux, використовується також для AppImage).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES packaging/resource-monitor.desktop DESTINATION share/applications)
    install(FILES packaging/resource-monitor.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()
