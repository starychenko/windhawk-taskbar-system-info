from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = ROOT / "taskbar-system-info.wh.cpp"


def extract_block(source: str, name: str) -> str:
    match = re.search(
        rf"// =={re.escape(name)}==\s*/\*\s*(.*?)\s*\*/\s*// ==/{re.escape(name)}==",
        source,
        re.DOTALL,
    )
    if not match:
        raise AssertionError(f"Missing {name} block")
    return match.group(1)


def parse_metadata(source: str) -> dict[str, str]:
    match = re.search(
        r"// ==WindhawkMod==\s*(.*?)\s*// ==/WindhawkMod==",
        source,
        re.DOTALL,
    )
    if not match:
        raise AssertionError("Missing WindhawkMod metadata block")

    metadata: dict[str, str] = {}
    for key, value in re.findall(r"^//\s+@(\S+)\s+(.+?)\s*$", match.group(1), re.MULTILINE):
        metadata[key] = value
    return metadata


def parse_scalar(value: str) -> str | int | bool:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] == '"':
        return value[1:-1]
    if value == "true":
        return True
    if value == "false":
        return False
    if re.fullmatch(r"-?\d+", value):
        return int(value)
    return value


def parse_settings(block: str) -> list[dict[str, object]]:
    """Parse the deliberately small YAML subset used by Windhawk settings.

    Keeping this validator dependency-free is useful on a fresh Windows system,
    where the bundled Python doesn't necessarily include PyYAML.
    """
    settings: list[dict[str, object]] = []
    current: dict[str, object] | None = None
    current_options: list[dict[str, str]] | None = None

    for line_number, raw_line in enumerate(block.splitlines(), 1):
        if not raw_line.strip():
            continue

        setting_match = re.fullmatch(r"- ([^:]+):\s*(.*)", raw_line)
        if setting_match:
            current = {
                setting_match.group(1): parse_scalar(setting_match.group(2))
            }
            settings.append(current)
            current_options = None
            continue

        if current is None:
            raise AssertionError(
                f"Settings line {line_number} appears before the first setting"
            )

        property_match = re.fullmatch(r"  (\$[^:]+(?::[^:]+)?):\s*(.*)", raw_line)
        if property_match:
            key, value = property_match.groups()
            if value:
                current[key] = parse_scalar(value)
                current_options = None
            else:
                current_options = []
                current[key] = current_options
            continue

        option_match = re.fullmatch(r"  - ([^:]+):\s*(.*)", raw_line)
        if option_match and current_options is not None:
            current_options.append(
                {option_match.group(1): str(parse_scalar(option_match.group(2)))}
            )
            continue

        raise AssertionError(
            f"Unsupported settings syntax on line {line_number}: {raw_line!r}"
        )

    return settings


