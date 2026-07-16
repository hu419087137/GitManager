cmake_minimum_required(VERSION 3.16)
project(GitManager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(SOURCES
    main.cpp
    MainWindow.h
    MainWindow.cpp
    core/GitTypes.h
    core/GitManager.h
    core/GitManager.cpp
    widgets/CommitGraphWidget.h
    widgets/CommitGraphWidget.cpp
    widgets/BranchListWidget.h
    widgets/BranchListWidget.cpp
    widgets/RepoListWidget.h
    widgets/RepoListWidget.cpp
    widgets/StatusWidget.h
    widgets/StatusWidget.cpp
    widgets/DiffWidget.h
    widgets/DiffWidget.cpp
    widgets/TerminalWidget.h
    widgets/TerminalWidget.cpp
    dialogs/CommitDialog.h
    dialogs/CommitDialog.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Widgets
)

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
