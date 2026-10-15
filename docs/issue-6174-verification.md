# Issue 6174: TaskbarTweaks compatibility

Record for 2026-10-15. The earlier candidate passed live verification on the
contributor's full theme; the original installation and desktop appearance were
restored. Subsequent review found three additional cases, corrected and tested
as described in the follow-up section below. That follow-up has not been loaded
into Explorer. Release qualification is recorded separately in
[the 1.8.1 release notes](releases/1.8.1.md).

## Report and environment

- [Contributor's report](https://github.com/ramensoftware/windhawk-mods/issues/6174#issuecomment-6188785874):
  a WindhawkBlur border disappears at zero corner radius, and changing
  `Grid.Column` clips the widget in a dock taskbar.
- [TaskbarTweaks.yaml](https://github.com/user-attachments/files/33484577/TaskbarTweaks.yaml),
  SHA-256 `7D4D60AC6E6B6EDC25D9DF5192C412A37CDB76F84FDBE7750FABDB70F0E1C2E6`.
- Baseline: `c04772295cac9b892e637f8aa41ba81085e17f70` (1.8.0).
- Windows 26300.9711, loaded Windows.UI.Xaml.dll 10.0.26100.9711,
  Windhawk 1.7.3, temporary Windows 11 Taskbar Styler 1.10.
- Earlier remote-desktop session: 1920x1200, 96 DPI (Styler runtime capture).
  Final local-console verification: 2560x1440.
- Installed local source differs from the repository baseline. Tests temporarily
  select a candidate DLL built from the repository; installed source/settings
  and unrelated mods are preserved.

## Placement: evidence and correction

The baseline fixture reproduced an 800-DIP visible frame with a 1148-DIP root,
410-DIP widget and transformed X=738. Only 62 DIP remained inside the frame.
The old check treated `Margin.Left=10` as the root-space position despite a
themed column. Live captures also showed Styler restoring Width/Height=Auto
after the mod wrote numeric dimensions.

The theme now owns the outer Grid's column/span/alignment and geometry. The
mod sizes the inner Viewbox, releases its reservation and preserves themed
margin/padding/Auto dimensions. Theme removal remeasures before automatic
reservation. Secondary copies share sizing; the move editor respects ownership.

Live tests exposed two more conditions missing from the first fixture:

1. At MaxWidth=750, all four ColumnDefinition.ActualWidth values were zero
   while arranged buttons occupied 490 DIP. The calculation now also measures
   siblings, taking the maximum for overlays in one column. The new regression
   fails before this correction and passes afterward.
2. In the original dynamic mode, a Stretch frame is content-sized by an
   ancestor's Auto column. Using its current width as the growth limit caused
   one-DIP shrinkage on each pass with the theme's negative right margin,
   eventually hiding the widget. The themed path now uses bounded ancestor
   capacity even for Stretch. A failing-before regression verifies growth from
   collapsed state and stable subsequent passes. Automatic placement retains
   its existing Stretch guard.

Raw diagnostic DLL output is retained in `styler-narrow-runtime-before-overlay.log`
and `styler-overlay-runtime.log`. Extra Styler property captures can displace
the theme's own captures, so those are not proof of normal theme behavior.

## Zero-radius border

The full theme reproduced the missing border on the original installed DLL.
At x=900, y=1152, RGB changed from (22,30,70) at radius 0 to (45,52,88) at
radius 1; adjacent background rows were unchanged. This supports an interaction
with the XAML/composition rendering path, not an invalid radius. Different
native paths for square and rounded borders are the likely mechanism.

The workaround uses a native Border expanded by one DIP and clipped to the
host's square bounds, with its tiny rounded corners outside the clip. Public
brush, thickness and CornerRadius=0 remain intact. Rounded, solid and effects
without a backdrop source retain native rendering.

Live native-Border captures show the square effect and solid magenta control
correctly. The candidate sample at x=900, y=1152 was (44,51,88), close to the
radius-1 control. Possible double blending on Windows builds where the square
effect already renders is unverified.

## Live ledger

Times are local UTC+3. Images are under ignored `build-issue6174-fix`.

| Scenario | Evidence | Result |
| --- | --- | --- |
| Square effect | `live-native-radius0.png`, 15:39:06 | Border visible |
| Solid control | `live-native-solid0.png`, 15:39:37 | Correct solid border |
| Repeated columns 0/1/3 | `live-repeat-column*.png`, 16:00-16:01 | Full widget, same Explorer PID |
| Width 750 before sibling correction | `live-narrow750-column3.png`, 16:05:51 | RAM/VRAM clipped |
| Width 750 after sibling correction | `live-final-narrow750.png`, 16:17:53 | Compact CPU/GPU/RAM/VRAM/network visible |
| Width restored, column 1 | `live-final-recovery-column1.png`, 16:18:49 | Full graphs/capacities |
| Columns 0 and 3 | `live-final-column0.png`, `live-final-column3.png`, 16:23 | Full widget |
| Original mode before Stretch correction | `live-final-original-theme.png`, 16:24:28 | Widget disappeared; not a pass |
| Final candidate, original mode over RDP | `live-qualified-original.png`, 18:01:06 | Full widget stable; square border visible |
| Final candidate, column 3 over RDP | `live-qualified-column3.png`, 18:01:39 | Full widget visible |
| Final candidate, console width 750 / column 3 | `live-console-narrow750.png`, 19:37:28 | Compact CPU/GPU/RAM/VRAM/network visible |
| Width restored, column 1 | `live-console-recovery-column1.png`, 19:37:51 | Full graphs/capacities/network visible |
| Columns 0 and 3 | `live-console-column0.png`, `live-console-column3.png`, 19:38:18 and 19:38:45 | Full widget visible |
| Original dynamic mode after column changes | `live-console-original.png`, 19:39:18 | Full widget remains visible; square effect border visible |
| Solid zero-radius control | `live-console-solid0.png`, 19:40:01 | Correct magenta border; content visible |
| Styler removed, candidate still loaded | `live-console-candidate-after-theme-removal.png`, 19:40:33 | Automatic left placement restored; normal full-width taskbar/tray/clock |
| Original installed DLL restored | `live-console-restored-original.png`, 19:44:12 | Normal taskbar, full widget, tray and clock confirmed visually |

Rows before 18:00 precede the final Stretch correction. All later candidate
checks use the same final DLL. Final candidate SHA-256:
`9B5B122CE05DEC342C0C102EC87DD4D439B818A0F8E575153245C914E7D99D43`.
Source SHA-256: `6FFFA22EC605C67DDE060B5FA8DCB7247B5503B662C790A243B66D2EF91F0F62`.
Rounded borders were exercised by the column cases at radius 1; the original
theme and solid control use radius 0. The final console sequence retained
Explorer PID 2212 throughout, including theme removal and original DLL reload.

The report cannot be dismissed as a malformed configuration. Placement had
reproduced defects in the mod's geometry ownership and available-width logic.
The square backdrop border has a separate rendering interaction for which the
mod now provides a targeted workaround. This does not establish that Styler
or Windows are otherwise defect-free.

## Separate crashes

A separate graphics/capture interruption occurred during experimental runs. Its
cause was not established, and this compatibility fix makes no claim about it.

Explorer PID 38452 failed at 15:42:07 in Windows.UI.Xaml.dll, exception c000027b,
while Styler reloaded columns. The symbolized minidump places the fault on the
SnapFlyoutHost thread in Diagnostics::GetRuntimeElement,
XamlDiagnostics::GiveRootsToCallback and AdviseVisualTreeChange.
Styler uses this subscription on style reload; this mod does not use that API.
No Taskbar System Info frame is on the faulting stack. This points to the
Styler/XAML diagnostics path but does not resolve responsibility among Windows,
Styler and other interactions. The minidump lacks the referenced stowed-exception
data. The same transition later passed with both original and candidate DLLs.
These changes do not claim to repair that separate crash.

## Automated checks and limits

- Source validator: 1.8.0, 32 settings; git diff --check passed.
- x64, ARM64 and temporary local-ID DLL builds passed.
- Regression: 173,515 behavioral checks passed before the final layout-only
  correction; the shared behavioral solver is unchanged.
- Full XAML suite passed after the final correction: 105 width/height/font
  cases, 200,000 generated geometries, secondary thread, coordinator, move,
  expiry, teardown and both new failing-before regressions.
- Real composition backdrop brush verifies clipping, unchanged public radius
  and removing the workaround for rounded/solid borders.

Physical ARM64, monitor transfer and mixed DPI were not tested. Specific modes
of the full author theme were checked; this does not qualify all Styler themes
or Windows versions.

## Restoration

Original installed source SHA-256:
`112EC854BD959AE59B56AD644E825A4611C90BD3CB84D8B4753D443979249E30`.
Original enabled library: `local@taskbar-system-info_1.8.0_798047.dll`.
Pre-test Engine Mods export SHA-256:
`86986C9BFAF6ADB70FD824BA6D27F97172EA639A9CC48091C48A35B5E3034EFD`.

The 18:07 Windows restart interrupted an earlier test controller. Its exact
owned files/settings were recovered at 19:36 before starting the final console
sequence. Final cleanup removed temporary Styler files/settings at 19:40:13,
then restored the original enabled DLL at 19:43:44 and removed WER capture
settings. The original library was confirmed loaded in Explorer PID 2212.
The final Engine Mods export and installed source match the hashes above
byte-for-byte. No Application Error event occurred from 19:36 through the
19:44:28 check. The 19:44:12 capture confirms normal taskbar/widget/tray/clock
appearance. Snipping Tool's capture mode was returned to Rectangle.

Artifacts and captures remain local in
ignored build directories. Full-screen captures include unrelated desktop
content and must not be published. No commit, push, release or GitHub reply was made.

## Follow-up review corrections

Three independent reviewers identified additional scenarios. All three were
reproduced with the production source in an isolated XAML island. The initial
review record and reproductions are in ignored `build-review-issue6174`.

1. Styler can restore the same numeric Width/Height/Margin that automatic
   placement would select. Clearing ownership and recording only subsequent
   writes lost those values; reapplying column-only placement retained the old
   fixed size. Automatic placement now records the actual accepted geometry
   even when setters need no changes, on primary and secondary taskbars.
2. A fixed 600-DIP root with two Star columns gave the widget a 300-DIP slot,
   but its inner content was sized from the whole remainder. The themed path
   now uses native column allocation when it accounts for the available width.
   When metadata is zero or the Auto dock can grow, a weighted allocation with
   column minima/maxima supplies the fallback. Auto-only slots retain growth.
3. Changing only CornerRadius or BorderBrush did not trigger layout, leaving
   the old overlay active. Dependency-property callbacks now update the child
   border on its owning UI thread, without capturing its controller. Both
   primary and secondary teardown revoke the callbacks.

Follow-up source SHA-256:
`E4540E307827291A2963422D19C9541FE7A3D9253FFC156FC34279FB35DE4D42`.
Validation evidence is under ignored `build-review-fixes`:

- `before.log`: the new tests fail on the previous source for render-only
  radius/brush changes, captured-value theme reapply and equal/weighted Stars.
- `final-xaml.log`: the full XAML suite passes, including primary/secondary
  reapply, render-only transitions, callback removal, constrained zero-width
  Star metadata, capped Pixel columns, three cleanup fault cases, 105 size/font
  cases and 200,000 generated geometries. The coordinator observed exit code 0.
- `final-regression.log`: 173,515 behavioral checks pass, exit code 0.
- `final-build-x64.log`, `final-build-arm64.log`: both DLL builds pass, exit 0.
- Source validator: 1.8.0, 32 settings; `git diff --check` passes.
- `styler-reapply.png`, `styler-star-equal.png`, `styler-star-weighted.png`
  in `build-ui-smoke` were inspected: metric labels and values fit their slots.

The second review identified two adjacent cases, also corrected:

- Pixel column Width must respect MaxWidth (with MinWidth taking priority).
  `after-review-probes.log` reproduced both an oversized widget in a capped
  host column and a hidden widget beside a capped neighboring column. Both
  now fit their native slots. The two `styler-pixel-*-cap.png` renders were
  inspected. Bounds assertions wait for a rendered frame: immediately after
  replacing definitions XAML could still report the preceding visual offset.
- Secondary removal now propagates callback-revocation failure and preserves
  outstanding tokens. A pending controller cannot update or be reused as a
  complete attachment; reconciliation cleans it up before recreating it.
  Native window destruction makes one immediate owner-thread retry. One-shot
  synthetic failures verify ordinary removal, partial attach and destruction.
  These checks do not establish recovery from arbitrary persistent COM errors.

The longer secondary fixture also exposed a test-harness lifetime problem:
its bitmap export pumped the primary watchdog, which could remove synthetic
secondary controllers while the fixture retained a pointer. The symbolized
debugger stack is in `xaml-symbolized.log`. The secondary fixture now pauses
that primary watchdog, restores it afterward and checks its controller before
reuse; secondary queues/timers and the separate coordinator tests still run.
This is a correction to test isolation, not a claim about an Explorer crash.

The prior live screenshots qualify the earlier hash only. New edge cases were
verified in isolated XAML, including the secondary UI thread; no new temporary
mod installation or desktop configuration change was made for this follow-up.
Independent re-review used three GPT-6.1 Sol reviewers at high reasoning:
geometry/ownership, border/thread lifetime, and a mandatory critic. All three
reviewed the final source hash above and returned ready with no confirmed
remaining P1/P2 finding in scope. Follow-up reviews reused the same reviewers;
they are not counted as additional independent reviewers. The coordinator
confirmed all four final command exit codes and updated this evidence record.
The local launch/coverage/decision record is `build-review-fixes/review-audit.json`.
