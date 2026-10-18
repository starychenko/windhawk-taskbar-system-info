# Upstream PR #6253 review corrections

Review: https://github.com/ramensoftware/windhawk-mods/pull/6253#issuecomment-6232025212

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
- `tests/run-metrics-smoke.ps1`: live counters and injected invalid engine data
  passed, including the expected failure exit code 5 for injected failures.
- `build.ps1 -OutputDirectory build-review-6253/x64`: passed, no warnings.
- `build.ps1 -Architecture aarch64 -OutputDirectory build-review-6253/arm64`:
  cross-build passed, no warnings.
- `git diff --check`: passed. Both new PNGs were visually inspected.

## Boundaries

The current corrections were not installed into Explorer. Physical monitor
hot-plug/mixed-DPI behavior and ARM64 hardware remain untested. Failure injection
covers repeated revocation failures followed by recovery, not arbitrary permanent
COM failures or an inaccessible owner thread. Those failures still report an
incomplete teardown and retain outstanding cleanup state.

The square-backdrop-border workaround is unchanged. The reporter confirmed that
1.8.1 fixes both reported Styler problems and closed issue #6174 on 2026-10-16.
Double blending on Windows builds where the native square border already works
has not been reproduced; this revision makes no broader claim about Styler.

These corrections update the PR branch. The previously published v1.8.1 release
asset and tag are unchanged and do not contain this review follow-up.
