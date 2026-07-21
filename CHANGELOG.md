# Changelog

All notable changes to Git Manager are documented in this file.

## [Unreleased]

### Added

- Worktree, Submodule, Git LFS, and hosting-provider workflows.
- Commit graph filtering, history rewrite previews, and advanced history operations.
- Git hooks observability, connection diagnostics, and credential storage.
- Force-with-lease push protection and configurable external Diff/Merge tools.
- Persistent layout settings, welcome view, notifications, shortcuts, and accessibility labels.
- Windows Release CI with automated tests and a portable `windeployqt` artifact.

### Changed

- Repository operations use asynchronous libgit2 tasks with cancellation and stale-result isolation.
- The Status/Diff panel uses a narrower default layout.

## [1.0.0] - 2026-07-21

- Initial Qt 6 and libgit2 desktop application baseline.
