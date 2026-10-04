# Taskbar placement and metrics verification - 2026-10-04

Scope: local version **1.6.0**, using the compiler and engine libraries bundled
with Windhawk **1.7.3**. Scope includes adaptive layouts, move confirmation,
metric publication freshness and GPU-counter recovery. These checks qualify
the source and isolated fixtures; they do not
establish the live Explorer behavior listed below.

## Regression reproduced before the fix

The isolated XAML fixture first added a stretched TaskbarFrameRepeater with
300 DIPs of actual button content on a 1920-DIP panel, a 120-DIP tray,
Reserve space enabled, and offsets 0 and 2000. The previous scalar calculation
failed with `Stretched repeater hides widget with usable space`.

The corrected implementation measures actual button hit areas. The regression
now keeps a readable widget visible at both offsets. The offset-2000 case
places the 410-DIP widget at x=1384 with six DIPs before the tray; the unused width of the
container is available space.

The review also reproduced two defects before correcting them:

- Confirming a valid adaptive placement could select a different compact layout
  and fail transaction verification. With real XAML font measurements on a
  957-by-29-DIP panel, buttons at 316-412, a fixed control at 492-572 and a tray at
  802-957, the preview at x=578/width=173 changed to x=6/width=340 after saving its
  anchor. The solver now prioritizes available width before distance when
  shrinking. The isolated apply/verify/persist/reload transaction keeps the
  original x=578/width=173 layout. The version-1 profile format remains unchanged.
- Matching GPU Engine items with invalid `CStatus` could remain unavailable
  indefinitely while VRAM continued working. Three consecutive such samples
  now prime a separate engine query. Its rate sample is collected on a later
  worker tick, at least one configured interval after its baseline. A valid
  fresh rate triggers GPU-query recovery; invalid fresh data leaves the existing
  query intact. Probes are limited to once per minute. Tests cover transient
  failures, persistent invalid data, successful recovery, cancellation and
  pending-query teardown while CPU and RAM remain available.

## Implemented behavior

- A plain geometry snapshot and interval solver choose the nearest full-size
  gap before trying a smaller one. The normal layout retains at least 85% scale.
  When its measured text, width or height cannot fit, the widget tries full,
  graphless, compact two-row and compact one-row layouts. Compact layouts can
  shrink while effective text remains at least 9 DIPs. The widest usable gap
  wins before distance during shrinking. An unreadable placement hides and
  restores automatically without rewriting settings or saved positions.
- XAML hit areas are transformed into the panel root's coordinates. Background
  containers, the widget subtree and hidden controls do not occupy space.
  Cached element references stay on the owning UI thread. Structural changes
  rediscover controls; ordinary metric text changes reuse the cache.
- Reservation removes the mod's previous margin from the baseline, preserves
  external margins, and verifies the arranged widget and button group. Failed
  reservation restores the baseline and uses a free gap without a layout loop.
- Ctrl+Alt+M opens a native preview; release keeps the candidate, Enter
  revalidates and confirms, Esc cancels, and Home stages a reset. Normal widget
  operation is click-through. Empty moveHotkey disables registration.
