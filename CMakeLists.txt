cmake_minimum_required(VERSION 3.16)
project(GitManager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(LIBGIT2_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libgit2-1.9.4" CACHE PATH "Path to bundled libgit2 source")
if(NOT EXISTS "${LIBGIT2_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "libgit2 source not found: ${LIBGIT2_SOURCE_DIR}")
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(OPENSSL_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/OpenSSLv3.0.16" "${CMAKE_BINARY_DIR}/third_party/openssl" EXCLUDE_FROM_ALL)
set(OPENSSL_ROOT_DIR "${CMAKE_BINARY_DIR}/third_party/openssl/install" CACHE PATH "" FORCE)
set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include" CACHE PATH "" FORCE)
set(OPENSSL_SSL_LIBRARY "${OPENSSL_SSL_LIB}" CACHE FILEPATH "" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_CRYPTO_LIB}" CACHE FILEPATH "" FORCE)
set(USE_HTTPS OpenSSL CACHE STRING "" FORCE)
set(USE_SSH exec CACHE STRING "" FORCE)
set(USE_BUNDLED_ZLIB ON CACHE STRING "" FORCE)
set(STATIC_CRT OFF CACHE BOOL "" FORCE)
add_subdirectory("${LIBGIT2_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/libgit2" EXCLUDE_FROM_ALL)

set(SOURCES
    main.cpp
    MainWindow.h
    MainWindow.cpp
    core/GitTypes.h
    core/GitResult.h
    core/RepositoryState.h
    core/RepositoryState.cpp
    core/GitCommandRunner.h
    core/GitCommandRunner.cpp
    core/GitManager.h
    core/GitManager.cpp
    core/LibGit2Backend.h
    core/LibGit2Backend.cpp
    core/DiffParser.h
    core/DiffParser.cpp
    core/parsers/StatusParser.h
    core/parsers/StatusParser.cpp
    core/parsers/BranchParser.h
    core/parsers/BranchParser.cpp
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
target_include_directories(${PROJECT_NAME} PRIVATE "${LIBGIT2_SOURCE_DIR}/include")

target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Widgets
    libgit2package
)

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
