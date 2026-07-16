cmake_minimum_required(VERSION 3.20)

project(QtOpenSSLv3SourceBuild LANGUAGES NONE)

option(OPENSSL_BUILD_SHARED "Build OpenSSL shared libraries" OFF)
option(OPENSSL_BUILD_APPS "Build openssl.exe and helper applications" OFF)
option(OPENSSL_BUILD_TESTS "Build OpenSSL tests" OFF)
option(OPENSSL_USE_ASM "Use OpenSSL assembler optimizations; requires NASM on Win64" OFF)
set(OPENSSL_CONFIGURE_TARGET
    ""
    CACHE STRING "Override OpenSSL Configure target, for example VC-WIN64A, linux-x86_64, darwin64-arm64-cc")

set(_openssl_perl_hints
    "C:/Strawberry/perl/bin"
    "D:/Qt/Tools/Perl/perl/bin"
    "C:/Qt/Tools/Perl/perl/bin")
if(NOT MSVC)
    list(APPEND _openssl_perl_hints
        "C:/msys64/usr/bin"
        "C:/Program Files/Git/usr/bin"
        "C:/Program Files/Git/mingw64/bin"
        "D:/Git/usr/bin"
        "D:/apps/Git/usr/bin")
endif()

find_program(PERL_EXECUTABLE
    NAMES perl perl.exe
    HINTS ${_openssl_perl_hints})
if(NOT PERL_EXECUTABLE)
    if(MSVC)
        message(FATAL_ERROR
            "MSVC/nmake OpenSSL builds require a native Windows Perl. "
            "Install Strawberry Perl and add perl.exe to PATH.")
    else()
        message(FATAL_ERROR
            "Perl is required to build OpenSSL from source. Install Perl and add it to PATH.")
    endif()
endif()

execute_process(
    COMMAND "${PERL_EXECUTABLE}" -MIPC::Cmd -MLocale::Maketext::Simple -e "exit 0"
    RESULT_VARIABLE _openssl_perl_check
    ERROR_VARIABLE _openssl_perl_error
)
if(NOT _openssl_perl_check EQUAL 0)
    message(FATAL_ERROR
        "The selected Perl cannot build OpenSSL because required core modules are missing: "
        "${PERL_EXECUTABLE}\n${_openssl_perl_error}")
endif()

if(MSVC)
    execute_process(
        COMMAND "${PERL_EXECUTABLE}" -V:osname
        OUTPUT_VARIABLE _openssl_perl_os
        ERROR_VARIABLE _openssl_perl_os_error
        RESULT_VARIABLE _openssl_perl_os_result
    )
    if(NOT _openssl_perl_os_result EQUAL 0
       OR NOT _openssl_perl_os MATCHES "MSWin32")
        message(FATAL_ERROR
            "MSVC/nmake OpenSSL builds require a native Windows Perl "
            "(for example Strawberry Perl). MSYS/Cygwin Perl is not supported: "
            "${PERL_EXECUTABLE}\n${_openssl_perl_os}${_openssl_perl_os_error}")
    endif()
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
    if(MSVC)
        if(_openssl_64bit)
            if(_openssl_processor MATCHES "arm64|aarch64")
                set(OPENSSL_CONFIGURE_TARGET VC-WIN64-ARM)
            else()
                set(OPENSSL_CONFIGURE_TARGET VC-WIN64A)
            endif()
        else()
            set(OPENSSL_CONFIGURE_TARGET VC-WIN32)
        endif()
    elseif(MINGW)
        if(_openssl_64bit)
            set(OPENSSL_CONFIGURE_TARGET mingw64)
        else()
            set(OPENSSL_CONFIGURE_TARGET mingw)
        endif()
    elseif(WIN32)
        message(FATAL_ERROR "Bundled OpenSSL on Windows currently supports MSVC and MinGW toolchains only.")
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