def main() -> int:
    source = SOURCE_PATH.read_text(encoding="utf-8")
    metadata = parse_metadata(source)

    expected = {
        "id": "taskbar-system-info",
        "version": "1.4.0",
        "author": "Yevhenii Starychenko",
        "github": "https://github.com/starychenko",
        "license": "GPL-3.0",
        "architecture": "x86-64",
    }
    for key, value in expected.items():
        assert metadata.get(key) == value, f"Unexpected @{key}: {metadata.get(key)!r}"

    assert metadata.get("name")
    assert metadata.get("description")
    assert metadata.get("name:uk-UA")
    assert metadata.get("description:uk-UA")

    settings = parse_settings(extract_block(source, "WindhawkModSettings"))
    assert len(settings) == 28, f"Expected 28 settings, got {len(settings)}"

    setting_keys: set[str] = set()
    for index, item in enumerate(settings):
        assert isinstance(item, dict), f"Setting #{index + 1} must be a mapping"
        value_keys = [key for key in item if not str(key).startswith("$")]
        assert len(value_keys) == 1, f"Setting #{index + 1} must have one value key"
        key = str(value_keys[0])
        assert key not in setting_keys, f"Duplicate setting: {key}"
        setting_keys.add(key)

        for localized_key in ("$name", "$name:uk-UA", "$description", "$description:uk-UA"):
            if localized_key in item:
                assert isinstance(item[localized_key], str), (
                    f"{key}.{localized_key} must be a string, got "
                    f"{type(item[localized_key]).__name__}"
                )

    monitor = next(item for item in settings if "monitor" in item)
    assert monitor["monitor"] == 1

    adaptive_colors = next(item for item in settings if "adaptiveColors" in item)
    assert adaptive_colors["adaptiveColors"] is True

    gpu_memory_mode = next(item for item in settings if "gpuMemoryMode" in item)
    assert gpu_memory_mode["gpuMemoryMode"] == "auto"
    for options_key in ("$options", "$options:uk-UA"):
        options = gpu_memory_mode.get(options_key)
        assert isinstance(options, list), f"gpuMemoryMode.{options_key} missing"
        assert {
            str(next(iter(option))) for option in options if isinstance(option, dict)
        } == {"auto", "dedicated", "shared"}

    temperature_source = next(
        item for item in settings if "temperatureSource" in item
    )
    assert temperature_source["temperatureSource"] == "auto"
    expected_temperature_sources = {
        "auto",
        "hwinfoAuto",
        "sharedMemory",
        "gadgetRegistry",
        "windowsNative",
        "disabled",
    }
    for options_key in ("$options", "$options:uk-UA"):
        options = temperature_source.get(options_key)
        assert isinstance(options, list), f"temperatureSource.{options_key} missing"
        assert {
            str(next(iter(option))) for option in options if isinstance(option, dict)
        } == expected_temperature_sources

    thermal_zone_aggregation = next(
        item for item in settings if "windowsThermalZoneAggregation" in item
    )
    assert thermal_zone_aggregation["windowsThermalZoneAggregation"] == "average"
    for options_key in ("$options", "$options:uk-UA"):
        options = thermal_zone_aggregation.get(options_key)
        assert isinstance(options, list), (
            f"windowsThermalZoneAggregation.{options_key} missing"
        )
        assert {
            str(next(iter(option))) for option in options if isinstance(option, dict)
        } == {"average", "hottest"}

    readme = extract_block(source, "WindhawkModReadme")
    assert "raw.githubusercontent.com/starychenko/windhawk-taskbar-system-info/" in readme

    assert "[[clang::no_destroy]] Grid g_widget{nullptr};" in source
    assert "std::optional<std::thread> g_metricsWorker" in source
    assert "std::shared_ptr<const ModSettings> CurrentSettings()" in source
    assert (
        "[[clang::no_destroy]] std::shared_ptr<const ModSettings>" not in source
    )
    assert "std::optional<std::list<FrameworkElement::Loaded_revoker>>" in source
    assert "#elif defined(_M_ARM64)" in source
    assert "0xD503237F" in source
    assert "SendMessageTimeoutW(" not in source
    assert "SendMessageW(window, message" in source
    assert "size_t elementOffset = 0;" in source
    assert 'Wh_Log(L"Removing stale Taskbar System Info widget")' in source
    assert "HWiNFO_SENS_SM2" in source
    assert "ReadHwInfoSharedMemory" in source
    assert "Software\\\\HWiNFO64\\\\VSB" in source
    assert "enum class TemperatureSource" in source
    assert "case TemperatureSource::SharedMemory:" in source
    assert "case TemperatureSource::GadgetRegistry:" in source
    assert "case TemperatureSource::WindowsNative:" in source
    assert "case TemperatureSource::Disabled:" in source
    assert "NormalizeRegistryTemperature" in source
    assert '\\\\Thermal Zone Information(*)\\\\Temperature' in source
    assert "ReadWindowsThermalZones" in source
    assert "TemperatureProvider::WindowsThermalZones" in source
    assert "TemperatureProvider::WindowsD3dkmt" in source
    assert "D3DKMT_ADAPTER_PERFDATA" in source
    assert "D3DKMTQueryAdapterInfo" in source
    assert "kAdapterPerfDataQueryType = 62" in source
    assert "ReadWindowsGpuTemperature" in source
    assert 'value == L"windowsThermalZones"' not in source
    assert "static_assert(offsetof(HwInfoHeader, pollTime) == 12);" in source
    assert "static_assert(offsetof(HwInfoReadingPrefix, value) == 284);" in source

    temperature_dispatch = source[
        source.index("void ReadTemperatures(") : source.index("uint64_t FileTimeValue(")
    ]
    assert "case TemperatureSource::HwInfoAuto:" in temperature_dispatch
    assert "ResolveGpuTemperatureAdapterName(settings)" in temperature_dispatch
    assert "ReadHwInfoTemperatures(snapshot, settings, gpuAdapterName," in temperature_dispatch
    assert "LogHwInfoGpuTemperatureMismatch" in temperature_dispatch
    assert "if (!snapshot.gpuTemp)" in temperature_dispatch
    assert "ReadWindowsGpuTemperature(snapshot, settings);" in temperature_dispatch
    assert "if (!snapshot.cpuTemp)" in temperature_dispatch
    assert "ReadWindowsThermalZones(snapshot, settings);" in temperature_dispatch
    assert "kPdhCounterRetryInterval" in source
    assert "kPdhReadFailureThreshold" in source
    assert "RecordPdhReadFailure" in source
    assert "IsHardPdhArrayFailure" in source
    assert "PDH_CSTATUS_NO_INSTANCE" in source
    assert "g_pdhGpuSampleWasAvailable" not in source
    assert "g_pdhVramSampleWasAvailable" not in source
    assert "InvalidateGpuAdapterCache" in source
    assert "D3DKMTEnumAdapters2" in source
    assert "D3DKMT_ADAPTERREGISTRYINFO" in source
    assert "D3DKMT_SEGMENTSIZEINFO" in source
    assert "GetLiveD3dkmtAdapterInfo" in source
    assert "ResolveCurrentGpuAdapterInfo" in source
    assert "HasGpuAdapterIdentityChanged" in source
    assert "!candidate.description.empty()" in source
    assert "AddPdhCounter(" in source
    assert "g_nextPdhCounterRetry" in source
    assert "TearDownTaskbarUi" in source
    assert "FindAnyWindowOnTaskbarThread" in source
    teardown = source[
        source.index("bool TearDownTaskbarUi()") : source.index("}  // namespace")
    ]
    assert "FindPrimaryTaskbarWindow()" in teardown
    assert 'Wh_Log(L"Initial taskbar UI teardown failed; will retry")' in source
    assert 'Wh_Log(L"Taskbar UI teardown retry failed")' in source
    assert 'GetModuleHandleW(L"gdi32.dll")' in source

    format_capacity = source[
        source.index("std::wstring FormatCapacity(") :
        source.index("enum class AlertLevel")
    ]
    assert "totalGb < 4.0" in format_capacity
    assert "std::abs(totalGb - roundedTotalGb) >= 0.05" in format_capacity
    assert "FormatFixed(totalGb, totalDecimals)" in format_capacity

    shared_memory_reader = source[
        source.index("void ReadHwInfoSharedMemory(") :
        source.index("std::optional<std::wstring> ReadRegistryString(")
    ]
    assert "HwInfoHeader header{};" in shared_memory_reader
    assert "std::memcpy(&header, view, sizeof(header));" in shared_memory_reader
    assert "HwInfoHeader verificationHeader{};" in shared_memory_reader
    assert "std::memcmp(&header, &verificationHeader," in shared_memory_reader
    assert "header->" not in shared_memory_reader
    assert "mappedSize >= sizeof(HwInfoHeader)" in shared_memory_reader
    assert "FixedAnsiToWide(reading.unit" not in shared_memory_reader
    assert "NormalizeHwInfoTemperature(" in shared_memory_reader
    assert "snapshot.cpuTemp = *value;" in shared_memory_reader
    assert "snapshot.gpuTemp = *value;" in shared_memory_reader
    assert shared_memory_reader.index("UnmapViewOfFile(view)") < shared_memory_reader.index(
        "FixedAnsiToWide(sensor.originalName"
    )
    assert "g_hwInfoInvalidUnitLogged" in shared_memory_reader
    assert "void ReadHwInfoGadgetRegistry(" in source
    assert "bool foundAny" not in source
    assert "HwInfoGadgetRegistryCache" in source
    assert "g_hwInfoGadgetRegistryCache.cpuIndex" in source
    assert "g_hwInfoGadgetRegistryCache.gpuIndex" in source
    assert "kGadgetRegistryRescanInterval" in source

    normalize_temperature = source[
        source.index("std::optional<double> NormalizeTemperature(") :
        source.index("constexpr char HwInfoTemperatureUnit(")
    ]
    assert (
        "fahrenheit ? (value - 32.0) * 5.0 / 9.0 : value"
        in normalize_temperature
    )
    assert 'unit == L"c"' in normalize_temperature
    assert 'unit == L"f"' in normalize_temperature
    assert "celsiusValue < -50.0" in normalize_temperature

    hwinfo_unit_reader = source[
        source.index("constexpr char HwInfoTemperatureUnit(") :
        source.index("void ReadHwInfoSharedMemory(")
    ]
    assert "MultiByteToWideChar" not in hwinfo_unit_reader
    assert "unit[i] == 'C' || unit[i] == 'c'" in hwinfo_unit_reader
    assert "unit[i] == 'F' || unit[i] == 'f'" in hwinfo_unit_reader
    assert "kHwInfoRawCelsiusUnit" in hwinfo_unit_reader
    assert "kHwInfoRawFahrenheitUnit" in hwinfo_unit_reader
    assert "static_assert(HwInfoTemperatureUnit" in hwinfo_unit_reader

    assert "g_sharedVramCounter" in source
    assert 'L"\\\\GPU Adapter Memory(*)\\\\Shared Usage"' in source
    assert "enum class GpuMemoryMode" in source
    assert "UseSharedGpuMemory(*adapter, settings)" in source
    assert "kAdapterTypeQueryType = 15" in source
    assert "kHybridIntegratedAdapterFlag" in source
    assert "adapter.integrated" in source
    integrated_gpu_heuristic = source[
        source.index("bool LooksLikeIntegratedGpu(") :
        source.index("bool UseSharedGpuMemory(")
    ]
    assert "kMaximumIntegratedCarveout" in integrated_gpu_heuristic
    assert "adapter.sharedSystemMemory > 0" in integrated_gpu_heuristic
    assert 'Contains(name, L"iris")' not in integrated_gpu_heuristic
    assert "vramTotalBytes" in source
    assert "bool explicitAdapterMissing = !settings.gpuAdapter.empty() && !adapter" in source
    assert "bool gpuAvailable = false;" in source
    assert "SetTextIfChanged(g_gpuUsageText, snapshot.gpuAvailable" in source
    assert 'L"--%"' in source
    assert "RecoverFromGpuAdapterIdentityChange" in source
    assert 'RecreatePdhSources(L"confirmed adapter LUID change"' in source
    assert 'RecordPdhReadFailure(L"counter read")' in source
    assert "bool IsSoftPdhArrayAbsence(PDH_STATUS status)" in source
    assert "bool adapterSampleMissing" in source
    assert "IsSoftPdhArrayAbsence(vramReadStatus)" in source
    assert "HasGpuAdapterIdentityChanged(*adapter" in source
    assert "g_unchangedGpuIdentityChecks" in source
    assert "std::chrono::seconds(300)" in source
    assert "g_pdhGpuSampleWasAvailable && !gpuUsage" not in source
    assert "NeedsWindowsThermalZones" in source
    assert "PdhRemoveCounter(g_thermalZoneCounter)" in source
    assert 'L"\\\\Processor Information(_Total)\\\\% Processor Utility"' in source
    assert "ReadCpuUtility" in source
    assert "bool EnsurePdhQuery(const ModSettings& settings)" in source
    assert "if (EnsurePdhQuery(settings))" in source
    assert "std::optional<MetricsSnapshot> CollectMetrics" in source
    assert "if (!snapshot)" in source
    assert "g_cachedD3dkmtAdapterHandle" in source
    assert "GetD3dkmtAdapterHandle" in source
    assert "constexpr int kMaxArrayReadAttempts = 4" in source
    assert "status != static_cast<PDH_STATUS>(PDH_MORE_DATA)" in source
    assert "std::optional<double> ReadCpuUsage()" in source
    assert "bool cpuAvailable = false;" in source
    assert "bool ramAvailable = false;" in source
    assert "snapshot.cpuAvailable" in source
    assert "snapshot.ramAvailable" in source
    assert "GpuAdapterIdentityScore" in source
    assert "GpuTemperatureScore(*sensor, *label, settings.gpuTempSensor," in source

    windows_thermal_reader_start = source.index(
        "void ReadWindowsThermalZones(",
        source.index("PDH_STATUS ReadPdhArray(")
    )
    windows_thermal_reader = source[
        windows_thermal_reader_start : source.index(
            "std::optional<double> ReadGpuUsage("
        )
    ]
    assert "PDH_CSTATUS_VALID_DATA" in windows_thermal_reader
    assert "kelvin < 200.0 || kelvin > 473.15" in windows_thermal_reader
    assert "ThermalZoneAggregation::Hottest" in windows_thermal_reader
    assert "aggregate /= validCount" in windows_thermal_reader

    loaded_hook = source[
        source.index("void* WINAPI TaskbarFrame_Constructor_Hook") :
        source.index("bool HookTaskbarDllSymbols()")
    ]
    assert "sender.try_as<FrameworkElement>()" in loaded_hook
    assert "ApplyLoadedTaskbarFrame(loadedFrame);" in loaded_hook
    loaded_apply = source[
        source.index("void ApplyLoadedTaskbarFrame(") :
        source.index("using TaskbarFrame_Constructor_t")
    ]
    assert "FindOnlyTaskbarWindow()" in loaded_apply
    assert "InjectWidget(taskbarFrame)" in loaded_apply
    assert "ApplyOnTaskbarThread();" in loaded_apply
    assert "ApplyOnTaskbarThread(taskbarFrame);" in loaded_apply
    assert "loadedRoot == widgetRoot" in loaded_apply
    assert "winrt::get_abi(loadedRoot)" not in loaded_apply

    assert 'L"Shell_SecondaryTrayWnd"' in source
    assert "CSecondaryTaskBand_ITaskListWndSite_vftable" in source
    assert "CSecondaryTaskBand_GetTaskbarHost_Original" in source
    assert "FindTaskbarWindowForMonitor" in source
    assert "RemoveWidgetForMoveContext" in source
    assert "EnsureConfiguredTaskbarPlacement" in source
    assert 'Wh_Log(L"Taskbar topology changed; moving the widget")' in source
    assert 'Wh_Log(L"Retrying taskbar placement")' in source
    assert "ApplyOnTaskbarUiThread" in source
    assert "g_nextPlacementRetry" in source
    assert 'L"Taskbar placement failed; retrying in %u seconds"' in source
    assert "CoreDispatcher" not in source
    assert "RunAsync(" not in source
    assert "g_placementApplyAction" not in source
    assert "ApplyLoadedFrameFallback(context->fallbackFrame" in source
    assert "GetDisplayTopologyFingerprint(monitors)" in source
    assert "FindConfiguredTaskbarWindow(monitors, false)" in source
    assert "g_placementIsFallback" in source
    assert "g_placementLocationUnknown" in source
    assert "g_hasFailedPlacementTarget" in source
    assert 'L"Monitor selection is unavailable for the direct-frame fallback"' in source
    assert "kMaximumUnknownPlacementProbeFailures" in source
    assert "g_unknownPlacementProbesSuspended" in source
    assert "FindAnyWindowOnTaskbarThread(targetWindow)" not in source

    timer_management = source[
        source.index("void UpdateTimerInterval()") :
        source.index("ColumnDefinition PixelColumn")
    ]
    assert "g_taskbarThreadId = GetCurrentThreadId();" in timer_management
    assert "std::chrono::milliseconds(g_widget ? 250 : 1000)" in timer_management
    assert "void StopTimer()" in timer_management

    placement_wrapper = source[
        source.index("void ApplyOnTaskbarUiThread(void* contextValue)") :
        source.index("void ApplyOnTaskbarThread(")
    ]
    assert "EnsureTimer();" not in placement_wrapper

    remove_widget = source[
        source.index("void RemoveWidget()") : source.index("bool InjectWidget(")
    ]
    assert "g_timer.Stop()" not in remove_widget
    assert "UpdateTimerInterval();" in remove_widget
    remove_taskbar = source[
        source.index("void RemoveFromCurrentTaskbar(") :
        source.index("void ResetPlacementRetryState(")
    ]
    assert "StopTimer();" in remove_taskbar

    placement_impl = source[
        source.index("void ApplyOnTaskbarUiThreadImpl(") :
        source.index("void ApplyOnTaskbarUiThread(")
    ]
    assert "HWND currentWindow = g_taskbarWindow.load();" in placement_impl
    assert "FindRememberedTaskbarWindow()" not in placement_impl

    mod_init = source[source.index("BOOL Wh_ModInit()") : source.index("void Wh_ModAfterInit()")]
    taskbar_symbols_check = mod_init[
        mod_init.index("if (!HookTaskbarDllSymbols())") :
        mod_init.index("if (HMODULE module = GetTaskbarViewModule())")
    ]
    assert "return FALSE;" in taskbar_symbols_check

    assert "EnsurePdhQuery();" not in mod_init, "PDH must be initialized lazily"
    update_widget = source[
        source.index("void UpdateWidgetText(bool force = false)") :
        source.index("void EnsureTimer()")
    ]
    assert "CollectMetrics(" not in update_widget, "Metrics must stay off the UI thread"
    assert "g_gpuHistory.clear();" not in update_widget
    assert "if (!force && !hasNewSample)" in update_widget
    assert "GetMetricsSince(" in update_widget
    assert "for (const MetricsSnapshot& newSnapshot : newSnapshots)" in update_widget
    assert "std::deque<PublishedMetricsSnapshot> g_publishedMetrics" in source
    assert "SetTextIfChanged" in update_widget
    assert "std::chrono::milliseconds(250)" in source
    assert "TextTrimming::CharacterEllipsis" in source
    assert "RefreshThemeBrushes" in source
    assert "ResolveWidgetTheme" in source
    assert "kLightGraphColor" in source
    assert "SPI_GETHIGHCONTRAST" in source
    assert "COLOR_HIGHLIGHT" in source
    assert "COLOR_WINDOWTEXT" in source
    assert "COLOR_GRAYTEXT" not in source
    assert "ActualThemeChanged" in source
    assert "RefreshWidgetTheme" not in source
    assert "WidgetThemeChanged" not in source
    assert "SystemColorsChanged()" in source
    assert "RefreshThemeBrushes(const ModSettings& settings, bool" not in source
    assert "g_lastAppliedRepeaterMarginLeft" in source
    assert "margin changed externally" in source

    late_hook = source[
        source.index("bool TryHookTaskbarViewSymbols(") :
        source.index("using LoadLibraryExW_t")
    ]
    assert "kMaximumTaskbarViewHookAttempts" in late_hook
    assert "g_taskbarViewDllLoaded = false" in late_hook
    assert "Taskbar.View symbol hook failed" in late_hook
    load_library_hook_start = source.index("HMODULE WINAPI LoadLibraryExW_Hook")
    load_library_hook = source[
        load_library_hook_start :
        source.index("void CloseMetricSources()", load_library_hook_start)
    ]
    assert "if (!g_taskbarViewDllLoaded &&" in load_library_hook
    assert "g_taskbarViewHookAttempts < kMaximumTaskbarViewHookAttempts" in load_library_hook

    metrics_worker = source[
        source.index("void MetricsWorkerProc()") : source.index("bool StartMetricsWorker()")
    ]
    wait_failed = metrics_worker[
        metrics_worker.index("if (waitResult == WAIT_FAILED)") :
        metrics_worker.index("settings = CurrentSettings();", metrics_worker.index("if (waitResult == WAIT_FAILED)"))
    ]
    assert "break;" not in wait_failed
    assert "std::this_thread::sleep_for" in wait_failed
    assert "else if (waitResult == WAIT_OBJECT_0)" in metrics_worker
    assert "ApplyTemperatureHoldover(snapshot->cpuTemp" in metrics_worker
    assert "ApplyTemperatureHoldover(snapshot->gpuTemp" in metrics_worker
    assert "kTemperatureHoldoverSamples = 2" in source
    assert "HWiNFO GPU temperature readings found" in source

    inject_widget = source[
        source.index("bool InjectWidget(") : source.index("using RunFromWindowThreadProc")
    ]
    reuse_path = inject_widget[
        inject_widget.index("if (g_widget &&") :
        inject_widget.index('Wh_Log(L"Removing stale Taskbar System Info widget")')
    ]
    assert "StartMetricsWorker()" in reuse_path
    assert "EnsureTimer();" in reuse_path

    assert "code[3] == 0x28" not in source

    print(
        "Source validation OK: "
        f"{metadata['id']} v{metadata['version']}, {len(settings)} settings"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError) as error:
        print(f"Source validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
