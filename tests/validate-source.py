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
        "version": "1.1.0",
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
    assert len(settings) == 23, f"Expected 23 settings, got {len(settings)}"

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
        "sharedMemory",
        "gadgetRegistry",
        "disabled",
    }
    for options_key in ("$options", "$options:uk-UA"):
        options = temperature_source.get(options_key)
        assert isinstance(options, list), f"temperatureSource.{options_key} missing"
        assert {
            str(next(iter(option))) for option in options if isinstance(option, dict)
        } == expected_temperature_sources

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
    assert "case TemperatureSource::Disabled:" in source
    assert "NormalizeRegistryTemperature" in source
    assert "static_assert(offsetof(HwInfoHeader, pollTime) == 12);" in source
    assert "static_assert(offsetof(HwInfoReadingPrefix, value) == 284);" in source

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
