cmake_minimum_required(VERSION 3.20)

project(GitManagerPrebuiltOpenSSL LANGUAGES NONE)

set(_openssl_source_root "${CMAKE_CURRENT_LIST_DIR}")
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _openssl_processor)
if(NOT _openssl_processor)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _openssl_processor)
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Bundled OpenSSL 3.0.16 only provides 64-bit binaries.")
endif()

if(MSVC)
    if(_openssl_processor MATCHES "arm64|aarch64")
        message(FATAL_ERROR "Bundled OpenSSL does not yet provide a Windows ARM64 binary.")
    endif()
    set(_openssl_root "${_openssl_source_root}/Win_x64")
    set(_openssl_ssl_library "${_openssl_root}/lib/libssl.lib")
    set(_openssl_crypto_library "${_openssl_root}/lib/libcrypto.lib")
    set(_openssl_ssl_runtime "${_openssl_root}/bin/libssl-3-x64.dll")
    set(_openssl_crypto_runtime "${_openssl_root}/bin/libcrypto-3-x64.dll")
    set(_openssl_library_type SHARED)
    set(_openssl_platform "Windows x64 / MSVC")
elseif(MINGW)
    message(FATAL_ERROR
        "Bundled OpenSSL only contains Windows MSVC binaries. "
        "Use the Qt msvc2019_64 kit or disable GITMANAGER_USE_PREBUILT_OPENSSL.")
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES)
        set(_openssl_macos_architecture "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(_openssl_macos_architecture "${_openssl_processor}")
    endif()
    if(NOT _openssl_macos_architecture MATCHES "^(arm64|aarch64)$")
        message(FATAL_ERROR "Bundled OpenSSL currently provides macOS ARM64 binaries only.")
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET
       OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "15.0")
        message(FATAL_ERROR
            "Bundled macOS OpenSSL requires CMAKE_OSX_DEPLOYMENT_TARGET=15.0 or newer. "
            "Disable GITMANAGER_USE_PREBUILT_OPENSSL to use SecureTransport instead.")
    endif()
    set(_openssl_root "${_openssl_source_root}/MacOS_arm64")
    set(_openssl_ssl_library "${_openssl_root}/lib/libssl.a")
    set(_openssl_crypto_library "${_openssl_root}/lib/libcrypto.a")
    set(_openssl_library_type STATIC)
    set(_openssl_platform "macOS ARM64 15.0+ / Clang")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(NOT _openssl_processor MATCHES "^(x86_64|amd64)$")
        message(FATAL_ERROR "Bundled OpenSSL currently provides Linux x86_64 binaries only.")
    endif()
    set(_openssl_root "${_openssl_source_root}/Linux_x64")
    set(_openssl_ssl_library "${_openssl_root}/lib64/libssl.so.3")
    set(_openssl_crypto_library "${_openssl_root}/lib64/libcrypto.so.3")
    set(_openssl_ssl_runtime "${_openssl_ssl_library}")
    set(_openssl_crypto_runtime "${_openssl_crypto_library}")
    set(_openssl_library_type SHARED)
    set(_openssl_platform "Linux x86_64")
elseif(UNIX)
    message(FATAL_ERROR
        "Bundled OpenSSL does not provide binaries for ${CMAKE_SYSTEM_NAME}. "
        "Disable GITMANAGER_USE_PREBUILT_OPENSSL to use the platform HTTPS backend.")
else()
    message(FATAL_ERROR "Bundled OpenSSL is not available for this platform.")
endif()

set(_openssl_include_dir "${_openssl_root}/include")
set(_openssl_ca_bundle "${_openssl_source_root}/certs/cacert.pem")
set(_openssl_required_files
    "${_openssl_include_dir}/openssl/ssl.h"
    "${_openssl_include_dir}/openssl/opensslv.h"
    "${_openssl_ssl_library}"
    "${_openssl_crypto_library}"
    "${_openssl_ca_bundle}")
if(_openssl_ssl_runtime)
    list(APPEND _openssl_required_files
        "${_openssl_ssl_runtime}"
        "${_openssl_crypto_runtime}")
endif()

foreach(_openssl_file IN LISTS _openssl_required_files)
    if(NOT EXISTS "${_openssl_file}")
        message(FATAL_ERROR "Bundled OpenSSL file is missing: ${_openssl_file}")
    endif()
endforeach()

add_library(OpenSSL::Crypto ${_openssl_library_type} IMPORTED GLOBAL)
add_library(OpenSSL::SSL ${_openssl_library_type} IMPORTED GLOBAL)

if(MSVC)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_IMPLIB "${_openssl_crypto_library}"
        IMPORTED_LOCATION "${_openssl_crypto_runtime}")
    set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_IMPLIB "${_openssl_ssl_library}"
        IMPORTED_LOCATION "${_openssl_ssl_runtime}")
else()
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${_openssl_crypto_library}")
    set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LOCATION "${_openssl_ssl_library}")
endif()

set_target_properties(OpenSSL::Crypto PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_openssl_include_dir}")
set_target_properties(OpenSSL::SSL PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_openssl_include_dir}"
    INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)

set(OPENSSL_ROOT_DIR "${_openssl_root}" CACHE PATH "Bundled prebuilt OpenSSL root" FORCE)
set(OPENSSL_INCLUDE_DIR "${_openssl_include_dir}" CACHE PATH "Bundled OpenSSL include directory" FORCE)
set(OPENSSL_SSL_LIBRARY "${_openssl_ssl_library}" CACHE FILEPATH "Bundled OpenSSL SSL library" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "${_openssl_crypto_library}" CACHE FILEPATH "Bundled OpenSSL Crypto library" FORCE)
set(OPENSSL_VERSION "3.0.16" CACHE STRING "Bundled OpenSSL version" FORCE)
set(OPENSSL_FOUND TRUE PARENT_SCOPE)
set(OPENSSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto PARENT_SCOPE)
set(OPENSSL_SSL_RUNTIME "${_openssl_ssl_runtime}" PARENT_SCOPE)
set(OPENSSL_CRYPTO_RUNTIME "${_openssl_crypto_runtime}" PARENT_SCOPE)
set(OPENSSL_CA_BUNDLE "${_openssl_ca_bundle}" PARENT_SCOPE)

message(STATUS "OpenSSL: prebuilt 3.0.16 (${_openssl_platform})")
message(STATUS "OpenSSL root: ${_openssl_root}")
