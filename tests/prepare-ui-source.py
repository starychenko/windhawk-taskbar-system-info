"""Instrument a disposable source copy; catalog builds contain no test hooks."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "taskbar-system-info.wh.cpp").read_text(encoding="utf-8")


def inject(anchor: str, replacement: str) -> None:
    global source
    if source.count(anchor) != 1:
        raise RuntimeError(f"UI test injection anchor changed: {anchor}")
    source = source.replace(anchor, replacement)


inject(
    "bool UnregisterWidgetBorderCallbacks(",
    "void (*g_borderRevokeTestHook)(DependencyProperty const&) = nullptr;\n"
    "bool UnregisterWidgetBorderCallbacks(",
)
inject(
    "host.UnregisterPropertyChangedCallback(property, *token);",
    "if (g_borderRevokeTestHook) g_borderRevokeTestHook(property);\n"
    "            host.UnregisterPropertyChangedCallback(property, *token);",
)
inject(
    "FrameworkElement FindTaskbarFrame(HWND taskbarWindow) {",
    "FrameworkElement (*g_testFindTaskbarFrame)(HWND) = nullptr;\n"
    "unsigned g_testFrameLookups = 0;\n"
    "FrameworkElement FindTaskbarFrame(HWND taskbarWindow) {\n"
    "    ++g_testFrameLookups;\n"
    "    if (g_testFindTaskbarFrame) return g_testFindTaskbarFrame(taskbarWindow);",
)
inject(
    "SecondaryWidget* AttachSecondaryWidget(HWND window, Grid root) {",
    "void (*g_secondaryAttachTestHook)() = nullptr;\n"
    "SecondaryWidget* AttachSecondaryWidget(HWND window, Grid root) {",
)
inject(
    "if (!SetTimer(window, kCopySubclass, 1000, nullptr))",
    "if (g_secondaryAttachTestHook) g_secondaryAttachTestHook();\n"
    "        if (!SetTimer(window, kCopySubclass, 1000, nullptr))",
)
inject(
    "bool RemoveWidgetCopy(HWND window) {",
    "void (*g_copyPropertyRemovedTestHook)(HWND) = nullptr;\n"
    "HANDLE RemoveCopyPropertyForTest(HWND window, LPCWSTR name) {\n"
    "    auto value = RemovePropW(window, name);\n"
    "    if (g_copyPropertyRemovedTestHook) g_copyPropertyRemovedTestHook(window);\n"
    "    return value;\n"
    "}\n"
    "bool RemoveWidgetCopy(HWND window) {",
)
copy_property_removal = "RemovePropW(window, kCopyProperty);"
if source.count(copy_property_removal) != 3:
    raise RuntimeError("Copy-property removal injection anchors changed")
source = source.replace(
    copy_property_removal, "RemoveCopyPropertyForTest(window, kCopyProperty);"
)
output = ROOT / "build-ui-smoke" / "instrumented-mod.wh.cpp"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(source, encoding="utf-8", newline="\n")
print(f"Prepared UI fixture source: {output}")