get_filename_component(_openssl_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(_openssl_perl_dir "${PERL_EXECUTABLE}" DIRECTORY)
cmake_path(CONVERT "$ENV{PATH}" TO_CMAKE_PATH_LIST _openssl_build_path NORMALIZE)
list(PREPEND _openssl_build_path "${_openssl_perl_dir}" "${_openssl_compiler_dir}")
cmake_path(CONVERT "${_openssl_build_path}" TO_NATIVE_PATH_LIST _openssl_build_path NORMALIZE)
if(MSVC)
    find_program(OPENSSL_BUILD_TOOL NAMES nmake HINTS "${_openssl_compiler_dir}")
    if(NOT OPENSSL_BUILD_TOOL)
        set(OPENSSL_BUILD_TOOL nmake)
        message(WARNING "nmake was not found during CMake configure. Run the build from a Visual Studio vcvars environment.")
    endif()
elseif(MINGW)
    find_program(OPENSSL_BUILD_TOOL NAMES mingw32-make HINTS "${_openssl_compiler_dir}")
    if(NOT OPENSSL_BUILD_TOOL)
        set(OPENSSL_BUILD_TOOL mingw32-make)
        message(WARNING "mingw32-make was not found during CMake configure. Run the build from a Qt MinGW environment.")
    endif()
elseif(WIN32)
    message(FATAL_ERROR "Bundled OpenSSL on Windows currently supports MSVC and MinGW toolchains only.")
else()
    find_program(OPENSSL_BUILD_TOOL NAMES gmake make HINTS "${_openssl_compiler_dir}")
    if(NOT OPENSSL_BUILD_TOOL)
        set(OPENSSL_BUILD_TOOL make)
        message(WARNING "make was not found during CMake configure. The OpenSSL build will still try to run 'make'.")
    endif()
endif()
set(_openssl_build_command "${OPENSSL_BUILD_TOOL}")

if(WIN32 AND OPENSSL_BUILD_SHARED)
    if(_openssl_64bit)
        set(_openssl_dll_suffix "-3-x64")
    else()
        set(_openssl_dll_suffix "-3")
    endif()
    set(OPENSSL_SSL_SHARED "${OPENSSL_INSTALL_DIR}/bin/libssl${_openssl_dll_suffix}.dll")
    set(OPENSSL_CRYPTO_SHARED "${OPENSSL_INSTALL_DIR}/bin/libcrypto${_openssl_dll_suffix}.dll")
    if(MSVC)
        set(OPENSSL_SSL_LIB "${OPENSSL_INSTALL_DIR}/lib/libssl.lib")
        set(OPENSSL_CRYPTO_LIB "${OPENSSL_INSTALL_DIR}/lib/libcrypto.lib")
    else()
        set(OPENSSL_SSL_LIB "${OPENSSL_INSTALL_DIR}/lib/libssl.dll.a")
        set(OPENSSL_CRYPTO_LIB "${OPENSSL_INSTALL_DIR}/lib/libcrypto.dll.a")
    endif()
elseif(WIN32 AND MSVC)
    set(OPENSSL_SSL_LIB "${OPENSSL_INSTALL_DIR}/lib/libssl.lib")
    set(OPENSSL_CRYPTO_LIB "${OPENSSL_INSTALL_DIR}/lib/libcrypto.lib")
elseif(WIN32)
    set(OPENSSL_SSL_LIB "${OPENSSL_INSTALL_DIR}/lib/libssl.a")
    set(OPENSSL_CRYPTO_LIB "${OPENSSL_INSTALL_DIR}/lib/libcrypto.a")
elseif(APPLE AND OPENSSL_BUILD_SHARED)
    set(OPENSSL_SSL_SHARED "${OPENSSL_INSTALL_DIR}/lib/libssl.3.dylib")
    set(OPENSSL_CRYPTO_SHARED "${OPENSSL_INSTALL_DIR}/lib/libcrypto.3.dylib")
    set(OPENSSL_SSL_LIB "${OPENSSL_SSL_SHARED}")
    set(OPENSSL_CRYPTO_LIB "${OPENSSL_CRYPTO_SHARED}")
elseif(OPENSSL_BUILD_SHARED)
    set(OPENSSL_SSL_SHARED "${OPENSSL_INSTALL_DIR}/lib/libssl.so.3")
    set(OPENSSL_CRYPTO_SHARED "${OPENSSL_INSTALL_DIR}/lib/libcrypto.so.3")
    set(OPENSSL_SSL_LIB "${OPENSSL_SSL_SHARED}")
    set(OPENSSL_CRYPTO_LIB "${OPENSSL_CRYPTO_SHARED}")
else()
    set(OPENSSL_SSL_LIB "${OPENSSL_INSTALL_DIR}/lib/libssl.a")
    set(OPENSSL_CRYPTO_LIB "${OPENSSL_INSTALL_DIR}/lib/libcrypto.a")
endif()

set(_openssl_configure_options
    ${OPENSSL_CONFIGURE_TARGET}
    "--prefix=${OPENSSL_INSTALL_DIR}"
    "--openssldir=${OPENSSL_INSTALL_DIR}/ssl"
    "--libdir=lib"
)

if(OPENSSL_BUILD_SHARED)
    list(APPEND _openssl_configure_options shared)
else()
    list(APPEND _openssl_configure_options no-shared)
endif()

if(OPENSSL_BUILD_APPS)
    set(_openssl_build_target build_sw)
    set(_openssl_install_target install_sw)
else()
    set(_openssl_build_target build_libs)
    set(_openssl_install_target install_dev)
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
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_openssl_build_path}"
            "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/Configure" ${_openssl_configure_options}
    COMMAND "${CMAKE_COMMAND}" -E touch "${_configure_stamp}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/openssl-build"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/Configure"
        "${CMAKE_CURRENT_LIST_DIR}/VERSION.dat"
        "${CMAKE_CURRENT_LIST_FILE}"
    COMMENT "Configuring bundled OpenSSL 3.0.16"
    VERBATIM
)

