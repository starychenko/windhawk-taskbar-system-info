# Taskbar System Info

A compact, click-through system monitor for the far-left free area of the
Windows 11 taskbar. It is designed for a quick administrator glance: current
values show what is happening now, while two restrained history traces reveal
whether a CPU or GPU spike is momentary or sustained.

![Taskbar System Info preview](assets/taskbar-system-info.png)

```text
CPU  10%  72°C  [60-second graph]    RAM   52%  16.7/32G
GPU   4%  56°C  [60-second graph]    VRAM   9%   2.1/24G
```

The fixed two-column layout keeps every metric in a predictable place. CPU and
GPU history uses a fixed 0-100% scale. RAM and VRAM use thin capacity bars.
Fixed-width fields prevent the layout from shifting as values change. Normal
values remain monochrome; only warning and critical readings receive color.
Adaptive colors are enabled by default: normal text follows the native taskbar
foreground, while graphs and alerts switch automatically between contrasting
light and dark palettes. Windows high-contrast mode uses its system highlight
colors instead of the custom palette. Disable the adaptive option to use the
manual color settings exactly.

Network and disk activity are intentionally not collected.

Unlike the performance placeholders in
[Taskbar Clock Customization](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-clock-customization.wh.cpp),
this mod does not alter the clock. It uses the free far-left taskbar area for a
stable 2x2 dashboard with rolling graphs, capacity bars, temperature alerts and
fixed-width values.

## Quick start

1. Install and enable the mod. CPU, GPU, RAM and VRAM normally work without any
   additional software.
2. Keep **Temperature source** set to **Automatic**. The mod first tries
   HWiNFO, then the temperature interfaces exposed by Windows and the display
   driver.
3. If a temperature remains `--°C`, Windows probably does not expose that
   sensor. Install HWiNFO64 and configure either Shared Memory or Gadget
   Registry as described below.
4. If the widget overlaps the Start button, enable **Reserve space before the
   Start button**. If it overlaps Widgets/weather, increase **Left offset** or
   disable the conflicting taskbar element.
5. On a multi-monitor system, select **Taskbar monitor**. Monitor 1 is always
   the primary display; the numbering of other displays is explained below.

The mod is read-only: it does not control clocks, fans, power limits or GPU
settings. It does not collect network/disk activity and does not send telemetry
or make internet requests.

## Metrics

- CPU utilization from the Windows Processor Utility counter, matching the
  frequency-aware Task Manager value when available. `GetSystemTimes` remains
  the compatibility fallback.
- RAM usage and capacity from `GlobalMemoryStatusEx`.
- GPU utilization from Windows PDH GPU engine counters.
- Dedicated or shared GPU-memory usage from Windows PDH counters.
- GPU-memory capacity and adapter identity from live D3DKMT enumeration, with
  DXGI as a compatibility fallback. Automatic mode uses shared GPU memory for
  integrated adapters, including the common small dedicated carve-out case,
  and dedicated VRAM for discrete adapters. The memory type can also be forced
  in settings for unusual drivers.
- CPU and GPU temperatures from HWiNFO when available.
- GPU fallback from the Windows display-driver interface (D3DKMT).
- CPU fallback from Windows ACPI thermal zones exposed through PDH.

Metric collection runs on a worker thread. The taskbar UI thread only renders
completed snapshots and catches up with every sample that arrived while the UI
was busy, so the history traces keep their sampling interval. If a display
driver restart assigns the adapter a new LUID, the mod detects it during the
normal adapter refresh and rebuilds the GPU performance counters once. Hard
counter failures use the same bounded recovery path and fixed cooldown.

The adapter with the most dedicated VRAM is selected automatically. A partial
adapter-name filter is available for multi-GPU systems. GPU usage and VRAM are
matched to the selected live adapter by LUID. Duplicate stale adapters without
a driver name are ignored when a named adapter with the same capacity exists.
For an integrated GPU, the displayed capacity is the Windows shared-memory
limit rather than a physically reserved memory pool, so its percentage has
different semantics from a discrete GPU's dedicated VRAM. Capacities below
1 GiB and fractional totals below 4 GiB retain one decimal place.

