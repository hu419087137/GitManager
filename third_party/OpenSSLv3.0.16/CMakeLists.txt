cmake_minimum_required(VERSION 3.20)

project(QtOpenSSLv3SourceBuild LANGUAGES NONE)

option(OPENSSL_BUILD_SHARED "Build OpenSSL shared libraries" OFF)
option(OPENSSL_BUILD_APPS "Build openssl.exe and helper applications" OFF)
option(OPENSSL_BUILD_TESTS "Build OpenSSL tests" OFF)
option(OPENSSL_USE_ASM "Use OpenSSL assembler optimizations; requires NASM on Win64" OFF)
set(OPENSSL_CONFIGURE_TARGET
    ""
    CACHE STRING "Override OpenSSL Configure target, for example VC-WIN64A, linux-x86_64, darwin64-arm64-cc")

find_program(PERL_EXECUTABLE
    NAMES perl perl.exe
    PATHS
        "C:/Strawberry/perl/bin"
        "C:/Program Files/Git/usr/bin"
        "C:/Program Files/Git/mingw64/bin"
        "D:/Git/usr/bin"
        "D:/Qt/Tools/Perl/perl/bin"
        "C:/Qt/Tools/Perl/perl/bin")
if(NOT PERL_EXECUTABLE)
    message(FATAL_ERROR
        "Perl is required to build OpenSSL from source. Install Strawberry Perl "
        "or Git for Windows with Perl, then add perl.exe to PATH.")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _openssl_processor)
if(NOT _openssl_processor)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _openssl_processor)
endif()

set(_openssl_64bit FALSE)
if(CMAKE_SIZEOF_VOID_P EQUAL 8 OR _openssl_processor MATCHES "x86_64|amd64|arm64|aarch64")
    set(_openssl_64bit TRUE)
endif()

set(_openssl_default_install_dir "${CMAKE_CURRENT_BINARY_DIR}/install")

set(OPENSSL_INSTALL_DIR
    "${_openssl_default_install_dir}"
    CACHE PATH "OpenSSL install output directory")

if(NOT OPENSSL_CONFIGURE_TARGET)
    if(WIN32 AND NOT MINGW)
        if(_openssl_64bit)
            if(_openssl_processor MATCHES "arm64|aarch64")
                set(OPENSSL_CONFIGURE_TARGET VC-WIN64-ARM)
            else()
                set(OPENSSL_CONFIGURE_TARGET VC-WIN64A)
            endif()
        else()
            set(OPENSSL_CONFIGURE_TARGET VC-WIN32)
        endif()
    elseif(WIN32)
        if(_openssl_64bit)
            set(OPENSSL_CONFIGURE_TARGET mingw64)
        else()
            set(OPENSSL_CONFIGURE_TARGET mingw)
        endif()
    elseif(APPLE)
        if(_openssl_processor MATCHES "arm64|aarch64")
            set(OPENSSL_CONFIGURE_TARGET darwin64-arm64-cc)
        else()
            set(OPENSSL_CONFIGURE_TARGET darwin64-x86_64-cc)
        endif()
    elseif(UNIX)
        if(_openssl_processor MATCHES "aarch64|arm64")
            set(OPENSSL_CONFIGURE_TARGET linux-aarch64)
        elseif(_openssl_processor MATCHES "arm")
            set(OPENSSL_CONFIGURE_TARGET linux-armv4)
        elseif(_openssl_64bit)
            set(OPENSSL_CONFIGURE_TARGET linux-x86_64)
        else()
            set(OPENSSL_CONFIGURE_TARGET linux-x86)
        endif()
    else()
        message(FATAL_ERROR "Please set OPENSSL_CONFIGURE_TARGET for this platform.")
    endif()
endif()

if(WIN32 AND NOT MINGW)
    find_program(OPENSSL_BUILD_TOOL NAMES nmake)
    if(NOT OPENSSL_BUILD_TOOL)
        set(OPENSSL_BUILD_TOOL nmake)
        message(WARNING "nmake was not found during CMake configure. Run the build from a Visual Studio vcvars environment.")
    endif()
else()
    find_program(OPENSSL_BUILD_TOOL NAMES gmake make mingw32-make)
    if(NOT OPENSSL_BUILD_TOOL)
        set(OPENSSL_BUILD_TOOL make)
        message(WARNING "make was not found during CMake configure. The OpenSSL build will still try to run 'make'.")
    endif()
endif()
set(_openssl_build_command "${OPENSSL_BUILD_TOOL}")

set(_openssl_configure_options
    ${OPENSSL_CONFIGURE_TARGET}
    "--prefix=${OPENSSL_INSTALL_DIR}"
    "--openssldir=${OPENSSL_INSTALL_DIR}/ssl"
)

if(OPENSSL_BUILD_SHARED)
    list(APPEND _openssl_configure_options shared)
else()
    list(APPEND _openssl_configure_options no-shared)
endif()

if(NOT OPENSSL_BUILD_APPS)
    list(APPEND _openssl_configure_options no-apps)
endif()

if(NOT OPENSSL_BUILD_TESTS)
    list(APPEND _openssl_configure_options no-tests)
endif()

if(NOT OPENSSL_USE_ASM)
    list(APPEND _openssl_configure_options no-asm)
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/openssl-build")
file(MAKE_DIRECTORY "${OPENSSL_INSTALL_DIR}")
file(MAKE_DIRECTORY "${OPENSSL_INSTALL_DIR}/include")

