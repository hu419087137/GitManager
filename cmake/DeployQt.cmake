if(NOT WIN32)
    message(FATAL_ERROR "DeployQt.cmake currently supports Windows only.")
endif()
if(NOT DEFINED APP_EXECUTABLE OR NOT EXISTS "${APP_EXECUTABLE}")
    message(FATAL_ERROR "APP_EXECUTABLE must point to the built GitManager executable.")
endif()
if(NOT DEFINED DEPLOY_DIRECTORY)
    message(FATAL_ERROR "DEPLOY_DIRECTORY is required.")
endif()
if(NOT DEFINED WINDEPLOYQT_EXECUTABLE OR NOT EXISTS "${WINDEPLOYQT_EXECUTABLE}")
    message(FATAL_ERROR "WINDEPLOYQT_EXECUTABLE was not found.")
endif()

file(REMOVE_RECURSE "${DEPLOY_DIRECTORY}")
file(MAKE_DIRECTORY "${DEPLOY_DIRECTORY}")
file(COPY "${APP_EXECUTABLE}" DESTINATION "${DEPLOY_DIRECTORY}")

execute_process(
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --release --no-translations --no-system-d3d-compiler
            --dir "${DEPLOY_DIRECTORY}" "${APP_EXECUTABLE}"
    RESULT_VARIABLE deploy_result
    OUTPUT_VARIABLE deploy_output
    ERROR_VARIABLE deploy_error)
if(NOT deploy_result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed (${deploy_result}):\n${deploy_output}\n${deploy_error}")
endif()

foreach(runtime IN ITEMS OPENSSL_SSL_RUNTIME OPENSSL_CRYPTO_RUNTIME)
    if(DEFINED ${runtime} AND EXISTS "${${runtime}}")
        file(COPY "${${runtime}}" DESTINATION "${DEPLOY_DIRECTORY}")
    endif()
endforeach()
if(DEFINED OPENSSL_CA_BUNDLE AND EXISTS "${OPENSSL_CA_BUNDLE}")
    file(MAKE_DIRECTORY "${DEPLOY_DIRECTORY}/certs")
    file(COPY "${OPENSSL_CA_BUNDLE}" DESTINATION "${DEPLOY_DIRECTORY}/certs")
endif()

message(STATUS "Deployed Git Manager to ${DEPLOY_DIRECTORY}")