## Temperature providers

The **Temperature source** setting provides these modes:

- **Automatic** fills CPU and GPU independently: HWiNFO Shared Memory first,
  then HWiNFO Gadget Registry, then Windows D3DKMT for a still-missing GPU
  reading and Windows thermal zones for a still-missing CPU reading.
- **HWiNFO automatic** uses only the two HWiNFO interfaces.
- **HWiNFO Shared Memory** uses only `Global\HWiNFO_SENS_SM2`.
- **HWiNFO Gadget Registry** uses only
  `HKCU\Software\HWiNFO64\VSB`.
- **Windows native** reads GPU temperature from the selected display driver via
  D3DKMT and CPU temperature from the Windows
  `\Thermal Zone Information(*)\Temperature` PDH counter. It needs no
  third-party monitor.
- **Disabled** skips temperature collection while keeping every other metric.

Windows thermal zones are ACPI platform zones. Depending on the firmware they
can represent a motherboard, chassis, skin, or processor-related zone rather
than the CPU package itself. The optional instance-name filter selects specific
zones. The aggregation setting defaults to the average used by Taskbar Clock
Customization; **Hottest** is available for alert-oriented monitoring. Systems
that don't expose thermal zones simply fall through without blocking other
metrics.

HWiNFO is optional and is not bundled with this mod.

Shared-memory integration targets HWiNFO 7.0 or newer, which permits full
disclosure of the interface. The free HWiNFO64 edition disables shared memory
after 12 hours of continuous use; HWiNFO64 Pro has no such limit. Temperature
units are classified from HWiNFO's raw unit bytes, independently of the Windows
ANSI code page.

Gadget Registry is a separate HWiNFO interface. Enable **Report to Gadget**
under **Sensor Settings > HWiNFO Gadget**. HWiNFO and Explorer must run under
the same Windows user. The automatic sensor matcher prefers:

- CPU: `CPU (Tctl/Tdie)`, `CPU Die (average)`, or `CPU Package`.
- GPU: `GPU Temperature`.

Partial HWiNFO sensor-name filters are available in the mod settings. When
Windows adapter identity is available, automatic GPU sensor selection also
matches the HWiNFO sensor name to that adapter. If Windows adapter enumeration
has never been available and no adapter filter is configured, HWiNFO falls back
to its generic GPU-temperature match; on multi-GPU systems, set the adapter and
sensor filters explicitly.

If the selected source is unavailable, temperatures are shown as `--°C`; CPU,
GPU, RAM, and VRAM monitoring continues to work. The active CPU and GPU
providers are logged only when they change. If automatic GPU matching finds
temperature readings but none match the selected Windows adapter, the log
explains that the sensor-name filter is the escape hatch.

## Setting up HWiNFO temperatures

You only need HWiNFO when Windows cannot provide the temperatures you want or
when you prefer HWiNFO's CPU package sensor. HWiNFO must be running while the
mod reads it. Running HWiNFO in **Sensors-only** mode is sufficient.

### Option A: HWiNFO Shared Memory

This is the easiest option and exposes the complete sensor table:

1. Open HWiNFO **Settings**.
2. On **General / User Interface**, enable **Shared Memory Support**.
3. Start or reopen the HWiNFO Sensors window.
4. Leave the mod on **Automatic**, or select **HWiNFO Shared Memory** if you
   want to use only this interface.

The free HWiNFO64 edition turns Shared Memory Support off after 12 hours of
continuous operation. This is an HWiNFO limitation, not a mod timer. When it
happens, restart/re-enable the HWiNFO feature, use Gadget Registry, allow the
Windows-native fallback, or use HWiNFO64 Pro. The mod does not bypass this
limitation.

### Option B: HWiNFO Gadget Registry

This interface is useful when Shared Memory is unavailable:

1. Open the HWiNFO **Sensors** window and its **Sensor Settings** dialog.
2. Open the **HWiNFO Gadget** tab.
3. Enable **Report to Gadget** for the CPU and GPU temperature readings you
   want to expose.
