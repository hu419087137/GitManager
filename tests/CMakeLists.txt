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

add_executable(TestGitManager
    unit/TestGitManager.cpp
    ../core/GitManager.cpp
    ../core/LibGit2Backend.cpp
)
target_include_directories(TestGitManager PRIVATE .. "${LIBGIT2_SOURCE_DIR}/include")
target_link_libraries(TestGitManager PRIVATE ${TEST_COMMON_LIBS} Qt6::Concurrent libgit2package)
gitmanager_configure_openssl_target(TestGitManager)
add_test(NAME GitManager COMMAND TestGitManager)

add_executable(TestCommitGraphWidget
    unit/TestCommitGraphWidget.cpp
    ../widgets/BranchListWidget.cpp
    ../widgets/CommitGraphWidget.cpp
)
target_include_directories(TestCommitGraphWidget PRIVATE ..)
target_link_libraries(TestCommitGraphWidget PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME CommitGraphWidget COMMAND TestCommitGraphWidget)
set_tests_properties(CommitGraphWidget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(TestHistoryDialogs
    unit/TestHistoryDialogs.cpp
    ../dialogs/RebaseDialog.cpp
    ../dialogs/ResetDialog.cpp
    ../widgets/InteractiveRebaseWidget.cpp
)
target_include_directories(TestHistoryDialogs PRIVATE ..)
target_link_libraries(TestHistoryDialogs PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME HistoryDialogs COMMAND TestHistoryDialogs)
set_tests_properties(HistoryDialogs PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

gitmanager_add_openssl_deployment(
    GitManagerTestOpenSSLRuntime
    TestDiffParser
    TestLibGit2Backend
    TestRepository
    TestGitManager)
