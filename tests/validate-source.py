from __future__ import annotations

import re
import sys
from pathlib import Path

import yaml


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


def main() -> int:
    source = SOURCE_PATH.read_text(encoding="utf-8")
    metadata = parse_metadata(source)

    expected = {
        "id": "taskbar-system-info",
        "version": "1.3.3",
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

    settings = yaml.safe_load(extract_block(source, "WindhawkModSettings"))
    assert isinstance(settings, list), "Settings root must be a YAML list"
    assert len(settings) == 25, f"Expected 25 settings, got {len(settings)}"

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
    assert 'value == L"windowsThermalZones"' in source
    assert "static_assert(offsetof(HwInfoHeader, pollTime) == 12);" in source
    assert "static_assert(offsetof(HwInfoReadingPrefix, value) == 284);" in source

    temperature_dispatch = source[
        source.index("void ReadTemperatures(") : source.index("uint64_t FileTimeValue(")
    ]
    assert "case TemperatureSource::HwInfoAuto:" in temperature_dispatch
    assert "ReadHwInfoTemperatures(snapshot, settings);" in temperature_dispatch
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
    assert 'Wh_Log(L"Initial taskbar UI teardown failed; will retry")' in source
    assert 'Wh_Log(L"Taskbar UI teardown retry failed")' in source
    assert 'GetModuleHandleW(L"gdi32.dll")' in source

    format_capacity = source[
        source.index("std::wstring FormatCapacity(") :
        source.index("enum class AlertLevel")
    ]
    assert "totalGb < 1.0 ? 1 : 0" in format_capacity
    assert "FormatFixed(totalGb, totalDecimals)" in format_capacity

    shared_memory_reader = source[
        source.index("void ReadHwInfoSharedMemory(") :
        source.index("std::optional<std::wstring> ReadRegistryString(")
    ]
    assert "HwInfoHeader header{};" in shared_memory_reader
    assert "std::memcpy(&header, view, sizeof(header));" in shared_memory_reader
    assert "header->" not in shared_memory_reader
    assert "mappedSize >= sizeof(HwInfoHeader)" in shared_memory_reader
    assert "FixedAnsiToWide(reading.unit" not in shared_memory_reader
    assert "NormalizeHwInfoTemperature(" in shared_memory_reader
    assert "snapshot.cpuTemp = *value;" in shared_memory_reader
    assert "snapshot.gpuTemp = *value;" in shared_memory_reader
    assert "void ReadHwInfoGadgetRegistry(" in source
    assert "bool foundAny" not in source

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
    assert "adapter->sharedSystemMemory" in source
    assert "vramTotalBytes" in source
    assert "bool gpuAvailable = false;" in source
    assert "g_gpuUsageText.Text(snapshot.gpuAvailable" in source
    assert 'L"--%"' in source
    assert "RecoverFromGpuAdapterIdentityChange" in source
    assert 'RecreatePdhSources(L"confirmed adapter LUID change"' in source
    assert 'RecordPdhReadFailure(L"counter read")' in source
    assert "adapter && vramReadStatus == ERROR_SUCCESS" in source
    assert "HasGpuAdapterIdentityChanged(*adapter" in source
    assert "g_nextGpuIdentityCheck = now + std::chrono::seconds(5)" in source
    assert "g_pdhGpuSampleWasAvailable && !gpuUsage" not in source
    assert "NeedsWindowsThermalZones" in source
    assert "PdhRemoveCounter(g_thermalZoneCounter)" in source

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
    assert "ApplyToCurrentTaskbar(nullptr);" in loaded_hook
    assert "InjectWidget(sender" not in loaded_hook

    mod_init = source[source.index("BOOL Wh_ModInit()") : source.index("void Wh_ModAfterInit()")]
    taskbar_symbols_check = mod_init[
        mod_init.index("if (!HookTaskbarDllSymbols())") :
        mod_init.index("if (HMODULE module = GetTaskbarViewModule())")
    ]
    assert "return FALSE;" in taskbar_symbols_check

    assert "EnsurePdhQuery();" not in mod_init, "PDH must be initialized lazily"
    update_widget = source[
        source.index("void UpdateWidgetText()") : source.index("void EnsureTimer()")
    ]
    assert "CollectMetrics(" not in update_widget, "Metrics must stay off the UI thread"
    assert "g_gpuHistory.clear();" not in update_widget

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
    except (AssertionError, OSError, yaml.YAMLError) as error:
        print(f"Source validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