4. Keep HWiNFO and Explorer/Windhawk running under the same Windows user.
5. Leave the mod on **Automatic**, or select **HWiNFO Gadget Registry** to use
   only this interface.

If automatic selection chooses the wrong reading, enter a distinctive part of
the HWiNFO sensor name in **CPU temperature sensor filter** or **GPU temperature
sensor filter**. Filters are normally unnecessary and should be left empty
until there is an actual mismatch.

## Default alerts

| Metric | Warning | Critical |
| --- | ---: | ---: |
| CPU temperature | 75°C | 85°C |
| GPU temperature | 80°C | 90°C |
| RAM and VRAM | 80% | 90% |

Alerts use a small release margin to avoid flickering around a threshold. CPU
and GPU utilization stays in the normal text color because brief 100% spikes
are not automatically a problem.

## Settings reference

### Layout and placement

| Setting | What it controls |
| --- | --- |
| **Widget width** | Total width of the two-column block. Increase it if values are clipped; decrease it when taskbar space is limited. |
| **Left offset** | Distance from the far-left edge of the selected taskbar. Useful when Widgets/weather occupies the same area. |
| **Taskbar monitor** | Taskbar that receives the widget. Monitor 1 is the primary display. An unavailable selection temporarily falls back to the primary taskbar. |
| **Reserve space before the Start button** | Adds left margin to the taskbar button area so left-aligned buttons do not overlap the widget. Usually unnecessary with centered buttons. |
| **Reserved space gap** | Extra empty space between the reserved widget area and the first taskbar button. |

### Sampling and graphs

| Setting | What it controls |
| --- | --- |
| **Update interval** | How often metrics are collected. One second gives the most useful quick-monitoring view; a longer interval reduces wakeups. |
| **Graph history** | Number of seconds represented by the CPU and GPU graphs. The graphs use a fixed 0-100% scale. |

### Appearance

| Setting | What it controls |
| --- | --- |
| **Font size / Font family** | Text appearance. Keep a compact font and supported size to avoid clipping. |
| **Adapt colors to the taskbar theme** | Recommended. Automatically follows light, dark and Windows high-contrast themes. |
| **Text color** | Manual normal-text color. Used only when adaptive colors are disabled; empty means the system color. |
| **Graph and bar color** | Manual CPU/GPU graph and RAM/VRAM bar color. |
| **Warning / Critical color** | Manual alert colors used after a configured threshold is crossed. |
| **Text opacity** | Opacity of values; labels are intentionally slightly quieter. High-contrast mode keeps important content fully visible. |

### Alerts

The four temperature thresholds control CPU/GPU warning and critical colors.
The two memory thresholds apply to both RAM and VRAM percentages. A critical
threshold is automatically kept above its warning threshold.

### GPU selection and memory

| Setting | What it controls |
| --- | --- |
| **GPU adapter filter** | Optional partial Windows adapter name for multi-GPU systems. Empty selects the adapter with the most dedicated VRAM. |
| **GPU memory type** | **Automatic** uses shared memory for an integrated GPU and dedicated VRAM for a discrete GPU. Force a mode only when a driver reports the adapter incorrectly. |

Shared GPU memory is a Windows allocation limit backed by system RAM, not a
fixed VRAM chip capacity. Its percentage is therefore not directly comparable
to dedicated VRAM usage on a discrete card.

### Temperature settings

