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

Network and disk activity are intentionally not collected.

## Metrics

- CPU utilization from `GetSystemTimes`.
- RAM usage and capacity from `GlobalMemoryStatusEx`.
- GPU utilization from Windows PDH GPU engine counters.
- Dedicated VRAM usage from `GPU Adapter Memory(*)\Dedicated Usage`.
- Dedicated VRAM capacity and adapter identity from DXGI.
- CPU and GPU temperatures from HWiNFO when available.

The adapter with the most dedicated VRAM is selected automatically. A partial
adapter-name filter is available for multi-GPU systems. GPU usage and VRAM are
matched to the selected DXGI adapter by LUID.

## Optional HWiNFO temperatures

HWiNFO is optional and is not bundled with this mod. Temperatures are read in
this order:

1. Shared memory `Global\HWiNFO_SENS_SM2`.
2. Gadget registry `HKCU\Software\HWiNFO64\VSB`.

If neither interface is available, temperatures are shown as `--°C`; CPU, GPU,
RAM, and VRAM monitoring continues to work. Shared-memory availability is
governed by the installed HWiNFO edition.

For the Gadget fallback, report suitable readings to Gadget in the HWiNFO
Sensors window. The automatic sensor matcher prefers:

- CPU: `CPU (Tctl/Tdie)`, `CPU Die (average)`, or `CPU Package`.
- GPU: `GPU Temperature`.

Partial sensor-name filters are available in the mod settings.

## Default alerts

| Metric | Warning | Critical |
| --- | ---: | ---: |
| CPU temperature | 75°C | 85°C |
| GPU temperature | 80°C | 90°C |
| RAM and VRAM | 80% | 90% |

Alerts use a small release margin to avoid flickering around a threshold. CPU
and GPU utilization stays in the normal text color because brief 100% spikes
are not automatically a problem.

## Compatibility and placement

- Windows 11 x64, primary taskbar.
- Centered taskbar icons are recommended.
- Enable **Reserve space before the Start button** if the widget overlaps
  left-aligned taskbar buttons.
- The widget is native XAML inside the taskbar, not a topmost overlay or XAML
  Diagnostics consumer.
- It can coexist with Taskbar Styler.

## Install

### From the official Windhawk catalog

Once accepted into the catalog, search for **Taskbar System Info** in Windhawk
and select **Install**.

### Manual installation

1. Open Windhawk and select **Create a new mod**.
2. Replace the generated source with `taskbar-system-info.wh.cpp`.
3. Select **Compile Mod** and enable it.

## Development and verification

Run from PowerShell:

```powershell
python .\tests\validate-source.py
.\build.ps1
.\tests\run-metrics-smoke.ps1
```

The local build uses the compiler bundled with Windhawk. The smoke-test queries
live DXGI and PDH state, verifies that both sources select the same GPU, and
checks GPU and VRAM ranges.

## Credits and license

Taskbar discovery and window-thread marshaling follow techniques from
[Multirow taskbar for Windows 11](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-multirow.wh.cpp)
by Michael Maltsev (`m417z`).

Released under GPL-3.0.
