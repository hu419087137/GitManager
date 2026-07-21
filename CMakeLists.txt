cmake_minimum_required(VERSION 3.20)
project(GitManager VERSION 1.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent Network)
find_package(Threads REQUIRED)

set(LIBGIT2_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libgit2-1.9.4" CACHE PATH "Path to bundled libgit2 source")
if(NOT EXISTS "${LIBGIT2_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "libgit2 source not found: ${LIBGIT2_SOURCE_DIR}")
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(WIN32 AND MSVC)
    set(_gitmanager_prebuilt_openssl_default ON)
else()
    set(_gitmanager_prebuilt_openssl_default OFF)
endif()
option(GITMANAGER_USE_PREBUILT_OPENSSL "Use bundled prebuilt OpenSSL 3.0.16" ${_gitmanager_prebuilt_openssl_default})
unset(_gitmanager_prebuilt_openssl_default)
if(DEFINED GITMANAGER_BUILD_BUNDLED_OPENSSL)
    message(STATUS "Removing obsolete GITMANAGER_BUILD_BUNDLED_OPENSSL cache option")
    unset(GITMANAGER_BUILD_BUNDLED_OPENSSL CACHE)
endif()
if(NOT GITMANAGER_USE_PREBUILT_OPENSSL)
    foreach(_openssl_cache_variable IN ITEMS
            OPENSSL_ROOT_DIR OPENSSL_INCLUDE_DIR OPENSSL_SSL_LIBRARY
            OPENSSL_CRYPTO_LIBRARY OPENSSL_VERSION)
        unset(${_openssl_cache_variable} CACHE)
    endforeach()
endif()
if(GITMANAGER_USE_PREBUILT_OPENSSL)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/OpenSSLv3.0.16" "${CMAKE_BINARY_DIR}/third_party/openssl" EXCLUDE_FROM_ALL)
    set(USE_HTTPS OpenSSL CACHE STRING "" FORCE)
elseif(WIN32)
    set(USE_HTTPS WinHTTP CACHE STRING "" FORCE)
elseif(APPLE)
    set(USE_HTTPS SecureTransport CACHE STRING "" FORCE)
else()
    set(USE_HTTPS OpenSSL-Dynamic CACHE STRING "" FORCE)
endif()
set(USE_SSH exec CACHE STRING "" FORCE)
set(USE_BUNDLED_ZLIB ON CACHE STRING "" FORCE)
set(STATIC_CRT OFF CACHE BOOL "" FORCE)
message(STATUS "GitManager compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} (${CMAKE_CXX_COMPILER})")
message(STATUS "GitManager Qt: ${Qt6_VERSION} (${Qt6_DIR})")
message(STATUS "GitManager HTTPS backend: ${USE_HTTPS}")
add_subdirectory("${LIBGIT2_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/libgit2" EXCLUDE_FROM_ALL)

function(gitmanager_configure_openssl_target target_name)
    if(NOT GITMANAGER_USE_PREBUILT_OPENSSL)
        return()
    endif()
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "Cannot deploy OpenSSL for unknown target: ${target_name}")
    endif()

    target_compile_definitions("${target_name}" PRIVATE GITMANAGER_BUNDLED_OPENSSL)
    if(UNIX AND NOT APPLE AND OPENSSL_SSL_RUNTIME)
        set_property(TARGET "${target_name}" APPEND PROPERTY BUILD_RPATH "$ORIGIN")
        set_property(TARGET "${target_name}" APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
endfunction()

function(gitmanager_add_openssl_deployment deployment_target output_target)
    if(NOT GITMANAGER_USE_PREBUILT_OPENSSL)
        return()
    endif()
    if(TARGET "${deployment_target}")
        message(FATAL_ERROR "OpenSSL deployment target already exists: ${deployment_target}")
    endif()

    set(_deployment_consumers "${output_target}" ${ARGN})
    foreach(_consumer IN LISTS _deployment_consumers)
        if(NOT TARGET "${_consumer}")
            message(FATAL_ERROR "Cannot deploy OpenSSL for unknown target: ${_consumer}")
        endif()
    endforeach()

    set(_deployment_sources "${OPENSSL_CA_BUNDLE}")
    if(OPENSSL_SSL_RUNTIME AND OPENSSL_CRYPTO_RUNTIME)
        list(APPEND _deployment_sources
            "${OPENSSL_SSL_RUNTIME}"
            "${OPENSSL_CRYPTO_RUNTIME}")
        add_custom_target("${deployment_target}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${output_target}>/certs"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${OPENSSL_CA_BUNDLE}"
                    "$<TARGET_FILE_DIR:${output_target}>/certs/cacert.pem"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${OPENSSL_SSL_RUNTIME}"
                    "${OPENSSL_CRYPTO_RUNTIME}"
                    "$<TARGET_FILE_DIR:${output_target}>"
            DEPENDS ${_deployment_sources}
            VERBATIM)
    else()
        add_custom_target("${deployment_target}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${output_target}>/certs"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${OPENSSL_CA_BUNDLE}"
                    "$<TARGET_FILE_DIR:${output_target}>/certs/cacert.pem"
            DEPENDS ${_deployment_sources}
            VERBATIM)
    endif()

    foreach(_consumer IN LISTS _deployment_consumers)
        add_dependencies("${_consumer}" "${deployment_target}")
    endforeach()
endfunction()

set(SOURCES
    main.cpp
    MainWindow.h
    MainWindow.cpp
    core/GitTypes.h
    core/RepositoryState.h
    core/RepositoryState.cpp
    core/GitManager.h
    core/GitManager.cpp
    core/RepositoryWatcher.h
    core/RepositoryWatcher.cpp
    core/RepositoryCache.h
    core/RepositoryCache.cpp
    core/LibGit2Backend.h
    core/LibGit2Backend.cpp
    core/LfsService.h
    core/LfsService.cpp
    core/HostingService.h
    core/HostingService.cpp
    core/HostingApiService.h
    core/HostingApiService.cpp
    core/CredentialStore.h
    core/CredentialStore.cpp
    core/GitDiagnosticService.h
    core/GitDiagnosticService.cpp
    core/HookService.h
    core/HookService.cpp
    core/ExternalToolService.cpp
    core/AppSettings.cpp
    core/DiffParser.h
    core/DiffParser.cpp
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
    widgets/CompareWidget.h
    widgets/CompareWidget.cpp
    widgets/TerminalWidget.h
    widgets/TerminalWidget.cpp
    widgets/InteractiveRebaseWidget.h
    widgets/InteractiveRebaseWidget.cpp
    widgets/NotificationWidget.cpp
    widgets/WelcomeWidget.cpp
    dialogs/CommitDialog.h
    dialogs/CommitDialog.cpp
    dialogs/BranchDialog.h
    dialogs/BranchDialog.cpp
    dialogs/WorktreeDialog.h
    dialogs/WorktreeDialog.cpp
    dialogs/SubmoduleDialog.h
    dialogs/SubmoduleDialog.cpp
    dialogs/LfsDialog.h
    dialogs/LfsDialog.cpp
    dialogs/HostingDialog.h
    dialogs/HostingDialog.cpp
    dialogs/HostingChangesDialog.h
    dialogs/HostingChangesDialog.cpp
    dialogs/HostingIssuesDialog.h
    dialogs/HostingIssuesDialog.cpp
    dialogs/HostingReviewDialog.h
    dialogs/HostingReviewDialog.cpp
    dialogs/GitDiagnosticsDialog.h
    dialogs/GitDiagnosticsDialog.cpp
    dialogs/HooksDialog.h
    dialogs/HooksDialog.cpp
    dialogs/ExternalToolsDialog.cpp
    dialogs/SettingsDialog.cpp
    dialogs/RebaseDialog.h
    dialogs/RebaseDialog.cpp
    dialogs/ResetDialog.h
    dialogs/ResetDialog.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE "${LIBGIT2_SOURCE_DIR}/include")

target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    libgit2package
)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} Advapi32)
endif()
gitmanager_configure_openssl_target(${PROJECT_NAME})
gitmanager_add_openssl_deployment(GitManagerOpenSSLRuntime ${PROJECT_NAME})

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .)
if(GITMANAGER_USE_PREBUILT_OPENSSL)
    if(OPENSSL_SSL_RUNTIME AND OPENSSL_CRYPTO_RUNTIME)
        install(FILES "${OPENSSL_SSL_RUNTIME}" "${OPENSSL_CRYPTO_RUNTIME}"
                DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    install(FILES "${OPENSSL_CA_BUNDLE}"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/certs")
endif()
install(FILES README.md CHANGELOG.md LICENSE DESTINATION .)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
