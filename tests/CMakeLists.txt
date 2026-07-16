set(TEST_COMMON_LIBS Qt6::Core Qt6::Gui Qt6::Test)

add_executable(TestStatusParser
    unit/TestStatusParser.cpp
    ../core/parsers/StatusParser.cpp
)
target_include_directories(TestStatusParser PRIVATE ..)
target_link_libraries(TestStatusParser PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME StatusParser COMMAND TestStatusParser)

add_executable(TestBranchParser
    unit/TestBranchParser.cpp
    ../core/parsers/BranchParser.cpp
)
target_include_directories(TestBranchParser PRIVATE ..)
target_link_libraries(TestBranchParser PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME BranchParser COMMAND TestBranchParser)

add_executable(TestDiffParser unit/TestDiffParser.cpp ../core/DiffParser.cpp)
target_include_directories(TestDiffParser PRIVATE ..)
target_link_libraries(TestDiffParser PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME DiffParser COMMAND TestDiffParser)

add_executable(TestLibGit2Backend unit/TestLibGit2Backend.cpp ../core/LibGit2Backend.cpp)
target_include_directories(TestLibGit2Backend PRIVATE .. "${LIBGIT2_SOURCE_DIR}/include")
target_link_libraries(TestLibGit2Backend PRIVATE ${TEST_COMMON_LIBS} libgit2package)
add_test(NAME LibGit2Backend COMMAND TestLibGit2Backend)

add_executable(TestRepository
    integration/TestRepository.cpp
    ../core/GitCommandRunner.cpp
    ../core/parsers/StatusParser.cpp
    ../core/parsers/BranchParser.cpp
)
target_include_directories(TestRepository PRIVATE ..)
target_link_libraries(TestRepository PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME RepositoryIntegration COMMAND TestRepository)