set(_configure_stamp "${CMAKE_BINARY_DIR}/openssl-build/configured.stamp")
set(_build_stamp "${CMAKE_BINARY_DIR}/openssl-build/built.stamp")
set(_install_stamp "${CMAKE_BINARY_DIR}/openssl-build/installed.stamp")

add_custom_command(
    OUTPUT "${_configure_stamp}"
    COMMAND "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/Configure" ${_openssl_configure_options}
    COMMAND "${CMAKE_COMMAND}" -E touch "${_configure_stamp}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/openssl-build"
    COMMENT "Configuring OpenSSL 3 for MSVC Win64"
    VERBATIM
)

add_custom_command(
    OUTPUT "${_build_stamp}"
    COMMAND "${_openssl_build_command}" build_sw
    COMMAND "${CMAKE_COMMAND}" -E touch "${_build_stamp}"
    DEPENDS "${_configure_stamp}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/openssl-build"
    COMMENT "Building OpenSSL libraries"
    VERBATIM
)

add_custom_command(
    OUTPUT "${_install_stamp}"
    COMMAND "${_openssl_build_command}" install_sw
    COMMAND "${CMAKE_COMMAND}" -E touch "${_install_stamp}"
    DEPENDS "${_build_stamp}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/openssl-build"
    COMMENT "Installing OpenSSL libraries"
    VERBATIM
)

add_custom_target(openssl ALL DEPENDS "${_install_stamp}")

if(WIN32 AND OPENSSL_BUILD_SHARED)
    if(_openssl_64bit)
        set(_openssl_dll_suffix "-3-x64")
    else()
        set(_openssl_dll_suffix "-3")
    endif()
    set(OPENSSL_SSL_SHARED "${OPENSSL_INSTALL_DIR}/bin/libssl${_openssl_dll_suffix}.dll")
    set(OPENSSL_CRYPTO_SHARED "${OPENSSL_INSTALL_DIR}/bin/libcrypto${_openssl_dll_suffix}.dll")
    set(OPENSSL_SSL_LIB "${OPENSSL_INSTALL_DIR}/lib/libssl.lib")
    set(OPENSSL_CRYPTO_LIB "${OPENSSL_INSTALL_DIR}/lib/libcrypto.lib")
elseif(WIN32)
    set(OPENSSL_SSL_LIB "${OPENSSL_INSTALL_DIR}/lib/libssl.lib")
    set(OPENSSL_CRYPTO_LIB "${OPENSSL_INSTALL_DIR}/lib/libcrypto.lib")
elseif(APPLE AND OPENSSL_BUILD_SHARED)
    set(OPENSSL_SSL_SHARED "${OPENSSL_INSTALL_DIR}/lib/libssl.3.dylib")
    set(OPENSSL_CRYPTO_SHARED "${OPENSSL_INSTALL_DIR}/lib/libcrypto.3.dylib")
    set(OPENSSL_SSL_LIB "${OPENSSL_SSL_SHARED}")
    set(OPENSSL_CRYPTO_LIB "${OPENSSL_CRYPTO_SHARED}")
elseif(APPLE)
    set(OPENSSL_SSL_LIB "${OPENSSL_INSTALL_DIR}/lib/libssl.a")
    set(OPENSSL_CRYPTO_LIB "${OPENSSL_INSTALL_DIR}/lib/libcrypto.a")
elseif(OPENSSL_BUILD_SHARED)
    set(OPENSSL_SSL_SHARED "${OPENSSL_INSTALL_DIR}/lib/libssl.so.3")
    set(OPENSSL_CRYPTO_SHARED "${OPENSSL_INSTALL_DIR}/lib/libcrypto.so.3")
    set(OPENSSL_SSL_LIB "${OPENSSL_SSL_SHARED}")
    set(OPENSSL_CRYPTO_LIB "${OPENSSL_CRYPTO_SHARED}")
else()
    set(OPENSSL_SSL_LIB "${OPENSSL_INSTALL_DIR}/lib/libssl.a")
    set(OPENSSL_CRYPTO_LIB "${OPENSSL_INSTALL_DIR}/lib/libcrypto.a")
endif()

add_library(OpenSSL::SSL UNKNOWN IMPORTED GLOBAL)
set_target_properties(OpenSSL::SSL PROPERTIES
    IMPORTED_LOCATION "${OPENSSL_SSL_LIB}"
    IMPORTED_IMPLIB "${OPENSSL_SSL_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include"
)
add_dependencies(OpenSSL::SSL openssl)

add_library(OpenSSL::Crypto UNKNOWN IMPORTED GLOBAL)
set_target_properties(OpenSSL::Crypto PROPERTIES
    IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIB}"
    IMPORTED_IMPLIB "${OPENSSL_CRYPTO_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include"
)
add_dependencies(OpenSSL::Crypto openssl)

message(STATUS "OpenSSL source: ${CMAKE_CURRENT_LIST_DIR}")
message(STATUS "OpenSSL install: ${OPENSSL_INSTALL_DIR}")
message(STATUS "OpenSSL Configure target: ${OPENSSL_CONFIGURE_TARGET}")
message(STATUS "OpenSSL build tool: ${OPENSSL_BUILD_TOOL}")