- The preview contains the live widget text, graphs and bars with a rounded
  frame and a hand cursor, with no instruction surface. The glass fill appears
  only while the mouse button is held for dragging. The normal widget and the
  idle, released and hovered preview have no visible fill. The native editor
  retains alpha 1/255 inside its rounded body so empty spaces can receive clicks;
  alpha-zero layered pixels pass clicks through under
  [Win32's layered-window rules](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows).
  Invalid destinations tint the
  drag surface and outline red. Six DIPs of inner side padding and clearance
  from mapped controls and taskbar edges remain in normal operation too. CPU/GPU
  cells use compact widths while retaining 100% and 100-degree readings at font
  size 13. Source opacity is restored on cancellation; normal
  metric collection continues while the source is hidden. High contrast uses
  an opaque system-color surface during dragging. A cached premultiplied bitmap avoids rerendering
  unchanged content on every pointer move.
- Display keys use QueryDisplayConfig monitor device paths. Confirmed positions
  are normalized against the actual layout's logical travel and stored by Windhawk's
  local string API. Manual monitor/offset changes have the documented priority.
- Move application is a transaction: transfer, arranged-geometry verification,
  then persistence. Failed transfer, geometry, storage or cancellation restores
  the previous preference. Move teardown retains metric history and its rendered
  sequence, preventing replay of already rendered samples.
- A stalled collector publication becomes unavailable after five seconds or
  three update intervals, whichever is longer. Forced redraw and move
  verification do not refresh old readings or graph timestamps. Fresh
  publication restores the readings and preserves genuine gaps in history.
- Embedded Windhawk and repository README sections for moving and placement are
  compared by the source validator to prevent documentation drift.

## Automated and visual checks

| Check | Result and boundary |
| --- | --- |
| `python -B tests/validate-source.py` | Pass: metadata version 1.6.0, 29 settings, source invariants and matching placement/move documentation. |
| `./tests/run-regression.ps1` | Pass: 173,440 behavioral checks, including 100,000 generated placement/serialization/confirmation cases. Pure placement, readability, minimum clearance, DPI arithmetic, persistence and injected transaction failures; real native hidden-window callbacks for capture, Enter revalidation, Home/Esc, hotkey conflict/retry and teardown. Production provider tests include invalid-engine recovery, priming/cooldown/cleanup, HWiNFO expiry, concurrent Registry publication and a disposable real Registry key. |
| `./tests/run-ui-smoke.ps1` | Pass: 105 width/height/font cases, four adaptive layouts and five preview DPIs; measured Verdana-9 width, alternate fonts and cross-height preview verification. Another 200,000 generated geometries use measured XAML/GDI+ fonts: all 192,913 visible previews retain placement and layout after serialization/confirmation. Real isolated XAML controls, stretched-container regression, resize/hide/restore, external margins, repeated layout and cache invalidation. The reserved compact confirmation and reload retain their preview. Native preview alpha, dragging states, stale-publication expiry/recovery and history-preserving teardown also pass. |
| `./tests/run-metrics-smoke.ps1` | Pass: live Windows counter and adapter reads on this workstation. AMD Radeon RX 7900 XTX: GPU usage 15.0227%, VRAM 2.50119/23.9427 GiB and native temperature 38 C in this snapshot. CPU utility 24.078%; Windows thermal zones unavailable. Fresh memory works after one collection. Injected engine errors and invalid CStatus return the expected exit code 5. This does not reproduce a driver replacement or test the new recovery path on a real failed driver. |
| `./build.ps1 -OutputDirectory ./build-fix-x64` | Pass with the Windhawk 1.7.3 x64 engine library and `-Wall -Wextra`, no warnings. |
| `./build.ps1 -Architecture aarch64 -OutputDirectory ./build-fix-arm64` | Pass with the Windhawk 1.7.3 ARM64 engine library and `-Wall -Wextra`, no warnings; cross-build only. |
| `git diff --check` | Pass. |

Rendered widget states inspected separately from coordinate checks: dark,
light, minimum width, larger font, unavailable readings and reserved confirmation.
A 30-DIP panel now retains the compact two-row widget; an unreadable gap hides it.
The live valid/invalid/reset/hover editor previews, light/dark colors, minimum
width, larger font, high-contrast fixture and 200% preview render were inspected
for compact spacing, readable values, unclipped side padding and color feedback.
Generated PNGs are in `build-ui-smoke`.

Windhawk storage calls are replaced in the regression and XAML executables with
process-local stores. The regression store can reject writes, and the XAML test
verifies an actual arranged widget before saving and reapplying the serialized
profile. Neither is a live Windhawk/Explorer restart or a transfer between
physical displays.

## Editor diagnostics follow-up

The 25 reported VS Code errors/warnings were corrected:
the workspace now selects Windhawk's Clang, C++23, bundled headers and matching
Windows/mod defines; the standalone metrics test defines `NOMINMAX`; the Python
validator gives scalar values and option lists explicit types and fixes import
spacing without disabling strict checks.

An isolated VS Code 1.140.0 extension-host test activated C/C++ 1.35.2, Pylance
2026.4.1 with strict checking and Ruff 2026.84.0. All six affected files have
zero errors and warnings. C/C++ emits three non-error hints for reference output
parameters in the main module. All four C++ translation units also pass Clang
syntax checking with the editor configuration. Source validation, Ruff
`E4,E7,E9,F,I`, live metrics smoke including both injected failures, and
`git diff --check` pass. The isolated editor exits normally; existing user/editor
settings and the installed Windhawk mod are unchanged.

## Explorer verification remains incomplete

The workstation has Windows build 26300.9457 and Windhawk 1.7.3. A current
read of `HKLM\SOFTWARE\Windhawk\Engine\Mods\local@taskbar-system-info` reports
version **1.6.0**, enabled, with library
`local@taskbar-system-info_1.6.0_657324.dll`. The installed source has SHA-256
`7C97CB870FBC770AA273BB0E0875B5D8DC3829C19EDD48B424CC29880FAB896C`, which differs
from this checkout. The registry state does not prove the behavior of the
reviewed source. The test runs did not install or replace the Explorer mod.

These scenarios still require the new source installed into Windhawk and live
Explorer verification:

- Top and bottom taskbars, left and centered alignment, Reserve space on/off.
- Start, Search, Task View, Widgets/weather, growing app groups, overflow,
  tray/clock size changes and clicks after leaving the editor.
- Real dragging and confirmation between displays with different DPI, including
  negative desktop coordinates, missing XAML roots and unplug/reconnect.
- Saved device-path selection after restarting Explorer/Windhawk and changes to
  manual settings while editing.
- Coexistence with an actual Taskbar Styler preset and any separately hosted
  third-party taskbar elements.
- Unload while editing, final keyboard/mouse cleanup and ARM64 hardware.
- Actual TDR/driver replacement with GPU-query recovery and preserved CPU data.

The isolated tests do not satisfy these physical acceptance checks. No claim
of complete Explorer compatibility is made for version 1.6.0 yet.
