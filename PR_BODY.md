CI: Build plugin binaries and produce runtime zips

Summary
-------
Add cross-platform CI and packaging for the native plugin so we can ship runtime ZIPs that contain the Lua mod (`info.txt`, `main.lua`) and a platform-specific plugin binary (DLL/SO/DYLIB). This enables end users to download a single runtime package and run the mod with voice functionality without building the native toolchain.

What this change does
---------------------
- Adds a GitHub Actions workflow (`.github/workflows/build_release.yml`) that:
  - Builds the plugin on Windows/macOS/Linux using CMake
  - Runs unit tests (ctest)
  - Uploads platform plugin artifacts
  - Packages `info.txt`, `main.lua`, and each platform binary into `runtime-zips` artifacts
- Adds helper build/packaging scripts under `scripts/` for local use (`build_plugin.{sh,ps1}`, `make_runtime_zip.{sh,ps1}`)
- Fixes a duplicate `StartPlugin` export in `plugin/src/plugin.cpp` that caused a potential linker conflict
- Updates documentation (`How-To-Use.txt`, `CurrentModState.txt`) with runtime packaging and build guidance

Why
---
Without prebuilt platform plugin binaries, the Lua UI works but voice capture/playout and P2P transport are not available to end users. This CI produces reproducible artifacts and runtime zips suitable for release.

Testing & validation
--------------------
- Trigger CI by opening this PR. Verify Actions run for all OSes and `runtime-zips` artifact is produced.
- Download a runtime zip for your platform, extract it, copy the plugin binary into `plugin/` in a local copy of the mod, and start Teardown.
- Enable Debug Mode in the Voice Chat settings panel and run the E2E mute test (Host: Ctrl+E) to validate global mute/forced-disconnect and that audio levels are delivered.

Notes & follow-ups
------------------
- If CI build fails on a specific OS due to missing packages or tooling, we can iterate on the workflow (install packages or cache toolchains).
- Future work: sign binaries, add TURN integration tests, and add E2E multi-instance automation.

Labels: ci, build
Requested reviewers: @<engineer1> (if any), @<maintainer> (repo owner)