add_custom_command(
    OUTPUT "${_build_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_openssl_build_path}"
            "${_openssl_build_command}" "PERL=${PERL_EXECUTABLE}" "${_openssl_build_target}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_build_stamp}"
    DEPENDS "${_configure_stamp}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/openssl-build"
    COMMENT "Building OpenSSL libraries"
    VERBATIM
)

add_custom_command(
    OUTPUT "${_install_stamp}"
    BYPRODUCTS "${OPENSSL_SSL_LIB}" "${OPENSSL_CRYPTO_LIB}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_openssl_build_path}"
            "${_openssl_build_command}" "PERL=${PERL_EXECUTABLE}" "${_openssl_install_target}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_install_stamp}"
    DEPENDS "${_build_stamp}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/openssl-build"
    COMMENT "Installing OpenSSL libraries"
    VERBATIM
)

add_custom_target(openssl DEPENDS "${_install_stamp}")

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
set_property(TARGET OpenSSL::SSL PROPERTY INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)
if(WIN32)
    set_property(TARGET OpenSSL::Crypto PROPERTY INTERFACE_LINK_LIBRARIES "crypt32;ws2_32;gdi32;advapi32;user32")
elseif(UNIX)
    set_property(TARGET OpenSSL::Crypto PROPERTY INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS};Threads::Threads")
endif()

set(OPENSSL_INSTALL_DIR "${OPENSSL_INSTALL_DIR}" PARENT_SCOPE)
set(OPENSSL_SSL_LIB "${OPENSSL_SSL_LIB}" PARENT_SCOPE)
set(OPENSSL_CRYPTO_LIB "${OPENSSL_CRYPTO_LIB}" PARENT_SCOPE)
set(OPENSSL_SSL_SHARED "${OPENSSL_SSL_SHARED}" PARENT_SCOPE)
set(OPENSSL_CRYPTO_SHARED "${OPENSSL_CRYPTO_SHARED}" PARENT_SCOPE)

message(STATUS "OpenSSL source: ${CMAKE_CURRENT_LIST_DIR}")
message(STATUS "OpenSSL install: ${OPENSSL_INSTALL_DIR}")
message(STATUS "OpenSSL Configure target: ${OPENSSL_CONFIGURE_TARGET}")
message(STATUS "OpenSSL build tool: ${OPENSSL_BUILD_TOOL}")
