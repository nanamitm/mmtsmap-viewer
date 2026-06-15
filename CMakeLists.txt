cmake_minimum_required(VERSION 3.21)
project(mmtsmap-viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Windows .rc carrying the application icon (Explorer/taskbar/Alt-Tab).
set(APP_RESOURCES)
if(WIN32)
    list(APPEND APP_RESOURCES resources/app.rc)
endif()

add_executable(mmtsmap-viewer WIN32
    src/main.cpp
    src/MmtsMapFile.h
    src/MmtsMapFile.cpp
    src/TimelineView.h
    src/TimelineView.cpp
    src/AudioLanesView.h
    src/AudioLanesView.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    resources/resources.qrc
    ${APP_RESOURCES}
)

target_link_libraries(mmtsmap-viewer PRIVATE Qt6::Widgets)

# Copy the required Qt runtime DLLs next to the exe so it runs in-place.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET mmtsmap-viewer POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations --no-system-d3d-compiler --no-opengl-sw
                    "$<TARGET_FILE:mmtsmap-viewer>"
            COMMENT "Deploying Qt runtime via windeployqt")
    endif()
endif()
