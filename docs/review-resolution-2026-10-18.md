# Upstream PR #6253 review corrections

Review: https://github.com/ramensoftware/windhawk-mods/pull/6253#issuecomment-6232025212

Follow-up: https://github.com/ramensoftware/windhawk-mods/pull/6253#issuecomment-6248173113

## Changes

- Secondary discovery runs after display/work-area changes, settings changes,
  primary moves, loaded frames, host detachment and the first shared presentation.
  Stable sample/watchdog refreshes do not enumerate windows or look up XAML roots.
  Failed discovery has six attempts with backoff; a later lifecycle event resets
  that budget. Each copy reads the shared presentation on its one-second timer.
- Copy teardown stops native timers and subclasses before XAML revocation.
  Failed XAML subscriptions retain their tokens for owner-thread retry. Dead
  native windows are removed from the active registry; their outstanding XAML
  controllers remain separately reachable for cleanup on the original thread.
  Cleanup does not add a permanent module pin or discard failed subscriptions.
- Remove a successfully detached controller from the registry before removing
  its HWND property. On failed cleanup during native destruction, transfer the
  registry owner to the pending queue before removing the property. This avoids
  both a dangling queued controller and duplicate pending ownership when unload
  observes a propertyless HWND at that boundary.
- Network visibility and byte/bit units now invalidate rejected dock reservations.
- Removed the duplicate RAM/VRAM bar updates and unused helpers; added `<limits>`.
- Formatted the code introduced since catalog version 1.7.2 using the catalog's
  Chromium style. Preserved the required Winsock/IP Helper header order.
- Moved coordinator lookup/fault-injection hooks into `tests/prepare-ui-source.py`.
  The UI runner generates an instrumented disposable copy under `build-ui-smoke`;
  production builds and the submitted source contain no test-hook blocks.
- Added actual-widget renders showing network rates on the main and secondary
  widgets. The network rates are labeled as illustrative in both READMEs.

## Verification on 2026-10-18

- `python -B tests/validate-source.py`: passed, version 1.8.1, 32 settings.
- `tests/run-regression.ps1`: passed, 173,515 behavioral checks.
- `tests/run-ui-smoke.ps1`: complete isolated XAML suite passed. New checks cover
  100 stable refreshes without further frame lookups, consumption of new samples
  by the copy timer, retry backoff/exhaustion and display-event recovery, root
  replacement before `Unloaded`, stopped native callbacks after revoke failure,
  cleanup after HWND destruction with and without the subclass still present,
  stale/propertyless registry entries, and reservation invalidation for both
  network settings. Existing secondary-thread, theme, placement, rendering and
  move-preview checks also passed.
- Deterministic unload interleaving: after the real HWND property is removed,
  another thread runs the production retirement function before owner-thread
  removal continues. The new test fails on the previous source with
  `Unload queued a deleted secondary controller` and passes after the fix.
  The repeated-revocation/native-destruction case also verifies exactly one
  pending cleanup owner and successful owner-thread cleanup.
- `tests/run-metrics-smoke.ps1`: live counters and injected invalid engine data
  passed, including the expected failure exit code 5 for injected failures.
- `build.ps1 -OutputDirectory build-review-6253/x64`: passed, no warnings.
- `build.ps1 -Architecture aarch64 -OutputDirectory build-review-6253/arm64`:
  cross-build passed, no warnings.
- `git diff --check`: passed. Both new PNGs were visually inspected.

## Live Explorer check on 2026-10-18

The final uninstrumented x64 DLL was temporarily loaded through Windhawk 1.7.3
into the existing Explorer process on Windows build 26300.9711. The active RDP
desktop had one 1920x1200 display. The installed source, DLL and configuration
were backed up before the test.

- Visually checked CPU/GPU/RAM/VRAM, graphs and network rates on the taskbar.
  Readings changed between captures and the widget remained within the taskbar.
- Changed `allMonitors` from 0 to 1 and back to 0 through the normal Windhawk
  settings-change mechanism. The main widget remained visible. There was no
  secondary live taskbar in this session.
- Disabled the candidate while `allMonitors=1`: its DLL unloaded and the widget
  disappeared. Re-enabled it: the DLL loaded and the widget reappeared with fresh
  readings. Explorer retained the same PID throughout; no Explorer crash/hang
  events were found for the test period.
- Restored the original installed DLL and enabled state. The complete mod
  registry export and installed source matched their backups byte-for-byte;
  the temporary candidate DLL was removed.

Tested source SHA-256 (workspace bytes):
`B590829DFD928E11CC6205AD83F668AF1BE6558A3931E5888A91B9F06F3ECA8E`.
Tested x64 DLL SHA-256:
`B11F93D5CF24E61FA823404315503CFDEB165B8834EE8964463843F8650839AF`.
Local evidence is retained under ignored `build-review-6253`: the
`explorer-check.jsonl` state log, `explorer-verification.json`, registry backup
and comparison, and four `explorer-*.png` captures.

## Boundaries

Live secondary-taskbar creation/removal, physical monitor hot-plug/mixed-DPI
behavior and ARM64 hardware remain untested. Failure injection
covers repeated revocation failures followed by recovery, not arbitrary permanent
COM failures or an inaccessible owner thread. Those failures still report an
incomplete teardown and retain outstanding cleanup state.

The square-backdrop-border workaround is unchanged. The reporter confirmed that
1.8.1 fixes both reported Styler problems and closed issue #6174 on 2026-10-16.
Double blending on Windows builds where the native square border already works
has not been reproduced; this revision makes no broader claim about Styler.

These corrections are included in the October 18 refresh of v1.8.1. The original
October 15 release did not contain them. The refreshed source matches upstream
PR #6253 at `300858378902543f23a95039c1308637a882db41`; tests and supporting
documentation live in this companion repository. See the
[release notes](releases/1.8.1.md) for update instructions and verification limits.