| Setting | What it controls |
| --- | --- |
| **Temperature source** | Selects Automatic, HWiNFO-only, Windows-native or Disabled behavior. Automatic is recommended. |
| **Windows thermal zone filter** | Optional partial ACPI/PDH instance name. Only affects the Windows-native CPU fallback. |
| **Windows thermal zone aggregation** | Uses the average of matching zones or the hottest zone. Firmware zones do not always represent the CPU package. |
| **CPU/GPU temperature sensor filter** | Optional partial HWiNFO sensor name. Leave empty for automatic selection. |

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| CPU or GPU temperature is `--°C` | Windows may not expose that sensor. Use Automatic mode, then configure HWiNFO Shared Memory or Gadget Registry. Confirm HWiNFO is running. |
| HWiNFO worked and stopped after about 12 hours | The free HWiNFO64 Shared Memory period expired. Re-enable/restart it, configure Gadget Registry, use Windows-native fallback, or use HWiNFO64 Pro. |
| GPU temperature belongs to another card | Set **GPU adapter filter** first. If needed, also set **GPU temperature sensor filter** to the matching HWiNFO sensor. |
| VRAM is `--` after a driver update | A changed adapter LUID triggers an automatic counter rebuild. Give it several update intervals. If it remains unavailable, reload the mod or restart Explorer and inspect the Windhawk log. |
| Integrated-GPU memory looks unexpectedly large | Automatic mode shows the Windows shared-memory limit. Select **Dedicated VRAM** only if you intentionally want the small reserved carve-out. |
| Discrete 512 MB GPU is shown as shared memory | Force **Dedicated VRAM**. The automatic memory-shape signal cannot always distinguish a legacy low-memory discrete card from an integrated carve-out. |
| Widget is missing or on the wrong taskbar | Verify **Taskbar monitor**, width and offset. Disconnecting a selected display temporarily moves the widget to the primary taskbar. Reload the mod after a major Explorer/taskbar update. |
| Widget overlaps Start, Widgets or another mod | Adjust **Left offset**, enable **Reserve space**, or disable the taskbar element using the same area. |

For diagnostics, open the mod's **Details** page in Windhawk and inspect its
log. Temperature-provider changes, adapter selection, counter recovery and
sensor-name mismatches are logged without printing every one-second sample.

## Compatibility and placement

- Windows 11 64-bit. The widget can be placed on the primary or a secondary
  taskbar. x64 is hardware-tested; ARM64 is compilation-tested.
- Monitor 1 is always the primary display. Other monitors are ordered by their
  position in the virtual desktop and can differ from the numbers in Windows
  Display Settings. An unavailable or disconnected selection falls back to the
  primary taskbar automatically and moves back when the selected display returns.
- Centered taskbar icons are recommended.
- Windows Widgets/weather or another left-side taskbar extension can occupy the
  same far-left area. Adjust the offset or disable the conflicting element if
  they overlap.
- Enable **Reserve space before the Start button** if the widget overlaps
  left-aligned taskbar buttons.
- The widget is native XAML inside the taskbar, not a topmost overlay or XAML
  Diagnostics consumer.
- It can coexist with Taskbar Styler.

Secondary-taskbar discovery is adapted from
[Taskbar Fluent Media Player](https://github.com/Salyts/Taskbar-Fluent-Media-Player)
by Salyts.

## Install

### From the official Windhawk catalog

Search for **Taskbar System Info** in Windhawk and select **Install**.

### Manual installation

1. Open Windhawk and select **Create a new mod**.
2. Replace the generated source with `taskbar-system-info.wh.cpp`.
3. Select **Compile Mod** and enable it.

## Development and verification

Run from PowerShell:

```powershell
python .\tests\validate-source.py
.\build.ps1
.\build.ps1 -Architecture aarch64 -OutputDirectory .\build-arm64
.\tests\run-metrics-smoke.ps1
```

The source validator uses only the Python standard library. The local build uses
the compiler and architecture-specific engine library bundled with Windhawk.
The smoke-test queries live D3DKMT, DXGI
fallback and PDH state, reports integrated-adapter detection, verifies that the
selected GPU LUID is present in the performance counters, checks GPU, VRAM and
temperature ranges, and reports whether Windows exposes usable ACPI thermal
zones.

## Credits and license

Taskbar discovery and window-thread marshaling follow techniques from
[Multirow taskbar for Windows 11](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-multirow.wh.cpp)
by Michael Maltsev (`m417z`). Native GPU temperature collection follows his
[Taskbar Clock Customization implementation](https://github.com/m417z/my-windhawk-mods/commit/861920df6380f4c13abec5d9226362c4725e8362).

Released under GPL-3.0.
