set(TEST_COMMON_LIBS Qt6::Core Qt6::Gui Qt6::Test Qt6::Network)

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

add_executable(TestLfsService unit/TestLfsService.cpp ../core/LfsService.cpp)
target_include_directories(TestLfsService PRIVATE ..)
target_link_libraries(TestLfsService PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME LfsService COMMAND TestLfsService)

add_executable(TestHostingService unit/TestHostingService.cpp ../core/HostingService.cpp)
target_include_directories(TestHostingService PRIVATE ..)
target_link_libraries(TestHostingService PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME HostingService COMMAND TestHostingService)

add_executable(TestHostingApiService
    unit/TestHostingApiService.cpp
    ../core/HostingApiService.cpp)
target_include_directories(TestHostingApiService PRIVATE ..)
target_link_libraries(TestHostingApiService PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME HostingApiService COMMAND TestHostingApiService)

add_executable(TestCredentialStore
    unit/TestCredentialStore.cpp
    ../core/CredentialStore.cpp
    ../core/HostingService.cpp)
target_include_directories(TestCredentialStore PRIVATE ..)
target_link_libraries(TestCredentialStore PRIVATE ${TEST_COMMON_LIBS})
if(WIN32)
    target_link_libraries(TestCredentialStore PRIVATE Advapi32)
endif()
add_test(NAME CredentialStore COMMAND TestCredentialStore)

add_executable(TestGitDiagnosticService
    unit/TestGitDiagnosticService.cpp
    ../core/GitDiagnosticService.cpp)
target_include_directories(TestGitDiagnosticService PRIVATE ..)
target_link_libraries(TestGitDiagnosticService PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME GitDiagnosticService COMMAND TestGitDiagnosticService)

add_executable(TestHookService unit/TestHookService.cpp ../core/HookService.cpp)
target_include_directories(TestHookService PRIVATE ..)
target_link_libraries(TestHookService PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME HookService COMMAND TestHookService)

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
    ../core/RepositoryCache.cpp
    ../core/RepositoryWatcher.cpp
    ../core/LibGit2Backend.cpp
    ../core/LfsService.cpp
    ../core/HostingService.cpp
    ../core/HostingApiService.cpp
    ../core/GitDiagnosticService.cpp
    ../core/HookService.cpp
    ../core/ExternalToolService.cpp
)
target_include_directories(TestGitManager PRIVATE .. "${LIBGIT2_SOURCE_DIR}/include")
target_link_libraries(TestGitManager PRIVATE ${TEST_COMMON_LIBS} Qt6::Concurrent libgit2package)
gitmanager_configure_openssl_target(TestGitManager)
add_test(NAME GitManager COMMAND TestGitManager)

add_executable(TestCommitGraphWidget
    unit/TestCommitGraphWidget.cpp
    ../widgets/BranchListWidget.cpp
    ../widgets/CommitGraphWidget.cpp
    ../widgets/CompareWidget.cpp
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
    ../dialogs/BranchDialog.cpp
    ../dialogs/WorktreeDialog.cpp
    ../dialogs/SubmoduleDialog.cpp
    ../dialogs/LfsDialog.cpp
    ../dialogs/HostingDialog.cpp
    ../dialogs/HostingChangesDialog.cpp
    ../dialogs/HostingIssuesDialog.cpp
    ../dialogs/HostingReviewDialog.cpp
    ../dialogs/GitDiagnosticsDialog.cpp
    ../dialogs/HooksDialog.cpp
    ../dialogs/ExternalToolsDialog.cpp
    ../core/ExternalToolService.cpp
    ../core/AppSettings.cpp
    ../dialogs/SettingsDialog.cpp
    ../widgets/NotificationWidget.cpp
    ../widgets/WelcomeWidget.cpp
    ../core/HostingService.cpp
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
