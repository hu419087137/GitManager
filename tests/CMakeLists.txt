set(TEST_COMMON_LIBS Qt6::Core Qt6::Gui Qt6::Test)

add_executable(TestDiffParser
    unit/TestDiffParser.cpp
    ../core/DiffParser.cpp
    ../core/LibGit2Backend.cpp
)
target_include_directories(TestDiffParser PRIVATE .. "${LIBGIT2_SOURCE_DIR}/include")
target_link_libraries(TestDiffParser PRIVATE ${TEST_COMMON_LIBS} libgit2package)
gitmanager_configure_openssl_target(TestDiffParser)
add_test(NAME DiffParser COMMAND TestDiffParser)

add_executable(TestLibGit2Backend unit/TestLibGit2Backend.cpp ../core/LibGit2Backend.cpp)
target_include_directories(TestLibGit2Backend PRIVATE .. "${LIBGIT2_SOURCE_DIR}/include")
target_link_libraries(TestLibGit2Backend PRIVATE ${TEST_COMMON_LIBS} libgit2package)
gitmanager_configure_openssl_target(TestLibGit2Backend)
add_test(NAME LibGit2Backend COMMAND TestLibGit2Backend)

add_executable(TestRepository
    integration/TestRepository.cpp
    ../core/LibGit2Backend.cpp
)
target_include_directories(TestRepository PRIVATE ..)
target_include_directories(TestRepository PRIVATE "${LIBGIT2_SOURCE_DIR}/include")
target_link_libraries(TestRepository PRIVATE ${TEST_COMMON_LIBS} libgit2package)
gitmanager_configure_openssl_target(TestRepository)
add_test(NAME RepositoryIntegration COMMAND TestRepository)

gitmanager_add_openssl_deployment(
    GitManagerTestOpenSSLRuntime
    TestDiffParser
    TestLibGit2Backend
    TestRepository)
