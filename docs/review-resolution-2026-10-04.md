# Upstream review fixes - Taskbar System Info 1.7.1

Review target: [windhawk-mods PR #5921](https://github.com/ramensoftware/windhawk-mods/pull/5921#issuecomment-5983287692),
reviewed commit `1d29ba6e9c40f3104814f24bce348e4844810d7a`.
The submitted update replaces official version 1.5.0. Version 1.7.1 includes
the fixes below after the companion repository's published 1.7.0 release.

## Required findings

| Finding | Resolution and evidence |
| --- | --- |
| The default shortcut intercepts application shortcuts and AltGr typing. | The YAML and C++ defaults are empty. Moving requires an explicitly configured shortcut. Documentation explains conflicts. Regression checks that the default creates a control without registering a global shortcut. |
| Missing RootGrid can cause a null C++/WinRT call. | Projection returns unavailable for a missing frame/root, detached XamlRoot, invalid DPI or failed screen-coordinate lookup. The isolated XAML test covers a missing RootGrid and a measurable detached root. |
| Exceptions can escape the placement window procedure. | Geometry and hotkey messages have a C++ exception boundary. A fault-injection test sends a real WM_HOTKEY and verifies cancellation without terminating the process. |
| A newly loaded module can adopt a stale window class. | Each class has an ownership flag. Existing classes are refused when the flag is unset; the flag is cleared only after successful unregistration. Native tests cover collisions, reuse, failed unregister, retry and repeated registration. |
| Real Explorer scenarios need validation. | Still open. The owner requested no Windows automation during this pass. The installed mod and Explorer were not changed. Isolated tests do not qualify the physical scenarios below. |

## Optional cleanup

- Remove unused placement counters, the cache rebuild counter, the unused
  available-width helper, obsolete column handles and an overwritten candidate.
  The scalar-placement helper used by old regression cases now belongs only to
  the test fixture.
- Remove the non-layered WM_PRINTCLIENT test renderer from production.
  Tests use the production layered surface while keeping their windows hidden.
- Remove no_destroy from plain layout/font/capacity values. The XAML cache is a
  nullable no_destroy container and is reset on its owning thread during removal.
- Format the new layout and editor blocks with clang-format and explicit braces.
  Add the direct array include and the exception include required by the fixes.
- Keep direct-source installation instructions in the companion README.
  The embedded Windhawk page describes installation through Windhawk.
- Correct the Ukrainian left-offset spelling and reservation description.
  Move test-status information off the embedded user page; retain the explicit
  verification boundary here and in the PR.

## Behavior and performance notes

- Revalidate the cached obstacle map on layout notifications, but skip the full
  placement solver and XAML geometry changes when inputs remain unchanged.
  Publish the reusable input snapshot only after application succeeds.
- Cache plain drag projections for at most 100 ms. The editor timer and Enter
  request fresh geometry. Cancellation invalidates the cache by an atomic epoch;
  its storage is released on the editor thread. No foreign XAML references are
  retained by this cache.
- Restrict broadcast-driven cancellation to WM_DISPLAYCHANGE and
  WM_SETTINGCHANGE/SPI_SETWORKAREA. Native notification tests cover unrelated
  mouse settings and work-area changes.
- Keep font measurements and capacity budgets across widget transfers. Changing
  the font or budget still invalidates the cache. Retain exact first-pass bounds
  for custom fonts rather than assuming repeated widest digits give identical
  bounds; first measurement remains exhaustive. The XAML teardown test verifies
  retention of these plain values and release of the XAML cache.
- Live Explorer profiling remains deferred. No latency improvement is claimed
  from the isolated checks.

## Additional issues found during self-review

- Allocation failure inside the EnumDisplayMonitors callback cannot safely
  unwind through user32. The callback now stores its exception and stops;
  enumeration rethrows after returning to C++, where the window procedure can
  handle it. Regression checks both failure containment and subsequent recovery.
- An early cleanup edit removed three required grid columns along with their
  unused handles. Visual inspection exposed clipped RAM/VRAM in one-row mode.
  Injection now creates all seven columns directly. The UI suite checks the
  arranged width and bounds of every real metric row, allowing XAML's device
  pixel rounding, in addition to abstract solver/preview bounds.

## Verification

All checks below passed with Windhawk 1.7.3 / Clang 20.1.3 on Windows 11:

| Check | Result |
| --- | --- |
| `python -B tests/validate-source.py` | Version 1.7.1, 29 settings, empty shortcut default, metadata/source invariants and matching placement/move documentation. |
| `./build.ps1 -OutputDirectory ./build-review-final-x64` | x64 DLL, -Wall/-Wextra, no warnings. |
| `./build.ps1 -Architecture aarch64 -OutputDirectory ./build-review-final-arm64` | ARM64 DLL, -Wall/-Wextra, no warnings; cross-build only. |
| `./tests/run-regression.ps1` | 173,469 behavioral checks, including native class ownership, callback allocation failure/recovery, opt-in shortcut and notification cancellation. |
| `./tests/run-ui-smoke.ps1` | 105 real-XAML width/height/font cases, four layouts, five preview DPIs; 200,000 generated real-font geometries, 192,913 visible previews preserved after serialization. Actual arranged row bounds, missing/detached roots, retained plain font cache and released XAML cache also passed. |
| `./tests/run-metrics-smoke.ps1` | Read-only live Windows providers succeeded; both injected engine-failure modes returned expected exit code 5. Windows CPU thermal zones were unavailable. |
| Official `.github/pr_validation.py` | Complete one-mod update passed, including author/version/settings, image links, changelog and symbol extraction. |
| `git diff --check` and byte comparison | Passed; official submission source is identical to the companion source. |

Visual inspection of the final compact two-row, light one-row, invalid-move and
live-drag renders confirmed that all metric groups render and the move surfaces
remain readable. These are isolated renders, not Explorer screenshots.

Source and documentation use UTF-8. The module and companion README retain CRLF
without a BOM. The official submission contains only the mod file; tests and
this report stay in the companion repository.

## Physical checks still required before release qualification

- Upgrade over the official 1.5.0 mod.
- Left and centered taskbar alignment with Reserve space off and on.
- Enter, Esc and Home on one and two physical displays, including mixed DPI.
- Disable the mod while its move editor is open; restart Explorer and verify
  persistence, hotkey cleanup, restored taskbar margins and recovery.
- Verify normal taskbar controls/clicks, display disconnect/reconnect and relevant
  Taskbar Styler presets; measure placement cost in the actual Explorer tree.

ARM64 cross-compilation is not an ARM64 hardware test. Healthy-provider reads
and injected failures do not prove recovery from a physical graphics-driver reset.
