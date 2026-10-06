#include <cstdlib>
#include <new>

bool observeTableAllocations = false;
bool failNextAllocation = false;
size_t sensorTableAllocation = 0, readingTableAllocation = 0, tableAllocations = 0;
void (*mutateTablePublication)() = nullptr;
void* operator new(size_t size) {
    if (failNextAllocation) {
        failNextAllocation = false;
        throw std::bad_alloc();
    }
    if (observeTableAllocations && (size == sensorTableAllocation || size == readingTableAllocation)) {
        ++tableAllocations;
        if (mutateTablePublication) mutateTablePublication();
    }
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }

#include "regression-fakes.h"
BOOL KeepPreviewHidden(HWND, int) { return FALSE; }
#define ShowWindow KeepPreviewHidden
#include "../taskbar-system-info.wh.cpp"
#undef ShowWindow
#undef PdhOpenQueryW
#undef PdhCloseQuery
#undef PdhAddEnglishCounterW
#undef PdhRemoveCounter
#undef PdhCollectQueryData
#undef PdhGetFormattedCounterValue
#undef PdhGetFormattedCounterArrayW
#undef OpenFileMappingW
#undef OpenMutexW
#undef WaitForSingleObject
#undef MapViewOfFile
#undef VirtualQuery
#undef UnmapViewOfFile
#undef ReleaseMutex
#undef CloseHandle
#undef RegOpenKeyExW
#undef RegCloseKey
#undef RegQueryValueExW
#undef RegQueryInfoKeyW
#undef RegEnumValueW
#include <iostream>
#include <limits>
#include <stdexcept>
#include <clocale>
#include <random>

namespace {
int checks = 0;
void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) throw std::runtime_error(description);
}
bool Near(double a, double b) { return std::abs(a - b) < 0.001; }

void HistoryAndScheduling() {
    using namespace std::chrono;
    {
        ModSettings settings;
        MetricsSnapshot snapshot; snapshot.capturedAt = SampleTime{} + hours(1);
        Check(MetricsSnapshotIsFresh(snapshot, settings, snapshot.capturedAt + seconds(5)),
              "normal snapshot allows five seconds of collection slack");
        Check(!MetricsSnapshotIsFresh(snapshot, settings, snapshot.capturedAt + milliseconds(5001)),
              "a stalled collector expires even without a new sequence");
        settings.updateInterval = 10;
        Check(MetricsSnapshotIsFresh(snapshot, settings, snapshot.capturedAt + seconds(30)) &&
              !MetricsSnapshotIsFresh(snapshot, settings, snapshot.capturedAt + seconds(31)),
              "slow collection expires after three configured intervals");
        Check(!MetricsSnapshotIsFresh(snapshot, settings, snapshot.capturedAt - seconds(1)) &&
              !MetricsSnapshotIsFresh({}, settings, snapshot.capturedAt),
              "future and unset snapshot timestamps are unavailable");
    }
    std::deque<HistorySample> history;
    SampleTime t{};
    for (int i = 0; i <= 6; ++i) {
        ApplyHistorySample(history, i != 3, i * 10.0, t + seconds(i), 60);
    }
    auto runs = BuildSparklineRuns(history, 60, 1, 120, 12);
    Check(history.size() == 7, "a missing sample must not erase history");
    Check(runs.size() == 2 && runs[0].size() == 3 && runs[1].size() == 3,
          "a missing sample must split graph geometry");
    Check(Near(runs[0][0].x, 108) && Near(runs[1].back().x, 120),
          "x coordinates must represent elapsed seconds");
    auto agedRuns = BuildSparklineRuns(history, 60, 1, 120, 12, t + seconds(16));
    Check(Near(agedRuns.back().back().x, 100), "a stalled graph moves left as real time advances");
    Check(BuildSparklineRuns(history, 60, 1, 120, 12, t + seconds(70)).empty(),
          "the entire graph expires without new collector samples");
    ApplyHistorySample(history, true, 25, t + seconds(20), 60);
    ApplyHistorySample(history, true, 30, t + seconds(21), 60);
    Check(BuildSparklineRuns(history, 60, 1, 120, 12).size() == 3,
          "collector stalls must produce gaps even with no explicit missing sample");
    ApplyHistorySample(history, false, 0, t + seconds(68), 60);
    Check(history.front().time == t + seconds(20), "history must expire by timestamp");
    ApplyHistorySample(history, true, std::numeric_limits<double>::quiet_NaN(),
                       t + seconds(69), 60);
    Check(!history.back().value, "NaN must create a gap");
    Check(AdvanceSampleDeadline(t + seconds(1), t + milliseconds(1250), seconds(1)) ==
              t + seconds(2), "collection time must not accumulate as timer drift");
    Check(AdvanceSampleDeadline(t + seconds(1), t + milliseconds(3250), seconds(1)) ==
              t + seconds(4), "slow collects must skip missed deadlines without a burst");
    Check(UiTimerInterval(true, true, 10) == seconds(10), "UI watchdog must scale");
    Check(UiTimerInterval(true, false, 10) == milliseconds(250), "startup stays responsive");
    Check(UiTimerInterval(false, false, 10) == seconds(1), "failed placement still retries");
    Check(Near(WidgetHeightForTaskbar(30), 30), "short taskbar must fit");
    Check(Near(WidgetHeightForTaskbar(48), 38), "normal taskbar must preserve size");
    Check(Near(WidgetHeightForTaskbar(0), 38), "unmeasured layout needs a sane fallback");
    Check(Near(WidgetHeightForTaskbar(std::numeric_limits<double>::quiet_NaN()), 38),
          "invalid layout measurement must not propagate");
    Check(FormatCapacity(0.4, 0.5, true) == L"0.4/0.5G", "512 MiB capacity must not show zero");
    const char* locale = std::setlocale(LC_NUMERIC, nullptr);
    std::string originalLocale = locale ? locale : "C";
    // Windows locale name; if unavailable, the ordinary checks still run.
    std::setlocale(LC_NUMERIC, "Ukrainian_Ukraine.1251");
    Check(FormatFixed(12.75, 1) == L"12.8", "display decimal separator must be stable");
    Check(Near(*ParseLocalizedDouble(L"12,75"), 12.75), "registry comma decimal");
    Check(Near(*ParseLocalizedDouble(L"12.75"), 12.75), "registry dot decimal");
    Check(!ParseLocalizedDouble(L"NaN"), "reject non-finite temperatures");
    Check(!ParseLocalizedDouble(L""), "reject empty temperatures");
    std::setlocale(LC_NUMERIC, originalLocale.c_str());

    GpuAdapterInfo ambiguous{L"AMD Radeon HD 6450", {}, {}, 512ull * 1024 * 1024,
                             8ull * 1024 * 1024 * 1024, false};
    ModSettings settings;
    Check(UseSharedGpuMemory(ambiguous, settings), "retain documented memory-shape auto mode");
    settings.gpuMemoryMode = GpuMemoryMode::Dedicated;
    Check(!UseSharedGpuMemory(ambiguous, settings),
          "explicit dedicated override must resolve ambiguous legacy 512 MiB GPUs");
}

TaskbarPlacement ResolveTaskbarPlacement(const ModSettings& settings, const TaskbarGeometry& geometry,
                                        PreferredPosition preferred, bool allowReservation = true) {
    return ResolvePlacementForSize(settings, geometry, preferred, settings.width,
        std::max(0.85, 9.0 / settings.fontSize), kWidgetHeight, allowReservation);
}

void AdaptivePlacement() {
    ModSettings settings; settings.width = 410;
    TaskbarGeometry geometry{1920, 48, {{0, 300, true}, {1800, 1920, false}}, true};
    auto p = ResolveTaskbarPlacement(settings, geometry, {2000, 0});
    Check(Near(p.left, 1384) && Near(p.width, 410) && Near(p.reserved, 0), "prefer the far-right free slot with breathing room before tray");
    settings.reserveSpace = true;
    p = ResolveTaskbarPlacement(settings, geometry, {0, 0});
    Check(Near(p.left, 6) && Near(p.width, 410) && Near(p.reserved, 424), "reserve before actual buttons with an inset at the panel edge");
    Check(PlacementFits(geometry, p), "reservation must leave the widget and shifted buttons separate");
    p = ResolveTaskbarPlacement(settings, geometry, {2000, 0});
    Check(Near(p.left, 1384) && Near(p.reserved, 0), "reservation need not move buttons when a nearer gap exists");
    settings.reserveSpace = false;
    geometry.occupied = {{0, 100}, {550, 700}, {1150, 1920}};
    p = ResolveTaskbarPlacement(settings, geometry, {600, 0});
    Check(Near(p.left, 706) && Near(p.width, 410), "choose nearest full-width gap between independent elements with clearance");
    geometry = {800, 48, {{0, 100}, {472, 800}}, true};
    p = ResolveTaskbarPlacement(settings, geometry, {200, 200});
    Check(Near(p.left, 106) && Near(p.width, 360), "shrink only when no full size slot exists, preserving side clearance");
    geometry.occupied[1].left = 452;
    Check(ResolveTaskbarPlacement(settings, geometry, {0, 0}).width == 0, "hide below 85 percent scale");
    geometry.occupied[1].left = 472; settings.fontSize = 9;
    Check(ResolveTaskbarPlacement(settings, geometry, {0, 0}).width == 0, "nine-DIP text must not become smaller");
    settings.fontSize = 11; geometry.height = 30;
    Check(ResolveTaskbarPlacement(settings, geometry, {0, 0}).width == 0, "respect readability when height constrains scale");
    geometry.height = 48; geometry.ready = false;
    Check(ResolveTaskbarPlacement(settings, geometry, {0, 0}).width == 0, "unknown layout is not a free slot");
    geometry = {1212, 48, {{0, 100}, {522, 690}, {1112, 1212}}, true};
    p = ResolveTaskbarPlacement(settings, geometry, {401, 696});
    Check(Near(p.left, 696), "equal-distance choices preserve the preceding slot");
    Check(!PlacementFits(geometry, {100, 410, 0}) && PlacementFits(geometry, {106, 410, 0}),
          "touching a button is invalid while a six-DIP gap is accepted");
    Check(!PlacementFits({500, 48, {}, true}, {0, 410, 0}) &&
          PlacementFits({500, 48, {}, true}, {6, 410, 0}),
          "panel edges keep the same minimum clearance without any mapped controls");
    auto free = FreeTaskbarIntervals(1000, {{100, 300}, {250, 400}, {-30, 10}, {900, 1200}});
    Check(free.size() == 2 && Near(free[0].left, 10) && Near(free[0].right, 100) &&
          Near(free[1].left, 400) && Near(free[1].right, 900), "merge overlaps and clip to the panel");
    for (double physical : {1280., 1920., 2560., 3840., 5120.}) {
        for (double scale : {1., 1.25, 1.5, 2.}) {
            for (bool reserve : {false, true}) {
                settings.reserveSpace = reserve;
                double width = physical / scale;
                geometry = {width, 48, {{20, 300, true}, {width - 180, width}}, true};
                p = ResolveTaskbarPlacement(settings, geometry, {2000, 0});
                Check(p.width == 0 || (PlacementFits(geometry, p) && p.width >= 348.5),
                      "DPI matrix must preserve mapped elements and readability");
            }
        }
    }
    geometry = {1920, 48, {{0, 300, true}, {1800, 1920}}, true};
    settings.reserveSpace = true;
    p = ResolveTaskbarPlacement(settings, geometry, {0, 0}, false);
    Check(p.reserved == 0 && Near(p.left, 306), "rejected reservation falls back to an existing gap");
    Check(!PlacementFits(geometry, {0, 410, 0}), "independent intersection check detects a button overlap");
    TaskbarGeometry arranged{1920, 48, {{1450, 1850, true}, {1800, 1920}}, true};
    Check(PlacementFits(arranged, {6, 410, 0}) && !ReservedControlsFit(arranged),
          "arranged buttons touching the tray reject reservation even when the widget itself is clear");
    arranged.occupied[0] = {1418, 1718, true};
    Check(ReservedControlsFit(arranged), "actual reservation keeps all moved buttons within their own gap");
    arranged.occupied[0] = {1900, 2100, true};
    Check(PlacementFits(arranged, {6, 410, 0}) && !ReservedControlsFit(arranged),
          "unreserved clipped buttons do not hide a free widget but cannot be reserved beyond the panel");
    settings.reserveGap = 0;
    p = ResolveTaskbarPlacement(settings, geometry, {0, 0});
    Check(Near(p.left, 6) && Near(p.reserved, 422),
          "a zero configured reservation gap still retains six DIP visual clearance");
}

void ResponsiveLayouts() {
    ModSettings settings; settings.width = 410; settings.reserveSpace = false;
    WidgetFontMetrics font;
    auto resolve = [&](double width, double height) {
        return ResolveWidgetPlacement(settings, {width + 12, height, {}, true}, {6, 6}, font);
    };
    auto normal = resolve(410, 48);
    Check(normal.layout.legacy && Near(normal.placement.width, 410) && Near(normal.layout.height, 38),
          "normal taskbar preserves the original layout");
    auto narrow = resolve(330, 48);
    Check(narrow.placement.width > 0 && narrow.layout.mode == WidgetLayoutMode::NoGraphs &&
          narrow.layout.showMemoryDetails && !narrow.layout.showGraphs && Near(narrow.layout.scale, 1),
          "width pressure removes graphs before memory details or readable text");
    auto shortBar = resolve(410, 30);
    Check(shortBar.placement.width > 0 && shortBar.layout.mode == WidgetLayoutMode::CompactTwoRows &&
          Near(shortBar.layout.scale, 1) && !shortBar.layout.showMemoryDetails,
          "thirty-DIP taskbar retains all essential readings without text shrinking");
    settings.width = 430;
    auto low = resolve(430, 20);
    Check(low.placement.width > 0 && low.layout.mode == WidgetLayoutMode::CompactOneRow &&
          Near(low.layout.scale, 1), "one row uses horizontal room on a low taskbar");
    settings.width = 410;
    auto tiny = resolve(100, 20);
    Check(tiny.placement.width == 0, "hide when neither essential layout can preserve nine-DIP text");
    auto reduced = resolve(200, 26);
    Check(reduced.placement.width > 0 && reduced.layout.scale < 1 &&
          reduced.layout.scale * settings.fontSize >= 9 - 1e-6,
          "compact fallback scales only within the minimum effective font size");
    auto restore = resolve(410, 48);
    Check(SameWidgetLayout(normal.layout, restore.layout) && settings.width == 410,
          "adaptive fallback restores the original layout and configured width");
    // The width preference belongs to the full layout. A low panel may need
    // a wider one-row layout to retain the configured nine-DIP font.
    {
        auto lowSettings = settings; lowSettings.width = 330; lowSettings.fontSize = 9;
        auto lowFont = font; lowFont.textHeight = 11;
        TaskbarGeometry widePanel{1500, 20, {}, true};
        auto readable = ResolveWidgetPlacement(lowSettings, widePanel, {350, 350}, lowFont);
        Check(readable.placement.width > 330 && readable.layout.mode == WidgetLayoutMode::CompactOneRow &&
              Near(readable.layout.scale, 1) && PlacementFits(widePanel, readable.placement) && lowSettings.width == 330,
              "a measured one-row layout wider than the full-width preference retains nine-DIP text");
        auto saved = ConfirmPlacementPreference({}, lowSettings, L"wide-compact", readable.placement.left,
                                                widePanel.width, false, readable.placement.width);
        auto restored = ResolveWidgetPlacement(lowSettings, widePanel,
            {0, readable.placement.left, saved->positions.at(L"wide-compact")}, lowFont);
        Check(Near(restored.placement.left, readable.placement.left) && SameWidgetLayout(restored.layout, readable.layout),
              "a compact layout wider than the preference survives confirmation without shifting");
        widePanel.width = readable.layout.width + 11; // One DIP less than its measured width plus clearance.
        Check(ResolveWidgetPlacement(lowSettings, widePanel, {6, 6}, lowFont).placement.width == 0,
              "measured compact width still obeys the actual free gap and nine-DIP floor");
    }
    auto heldSettings = g_settings;
    for (int size : {9, 11, 13}) {
        settings.fontSize = size;
        g_settings = std::make_shared<ModSettings>(settings);
        double factor = size / 11.0;
        font = WidgetFontMetrics{};
        for (auto& width : font.widths) width *= factor;
        font.textHeight = std::ceil(15 * factor);
        for (double height : {20., 24., 30., 38., 48.}) {
            for (double width : {80., 150., 180., 200., 260., 330., 410., 600.}) {
                for (double dpi : {1., 1.25, 1.5, 1.75, 2.}) {
                    TaskbarGeometry geometry{width + 212, height, {{0, 100, true}, {width + 112, width + 212}}, true};
                    auto result = ResolveWidgetPlacement(settings, geometry, {106, 106}, font);
                    const auto& layout = result.layout;
                    Check(result.placement.width == 0 || (PlacementFits(geometry, result.placement) &&
                        layout.scale * size >= 9 - 1e-6 && layout.scale * layout.height <= height + 1e-6 &&
                        layout.scale * layout.width <= result.placement.width + 1e-6),
                        "responsive width/height/font/DPI matrix preserves controls and readability");
                    if (result.placement.width == 0) continue;
                    MoveEditorState editor; editor.visual.fontMetrics = font;
                    editor.target.geometry = geometry; editor.target.scale = dpi; editor.candidate = result.placement;
                    auto preview = ResolveMovePreviewLayout(editor, {0, 0, 4000, 2000});
                    Check(SameWidgetLayout(preview.widget, layout) && Near(preview.contentScale, layout.scale * dpi),
                          "destination preview shares exactly the widget layout and physical scale");
                    for (int index : {0, 1, 2, 3, 4, 6, 7, 8, 9, 10}) {
                        const auto& cell = layout.cells[index];
                        Check(cell.Width > 0 && cell.X >= 0 && cell.Y >= 0 &&
                            cell.X + cell.Width <= layout.width + .01 && cell.Y + cell.Height <= layout.height + .01,
                            "all essential cells remain inside the chosen intrinsic layout");
                    }
                }
            }
        }
    }
    g_settings = heldSettings;
    settings.fontSize = 11; font = WidgetFontMetrics{};
    for (bool reserve : {false, true}) {
        settings.reserveSpace = reserve;
        TaskbarGeometry geometry{700, 30, {{20, 300, true}, {580, 700}}, true};
        auto result = ResolveWidgetPlacement(settings, geometry, {6, 6}, font);
        Check(result.placement.width > 0 && PlacementFits(geometry, result.placement),
              "adaptive layout retains reservation and clearance on short taskbars");
    }
    settings.reserveSpace = false;
    TaskbarGeometry compactPanel{700, 30, {{0, 300, true}, {580, 700}}, true};
    auto draft = ResolveWidgetPlacement(settings, compactPanel, {600, 600}, font);
    auto confirmed = ConfirmPlacementPreference({}, settings, L"compact-display", draft.placement.left, 700, false, draft.placement.width);
    auto fraction = confirmed->positions.at(L"compact-display");
    auto applied = ResolveWidgetPlacement(settings, compactPanel,
        {fraction * (700 - settings.width), draft.placement.left, fraction}, font);
    Check(Near(applied.placement.left, draft.placement.left),
          "saving a compact placement must not shift it away from the preview");
    auto encoded = ParseProfiles(SerializeProfiles(*confirmed));
    Check(encoded && Near(encoded->positions.at(L"compact-display"), fraction) && settings.width == 410,
          "compact anchor uses the unchanged profile format and does not change configured width");
    for (double panelWidth : {700., 1280., 1920.}) {
        auto geometry = compactPanel; geometry.width = panelWidth;
        geometry.occupied.back() = {panelWidth - 120, panelWidth};
        auto anchored = ResolveWidgetPlacement(settings, geometry, {0, draft.placement.left, fraction}, font);
        auto reference = ResolveWidgetPlacement(settings, geometry,
            {fraction * (panelWidth - anchored.placement.width), draft.placement.left}, font);
        Check(Near(anchored.placement.left, reference.placement.left) &&
            Near(confirmed->positions.at(L"compact-display"), fraction),
            "saved fraction adapts to layout and panel width without rewriting the profile");
    }
    Check(!ConfirmPlacementPreference({}, settings, L"compact-display", 6, 700, false,
        std::numeric_limits<double>::quiet_NaN()), "invalid actual widget width cannot be saved");
    Check(ResolveWidgetPlacement(settings, {700, 30, {}, false}, {6, 6}, font).placement.width == 0,
          "unknown geometry cannot select a visible adaptive layout");

    // Confirming a snapped anchor must not change the layout selected in preview.
    // A closer, narrower reservation used to beat a more readable distant slot.
    settings.width = 766; settings.fontSize = 12; settings.reserveSpace = true; settings.reserveGap = 26;
    font = WidgetFontMetrics{};
    for (auto& width : font.widths) width *= 12.0 / 11;
    font.textHeight = 17;
    TaskbarGeometry reservedPanel{1346, 30, {{399, 638, true}, {1202, 1346}, {718, 798}}, true};
    draft = ResolveWidgetPlacement(settings, reservedPanel, {489, 0}, font);
    confirmed = ConfirmPlacementPreference({}, settings, L"reserved-display", draft.placement.left,
                                            reservedPanel.width, false, draft.placement.width);
    encoded = ParseProfiles(SerializeProfiles(*confirmed));
    applied = ResolveWidgetPlacement(settings, reservedPanel,
        {0, draft.placement.left, encoded->positions.at(L"reserved-display")}, font);
    Check(draft.placement.width > 0 && PlacementFits(reservedPanel, draft.placement) &&
          SameWidgetLayout(draft.layout, applied.layout) && Near(draft.layout.scale, applied.layout.scale) &&
          Near(draft.placement.left, applied.placement.left) && Near(draft.placement.width, applied.placement.width),
          "confirming a reserved adaptive placement preserves the preview layout, scale and position");
}

void PlacementConfirmationMatrix() {
    std::mt19937 random(20261004);
    auto pick = [&](int low, int high) { return std::uniform_int_distribution<int>(low, high)(random); };
    for (int i = 0; i < 100000; ++i) {
        ModSettings settings; settings.width = pick(330, 800); settings.fontSize = pick(9, 13);
        settings.reserveSpace = pick(0, 1); settings.reserveGap = pick(0, 100);
        WidgetFontMetrics font;
        for (auto& width : font.widths) width *= settings.fontSize / 11.0;
        font.textHeight = std::ceil(15 * settings.fontSize / 11.0);
        TaskbarGeometry geometry{static_cast<double>(pick(250, 2500)), static_cast<double>(pick(15, 64)), {}, true};
        double start = pick(0, static_cast<int>(geometry.width) / 3), stop = start + pick(30, 300);
        geometry.occupied = {{start, stop, true}, {geometry.width - pick(70, 200), geometry.width}};
        if (pick(0, 1)) geometry.occupied.push_back({stop + 80, stop + 160});
        double wanted = pick(0, 2500);
        auto draft = ResolveWidgetPlacement(settings, geometry, {wanted, 0}, font);
        if (!draft.placement.width) continue;
        Check(PlacementFits(geometry, draft.placement), "generated preview preserves obstacle clearance");
        auto saved = ConfirmPlacementPreference({}, settings, L"matrix-display", draft.placement.left,
                                                geometry.width, false, draft.placement.width);
        auto encoded = ParseProfiles(SerializeProfiles(*saved));
        auto restored = ResolveWidgetPlacement(settings, geometry,
            {0, draft.placement.left, encoded->positions.at(L"matrix-display")}, font);
        Check(std::abs(restored.placement.left - draft.placement.left) <= kPlacementTolerance &&
              Near(restored.placement.width, draft.placement.width) &&
              Near(restored.placement.reserved, draft.placement.reserved) &&
              SameWidgetLayout(restored.layout, draft.layout) && Near(restored.layout.scale, draft.layout.scale),
              "generated preview survives serialization and confirmation without changing placement or layout");
    }
}

void DraggedLayouts() {
    auto heldSettings = g_settings;
    ModSettings settings; settings.width = 410; settings.fontSize = 11; settings.reserveSpace = false;
    g_settings = std::make_shared<ModSettings>(settings);
    WidgetFontMetrics font;
    struct Case { TaskbarGeometry geometry; WidgetLayoutMode mode; };
    const Case cases[] = {
        {{1200, 48, {{0, 100, true}, {1050, 1200}}, true}, WidgetLayoutMode::Full},
        {{750, 48, {{0, 300, true}, {600, 750}}, true}, WidgetLayoutMode::NoGraphs},
        {{1200, 30, {{0, 100, true}, {1050, 1200}}, true}, WidgetLayoutMode::CompactTwoRows},
        {{1200, 20, {{0, 100, true}, {1050, 1200}}, true}, WidgetLayoutMode::CompactOneRow},
        // The legacy Viewbox is wider than its content when height constrains scaling.
        {{1200, 33, {{0, 100, true}, {1050, 1200}}, true}, WidgetLayoutMode::Full},
    };
    RECT display{-10000, -10000, 10000, 10000};
    for (const auto& scenario : cases) {
        auto initial = ResolveWidgetPlacement(settings, scenario.geometry, {350, 350}, font);
        Check(initial.placement.width > 0 && initial.layout.mode == scenario.mode,
              "drag fixtures exercise every adaptive layout and the height-scaled original");
        for (double dpi : {1., 1.25, 1.5, 1.75, 2.}) {
            MoveEditorState source; source.visual.fontMetrics = font; source.candidate = initial.placement;
            source.target.geometry = scenario.geometry; source.target.origin = {-1920, -900}; source.target.scale = dpi;
            auto shown = ResolveMovePreviewLayout(source, display);
            double screenLeft = shown.bounds.left + shown.content.left;
            LONG contentWidth = shown.content.right - shown.content.left;
            for (double fraction : {0., .25, .5, .75, .95}) {
                POINT cursor{static_cast<LONG>(screenLeft + std::lround(contentWidth * fraction)), -890};
                double captured = (cursor.x - screenLeft) / contentWidth;
                WidgetPointerAnchor anchor{ScreenPointToTaskbarX(source.target, cursor), captured};
                auto stayed = ResolveWidgetPlacement(settings, scenario.geometry,
                    {0, initial.placement.left, std::nullopt, anchor}, font);
                source.candidate = stayed.placement;
                auto after = ResolveMovePreviewLayout(source, display);
                double heldPoint = after.bounds.left + after.content.left + captured * (after.content.right - after.content.left);
                Check(stayed.placement.width > 0 && SameWidgetLayout(initial.layout, stayed.layout) &&
                      std::abs(heldPoint - cursor.x) <= 1,
                      "press/release without moving keeps the held content point within one physical pixel");
                for (double destinationHeight : {20., 24., 30., 38., 48.}) {
                    for (double destinationDpi : {1., 1.25, 1.5, 1.75, 2.}) {
                        TaskbarProjection target; target.geometry = {2000, destinationHeight,
                            {{0, 100, true}, {1850, 2000}}, true};
                        target.origin = {2560, 100}; target.scale = destinationDpi;
                        POINT destinationCursor{static_cast<LONG>(target.origin.x + 1000 * destinationDpi), 110};
                        WidgetPointerAnchor destinationAnchor{ScreenPointToTaskbarX(target, destinationCursor), captured};
                        auto moved = ResolveWidgetPlacement(settings, target.geometry,
                            {0, stayed.placement.left, std::nullopt, destinationAnchor}, font);
                        MoveEditorState destination; destination.visual.fontMetrics = font;
                        destination.target = target; destination.candidate = moved.placement;
                        auto preview = ResolveMovePreviewLayout(destination, display);
                        double point = preview.bounds.left + preview.content.left + captured * (preview.content.right - preview.content.left);
                        Check(moved.placement.width > 0 && PlacementFits(target.geometry, moved.placement) &&
                              SameWidgetLayout(moved.layout, preview.widget) && std::abs(point - destinationCursor.x) <= 1,
                              "destination layout and DPI preserve the grabbed point when unobstructed");
                        double pendingLeft = PreferredLeftForWidget(
                            {0, 0, std::nullopt, destinationAnchor}, target.geometry,
                            moved.placement.width, moved.layout.width, moved.layout.height);
                        auto released = ResolveWidgetPlacement(settings, target.geometry,
                            {pendingLeft, moved.placement.left}, font);
                        Check(Near(released.placement.left, moved.placement.left) && SameWidgetLayout(released.layout, moved.layout),
                              "releasing the drag retains the preview position for idle refresh and confirmation");
                    }
                }
            }
        }
    }
    auto blocked = ResolveWidgetPlacement(settings, {700, 30, {{0, 300, true}, {580, 700}}, true},
        {0, 0, std::nullopt, WidgetPointerAnchor{20, .5}}, font);
    Check(blocked.placement.width > 0 && PlacementFits({700, 30, {{0, 300, true}, {580, 700}}, true}, blocked.placement),
          "cursor anchoring still snaps clear of occupied taskbar buttons");
    g_settings = heldSettings;
}

void MovePreferences() {
    TaskbarProjection projection;
    projection.origin = {-1920, -900}; projection.scale = 1.5;
    Check(Near(ScreenPointToTaskbarX(projection, {-1125, -850}), 530),
          "negative display origins and 150 percent DPI convert physical cursor coordinates");
    projection.origin = {2560, 0}; projection.scale = 2;
    Check(Near(ScreenPointToTaskbarX(projection, {3560, 20}), 500),
          "200 percent destination uses its own logical coordinate scale");
    MoveEditorState editor;
    editor.visual.ready = true; editor.visual.width = 410;
    editor.target.origin = {-1920, -1080}; editor.target.scale = 1.5;
    editor.target.geometry = {1280, 48, {}, true}; editor.candidate = {100, 410, 0};
    auto preview = ResolveMovePreviewLayout(editor, {-1920, -1080, 0, 0});
    Check(preview.bounds.left + preview.content.left == -1770 && preview.content.right - preview.content.left == 615,
          "glass padding preserves actual widget coordinates at a negative-origin 150 percent display");
    Check(preview.bounds.top == -1078 && preview.bounds.bottom == -1011 &&
          preview.bounds.right - preview.bounds.left == 615 && preview.body.top == 0,
          "top taskbar preview contains only the widget with no instruction surface");
    editor.target.origin = {0, 1032}; editor.target.scale = 1;
    editor.target.geometry = {1920, 48, {}, true}; editor.candidate.left = 300;
    preview = ResolveMovePreviewLayout(editor, {0, 0, 1920, 1080});
    Check(preview.bounds.top == 1034 && preview.bounds.bottom == 1078 &&
          preview.bounds.top + preview.content.top == 1037,
          "bottom taskbar preview stays within the panel without moving the proposed widget");
    editor.candidate.width = 348.5;
    preview = ResolveMovePreviewLayout(editor, {0, 0, 1920, 1080});
    Check(Near(preview.contentScale, .85) && preview.content.bottom - preview.content.top == 32,
          "live preview follows the accepted readable widget scale");
    auto hotkey = ParseMoveHotkey(L"Ctrl+Alt+M");
    Check(hotkey && hotkey->key == 'M' && hotkey->modifiers == (MOD_CONTROL | MOD_ALT), "default move combination");
    Check(ParseMoveHotkey(L" shift + WIN + f24 ")->key == VK_F24, "case and whitespace in hotkeys");
    Check(ParseMoveHotkey(L"")->key == 0, "empty disables the shortcut");
    for (auto invalid : {L"Ctrl+Ctrl+M", L"Ctrl+", L"A+B", L"F25", L"Ctrl+Mouse1", L"Ctrl", L"+M", L"F01", L"F 2", L"F-2"}) {
        std::wstring reason;
        Check(!ParseMoveHotkey(invalid, &reason) && !reason.empty(), "malformed hotkeys have a specific rejection reason");
    }
    PlacementProfiles profiles{2, 2000, L"display-b", {{L"display-a", .25}, {L"display-b", .75}}};
    auto saved = SerializeProfiles(profiles); auto parsed = ParseProfiles(saved);
    Check(parsed && parsed->target == L"display-b" && parsed->monitor == 2 && parsed->offset == 2000 &&
          Near(parsed->positions.at(L"display-a"), .25), "per-display positions survive serialization");
    Check(SerializeProfiles(*parsed) == saved, "profile serialization is deterministic");
    for (auto invalid : {L"", L"2\n1 10\n\n", L"1\n33 10\n\n", L"1\n1 -10\n\n", L"1\n1 10\nx\t\n",
                         L"1\n1 10\n\nx\tNaN\n", L"1\n1 10\n\nx\t1.1\n", L"1\n1 10\n\nx\t.1\nx\t.2\n",
                         L"1\n1 \n\n", L"1\n1 2147483648\n\n", L"1\n1  10\n\n", L"1\n1 10junk\n\n"})
        Check(!ParseProfiles(invalid), "corrupt saved data must not become a placement");
    ModSettings settings; settings.monitor = 2; settings.leftOffset = 2000;
    auto reconciled = profiles;
    settings.fontSize = 16; settings.width = 600;
    Check(!ReconcileProfiles(reconciled, settings, L"display-b") && SerializeProfiles(reconciled) == saved,
          "appearance settings preserve target and every display preference");
    settings.leftOffset = 20;
    Check(ReconcileProfiles(reconciled, settings, L"display-b") && reconciled.target == L"display-b" &&
          !reconciled.positions.contains(L"display-b") && Near(reconciled.positions.at(L"display-a"), .25),
          "offset-only edit retains the dragged target and other displays");
    Check(SavePlacementProfiles(reconciled), "save an offset-only edit with a target but no dragged position");
    SetPlacementProfiles({}); LoadPlacementProfiles();
    auto reloaded = PlacementProfilesSnapshot();
    Check(reloaded.target == L"display-b" && reloaded.offset == 20 && reloaded.positions.size() == 1 &&
          Near(reloaded.positions.at(L"display-a"), .25), "offset-only edit survives real storage reload");
    Check(ParseProfiles(L"1\n2 20\ndisplay-b\n")->target == L"display-b",
          "a lone dragged display may use its manual offset");
    reconciled = profiles; settings.leftOffset = 2000;
    settings.monitor = 1;
    Check(ReconcileProfiles(reconciled, settings, L"display-a") && reconciled.target.empty() && reconciled.positions.size() == 2,
          "manual monitor change releases dragged target and retains per-display positions");
    settings.leftOffset = 20;
    Check(ReconcileProfiles(reconciled, settings, L"display-a") && !reconciled.positions.contains(L"display-a") &&
          Near(reconciled.positions.at(L"display-b"), .75), "manual offset change clears only the current display position");
    auto confirmed = ConfirmPlacementPreference(profiles, settings, L"display-c", 700, 2000, false);
    Check(confirmed && confirmed->target == L"display-c" && Near(confirmed->positions.at(L"display-c"), .5) &&
          confirmed->positions.size() == 3, "confirmed move saves a fraction of full-width logical travel");
    auto reset = ConfirmPlacementPreference(profiles, settings, L"", 0, 2000, true);
    Check(reset && reset->target.empty() && reset->positions.empty() && reset->monitor == 1 && reset->offset == 20,
          "confirmed Home clears all drag preferences and uses current manual settings");
    Check(profiles.target == L"display-b" && profiles.positions.size() == 2,
          "drafting a move or reset leaves the preceding saved preference intact");
    Check(!ConfirmPlacementPreference(profiles, settings, L"", 0, 2000, false), "display without stable identity cannot be saved");
    auto limited = profiles;
    for (int i = 0; limited.positions.size() < 32; ++i) limited.positions[L"screen-" + std::to_wstring(i)] = .5;
    Check(!ConfirmPlacementPreference(limited, settings, L"new-screen", 0, 2000, false) &&
          ConfirmPlacementPreference(limited, settings, L"display-b", 0, 2000, false).has_value(),
          "profile bound rejects only new displays and still allows updating an existing one");
    SetPlacementProfiles(profiles);
    Check(PlacementProfilesSnapshot().target == L"display-b", "profile snapshots are copied safely");
    for (int failure = 0; failure < 5; ++failure) {
        fake::localSaveFails = false; SavePlacementProfiles(profiles);
        int applied = 0, verified = 0, restored = 0;
        auto epoch = g_moveEpoch.load();
        fake::localSaveFails = failure == 2;
        bool success = CompleteMoveTransaction(profiles, *confirmed, epoch, [&] {
            ++applied;
            if (failure == 3) CancelMoveEditor();
            if (failure == 4) throw std::runtime_error("injected transfer exception");
            return failure != 0;
        }, [&] { ++verified; return failure != 1; }, [&] { ++restored; return true; });
        Check(!success && applied == 1 && restored == 1 && !g_moveCommitting &&
              PlacementProfilesSnapshot().target == profiles.target &&
              ParseProfiles(fake::localStorage[L"placement.v1"])->target == profiles.target,
              "failed transfer, layout, storage, cancellation or exception restores preference and releases commit state");
        Check(verified == (failure == 0 || failure == 4 ? 0 : 1), "unapplied or throwing transfers are never verified or saved");
    }
    fake::localSaveFails = false; int restored = 0;
    Check(CompleteMoveTransaction(profiles, *confirmed, g_moveEpoch.load(), [] { return true; },
          [] { return true; }, [&] { ++restored; return true; }) && !restored && !g_moveCommitting &&
          ParseProfiles(fake::localStorage[L"placement.v1"])->target == L"display-c",
          "successful transfer and verification persist the confirmed target without rollback");
    Check(CompleteMoveTransaction(*confirmed, *reset, g_moveEpoch.load(), [] { return true; },
          [] { return true; }, [] { return true; }) && PlacementProfilesSnapshot().positions.empty() &&
          ParseProfiles(fake::localStorage[L"placement.v1"])->target.empty(), "confirmed Home reset is persisted only after successful placement");
    fake::localStorage.clear();
    SetPlacementProfiles({});
}

void SetMapping(std::vector<HwInfoSensorPrefix> sensors,
                std::vector<HwInfoReadingPrefix> readings) {
    HwInfoHeader header{};
    header.signature = kHwInfoSignature;
    header.version = 2;
    header.revision = 1;
    header.pollingPeriod = 2000;
    header.pollTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    header.sensorOffset = sizeof(header);
    header.sensorStride = sizeof(HwInfoSensorPrefix);
    header.sensorCount = static_cast<uint32_t>(sensors.size());
    header.readingOffset = header.sensorOffset + header.sensorStride * header.sensorCount;
    header.readingStride = sizeof(HwInfoReadingPrefix);
    header.readingCount = static_cast<uint32_t>(readings.size());
    fake::mappingOffset = 16;
    fake::mapping.resize(fake::mappingOffset + header.readingOffset +
                         header.readingStride * header.readingCount);
    BYTE* bytes = fake::mapping.data() + fake::mappingOffset;
    std::memcpy(bytes, &header, sizeof(header));
    std::memcpy(bytes + header.sensorOffset, sensors.data(), sensors.size() * sizeof(sensors[0]));
    std::memcpy(bytes + header.readingOffset, readings.data(), readings.size() * sizeof(readings[0]));
}
HwInfoSensorPrefix Sensor(uint32_t id, const char* name) {
    HwInfoSensorPrefix sensor{};
    sensor.sensorId = id;
    std::strcpy(sensor.originalName, name);
    return sensor;
}
HwInfoReadingPrefix Reading(uint32_t sensor, uint32_t id, const char* label, double value) {
    HwInfoReadingPrefix reading{};
    reading.readingType = kHwInfoTemperatureType;
    reading.sensorIndex = sensor;
    reading.readingId = id;
    reading.value = value;
    std::strcpy(reading.originalLabel, label);
    std::strcpy(reading.unit, "C");
    return reading;
}
MetricsSnapshot SharedSnapshot() {
    MetricsSnapshot snapshot;
    HwInfoTemperatureDiagnostics diagnostics;
    ReadHwInfoSharedMemory(snapshot, ModSettings{}, std::nullopt, diagnostics);
    return snapshot;
}
void SensorFreshness() {
    using namespace std::chrono;
    HwInfoHeader header{}; header.revision = 1; header.pollingPeriod = 2000;
    header.pollTime = 1000000;
    HwInfoSharedMemoryCache cache;
    auto now = SampleTime{} + hours(1);
    Check(HwInfoPublicationIsFresh(header, cache, 1000000, now), "current HWiNFO publication is fresh");
    Check(HwInfoPublicationIsFresh(header, cache, 1000000, now + seconds(8)) &&
          !HwInfoPublicationIsFresh(header, cache, 1000000, now + milliseconds(8001)),
          "frozen poll time expires by monotonic clock despite wall-clock rollback");
    ++header.pollTime;
    Check(HwInfoPublicationIsFresh(header, cache, 1000001, now + seconds(9)),
          "resumed HWiNFO polling recovers without a cache restart");
    Check(!HwInfoPublicationIsFresh(header, cache, 1000030, now + seconds(10)),
          "an ancient initial publication is unavailable");
    header.pollTime = 0;
    Check(!HwInfoPublicationIsFresh(header, cache, 1000001, now), "unset poll timestamp is unavailable");
    header.pollTime = 1000010;
    Check(!HwInfoPublicationIsFresh(header, cache, 1000001, now), "future poll timestamp is unavailable");
    header.pollTime = 1000000; header.pollingPeriod = 10000; cache = {};
    Check(HwInfoPublicationIsFresh(header, cache, 1000020, now) &&
          HwInfoPublicationIsFresh(header, cache, 1000032, now + seconds(32)) &&
          !HwInfoPublicationIsFresh(header, cache, 1000033, now + seconds(33)),
          "long HWiNFO polling periods receive proportional slack");
    header.revision = 0; header.pollingPeriod = 0; cache = {};
    Check(HwInfoPublicationIsFresh(header, cache, 1000007, now) &&
          !HwInfoPublicationIsFresh(header, cache, 1000009, now + seconds(2)),
          "older header revisions use the default polling interval");

    g_hwInfoSharedMemoryCache = {};
    SetMapping({Sensor(1, "CPU")}, {Reading(0, 101, "CPU Package", 72)});
    auto* mappedHeader = reinterpret_cast<HwInfoHeader*>(fake::mapping.data() + fake::mappingOffset);
    auto currentPoll = mappedHeader->pollTime;
    mappedHeader->pollTime = 1;
    Check(!SharedSnapshot().cpuTemp && !g_hwInfoLayoutRejectedLogged,
          "reader refuses stale publication without misclassifying its valid layout");
    mappedHeader->pollTime = currentPoll;
    Check(SharedSnapshot().cpuTemp == 72, "reader accepts renewed polling with unchanged temperature");
    g_hwInfoSharedMemoryCache.lastPollChange = SampleTime::clock::now() - seconds(9);
    Check(!SharedSnapshot().cpuTemp, "reader refuses a frozen cached publication");
    fake::registry = {{L"Sensor0", L"CPU"}, {L"Label0", L"CPU Package"},
                      {L"ValueRaw0", L"45"}, {L"Value0", L"45 \u00B0C"}};
    fake::TouchRegistry();
    g_hwInfoGadgetRegistryCache = {};
    MetricsSnapshot fallback; HwInfoTemperatureDiagnostics diagnostics;
    ReadHwInfoTemperatures(fallback, ModSettings{}, std::nullopt, diagnostics);
    Check(fallback.cpuTemp == 45 && fallback.cpuTempProvider == TemperatureProvider::HwInfoGadgetRegistry,
          "stale shared memory releases the HWiNFO automatic registry fallback");
    fake::mapping.clear(); fake::registry.clear(); g_hwInfoSharedMemoryCache = {};
}
void SensorIdentity() {
    g_hwInfoSharedMemoryCache = {};
    auto sensors = std::vector{Sensor(1, "CPU"), Sensor(2, "GPU")};
    auto package = Reading(0, 101, "CPU Package", 72);
    auto core = Reading(0, 102, "Core 3", 42);
    auto gpu = Reading(1, 201, "GPU Temperature", 55);
    SetMapping(sensors, {package, core, gpu});
    Check(SharedSnapshot().cpuTemp == 72, "initial shared-memory discovery");
    auto deadline = g_hwInfoSharedMemoryCache.nextFullScan;
    SetMapping(sensors, {core, package, gpu});
    Check(SharedSnapshot().cpuTemp == 72, "reordered valid readings must not substitute a core");
    Check(g_hwInfoSharedMemoryCache.cpuReadingIndex == 1,
          "identity mismatch must rescan in the same sample");
    Check(deadline > SampleTime::clock::now(), "test must exercise cache, not scheduled rescan");

    sensors[0].sensorInstance = 7;
    SetMapping(sensors, {core, package, gpu});
    Check(SharedSnapshot().cpuTemp == 72 &&
          g_hwInfoSharedMemoryCache.cpuIdentity->sensorInstance == 7,
          "sensor-instance changes must refresh identity");
    fake::mutexTimeout = true;
    Check(!SharedSnapshot().cpuTemp, "timeout must not display a stale temperature as live");
    fake::mutexTimeout = false;
    auto* header = reinterpret_cast<HwInfoHeader*>(fake::mapping.data() + fake::mappingOffset);
    header->readingOffset = static_cast<uint32_t>(fake::mapping.size() - 8);
    Check(!SharedSnapshot().cpuTemp && g_hwInfoLayoutRejectedLogged,
          "mapped-size bounds failure must reject layout and latch diagnostic");
    SetMapping(sensors, {core, package, gpu});
    Check(SharedSnapshot().cpuTemp == 72 && !g_hwInfoLayoutRejectedLogged,
          "a valid layout must recover and reset the diagnostic");
    unsigned fast = 0;
    for (int i = 0; i < 4; ++i) {
        Check(HwInfoRescanDelay(false, fast, std::chrono::seconds(60)) ==
                  std::chrono::seconds(5), "partial discovery window");
    }
    Check(HwInfoRescanDelay(false, fast, std::chrono::seconds(60)) ==
              std::chrono::seconds(60), "partial rescans must back off");
    HwInfoRescanDelay(true, fast, std::chrono::seconds(60));
    Check(fast == 0, "completed discovery must reset the short retry window");

    auto populateRegistry = [](bool swapped) {
        fake::TouchRegistry();
        fake::registry.clear();
        for (int i = 0; i < 3; ++i) {
            int record = i < 2 && swapped ? 1 - i : i;
            auto suffix = std::to_wstring(i);
            fake::registry[L"Sensor" + suffix] = record == 2 ? L"GPU" : L"CPU";
            fake::registry[L"Label" + suffix] =
                record == 2 ? L"GPU Temperature" : record ? L"Core 3" : L"CPU Package";
            fake::registry[L"ValueRaw" + suffix] = record == 2 ? L"55" : record ? L"42" : L"72";
            fake::registry[L"Value" + suffix] = L"72 \u00B0C";
        }
    };
    auto registrySnapshot = [] {
        MetricsSnapshot snapshot;
        HwInfoTemperatureDiagnostics diagnostics;
        ReadHwInfoGadgetRegistry(snapshot, ModSettings{}, std::nullopt, diagnostics);
        return snapshot;
    };
    g_hwInfoGadgetRegistryCache = {};
    populateRegistry(false);
    Check(registrySnapshot().cpuTemp == 72, "registry discovery");
    populateRegistry(true);
    Check(registrySnapshot().cpuTemp == 72 && g_hwInfoGadgetRegistryCache.cpuIndex == 1,
          "registry Sensor/Label reorder must rescan immediately");
    for (const auto& prefix : {L"Sensor", L"Label", L"ValueRaw", L"Value"}) {
        fake::registry[std::wstring(prefix) + L"7001"] = fake::registry[std::wstring(prefix) + L"1"];
        fake::registry.erase(std::wstring(prefix) + L"1");
    }
    Check(registrySnapshot().cpuTemp == 72 && g_hwInfoGadgetRegistryCache.cpuIndex == 7001,
          "sparse registry indices beyond the old scan cap must be discovered");
    fake::mapping.clear();
    fake::registry.clear();
}

void SharedMemoryCopyBudget() {
    auto sensors = std::vector<HwInfoSensorPrefix>(1000, Sensor(1, "CPU"));
    auto readings = std::vector<HwInfoReadingPrefix>(1000, Reading(0, 101, "CPU Package", 72));
    SetMapping(sensors, readings);
    auto* header = reinterpret_cast<HwInfoHeader*>(fake::mapping.data() + fake::mappingOffset);
    auto currentPoll = header->pollTime;
    sensorTableAllocation = sensors.size() * sizeof(sensors[0]);
    readingTableAllocation = readings.size() * sizeof(readings[0]);
    g_hwInfoSharedMemoryCache = {}; header->pollTime = 1;
    tableAllocations = 0; observeTableAllocations = true;
    for (int i = 0; i < 3; ++i) Check(!SharedSnapshot().cpuTemp, "old publication supplies no temperature");
    observeTableAllocations = false;
    Check(tableAllocations == 0 && g_hwInfoSharedMemoryCache.nextFullScan == SampleTime{},
          "old header is refused before table allocation and leaves discovery due for recovery");
    header->pollTime = currentPoll;
    observeTableAllocations = true;
    for (int i = 0; i < 3; ++i) Check(SharedSnapshot().cpuTemp == 72, "fresh publication recovers immediately");
    observeTableAllocations = false;
    Check(tableAllocations == 2 && g_hwInfoSharedMemoryCache.nextFullScan > SampleTime::clock::now(),
          "three fresh reads allocate one table pair and keep the scan deadline");
    g_hwInfoSharedMemoryCache.nextFullScan = {};
    g_hwInfoSharedMemoryCache.lastPollChange = SampleTime::clock::now() - std::chrono::seconds(9);
    tableAllocations = 0; observeTableAllocations = true;
    for (int i = 0; i < 3; ++i) Check(!SharedSnapshot().cpuTemp, "frozen publication stays unavailable");
    observeTableAllocations = false;
    Check(tableAllocations == 0, "monotonic expiry also precedes a due full-table scan");
    // Change the header during allocation to exercise the real double-header
    // check when the publisher's mutex cannot be opened.
    fake::mutexAvailable = false; g_hwInfoSharedMemoryCache = {};
    mutateTablePublication = [] {
        auto* live = reinterpret_cast<HwInfoHeader*>(fake::mapping.data() + fake::mappingOffset);
        ++live->pollTime; mutateTablePublication = nullptr;
    };
    observeTableAllocations = true;
    auto raced = SharedSnapshot();
    observeTableAllocations = false;
    Check(!raced.cpuTemp, "a changing header cannot publish a mixed table without the mutex");
    Check(SharedSnapshot().cpuTemp == 72, "a stable mutex-free publication recovers on the next read");
    header->pollTime = 1; tableAllocations = 0; observeTableAllocations = true;
    Check(!SharedSnapshot().cpuTemp, "mutex-free old publication remains unavailable");
    observeTableAllocations = false;
    Check(tableAllocations == 0, "mutex-free old publication also avoids table allocation");
    fake::mutexAvailable = true; mutateTablePublication = nullptr;
    fake::mapping.clear(); g_hwInfoSharedMemoryCache = {};
}

GpuAdapterInfo TestAdapter() {
    return {L"Test GPU", L"0x00000000_0x00000042", {0x42, 0},
            4ull * 1024 * 1024 * 1024, 8ull * 1024 * 1024 * 1024, false};
}
void CacheAdapter() {
    g_cachedGpuAdapterInfo = TestAdapter();
    g_cachedGpuAdapterFilter = L"";
    g_cachedGpuAdapterResolved = true;
    g_nextGpuAdapterResolve = SampleTime::clock::now() + std::chrono::hours(1);
}
void ResetPdh() {
    CloseMetricSources();
    fake::queries.clear();
    fake::counters.clear();
    fake::stale = nullptr;
    fake::freshMemory = true;
    fake::memoryAvailable = true;
    fake::enginesAvailable = true;
    fake::hardArrays = false;
    fake::invalidEngine = false;
    fake::invalidEngineQuery = nullptr;
    fake::growArrayAttempts = 0;
    fake::opens = 0;
    CacheAdapter();
}
uint64_t WallFileTime() {
    FILETIME now{}; GetSystemTimeAsFileTime(&now); return FileTimeValue(now);
}

void RegistryFreshness() {
    using namespace std::chrono;
    constexpr uint64_t second = 10000000;
    auto wall = WallFileTime(); auto now = SampleTime{} + hours(1);
    HwInfoGadgetRegistryCache cache;
    Check(HwInfoRegistryPublicationIsFresh(wall, wall, now, cache), "new Registry publication is fresh");
    Check(HwInfoRegistryPublicationIsFresh(wall, wall + 60 * second, now + seconds(60), cache) &&
          !HwInfoRegistryPublicationIsFresh(wall, wall + 61 * second, now + seconds(61), cache),
          "Registry publication expires at its bounded wall-clock age");
    Check(!HwInfoRegistryPublicationIsFresh(wall, wall, now + seconds(61), cache),
          "Registry monotonic expiry survives wall-clock rollback");
    Check(!HwInfoRegistryPublicationIsFresh(0, wall, now, cache) &&
          !HwInfoRegistryPublicationIsFresh(wall + 3 * second, wall, now, cache),
          "unset and future Registry timestamps cannot prove publication");
    Check(HwInfoRegistryPublicationIsFresh(wall + second, wall + second, now + seconds(62), cache),
          "a new Registry write restores freshness after a stall");

    ResetPdh(); ModSettings settings; EnsureCpuPdhQuery(settings);
    SetMapping({Sensor(1, "CPU")}, {Reading(0, 101, "CPU Package", 72)});
    auto* header = reinterpret_cast<HwInfoHeader*>(fake::mapping.data() + fake::mappingOffset);
    header->pollTime = 1;
    fake::registry = {{L"Sensor0", L"CPU"}, {L"Label0", L"CPU Package"},
                      {L"ValueRaw0", L"72"}, {L"Value0", L"72 \u00B0C"},
                      {L"Sensor1", L"Test GPU"}, {L"Label1", L"GPU Temperature"},
                      {L"ValueRaw1", L"55"}, {L"Value1", L"55 \u00B0C"}};
    fake::registryWriteTime = WallFileTime() - 61 * second;
    auto readsBefore = fake::registryValueReads;
    for (int i = 0; i < 3; ++i) {
        auto snapshot = CollectMetrics(settings);
        Check(snapshot.cpuTemp == 50 && snapshot.cpuTempProvider == TemperatureProvider::WindowsThermalZones &&
              !snapshot.gpuTemp && MetricsSnapshotIsFresh(snapshot, settings, SampleTime::clock::now()),
              "Automatic uses live Windows CPU data instead of republishing old Registry temperatures");
    }
    Check(fake::registryValueReads == readsBefore, "expired Registry is refused before any sensor value reads");
    for (auto source : {TemperatureSource::HwInfoAuto, TemperatureSource::GadgetRegistry}) {
        settings.temperatureSource = source;
        MetricsSnapshot snapshot; ReadTemperatures(snapshot, settings);
        Check(!snapshot.cpuTemp && !snapshot.gpuTemp, "explicit HWiNFO modes never return old Registry values");
    }
    settings.temperatureSource = TemperatureSource::Auto;
    fake::TouchRegistry();
    MetricsSnapshot fresh; ReadTemperatures(fresh, settings);
    Check(fresh.cpuTemp == 72 && fresh.gpuTemp == 55 &&
          fresh.cpuTempProvider == TemperatureProvider::HwInfoGadgetRegistry,
          "fresh Registry still works when Shared Memory has stopped publishing");
    g_hwInfoGadgetRegistryCache.lastWriteChange = SampleTime::clock::now() - seconds(61);
    MetricsSnapshot frozen; ReadTemperatures(frozen, settings);
    Check(frozen.cpuTemp == 50 && !frozen.gpuTemp, "a frozen cached Registry publication releases native fallback");
    fake::TouchRegistry();
    MetricsSnapshot resumed; ReadTemperatures(resumed, settings);
    Check(resumed.cpuTemp == 72 && resumed.gpuTemp == 55, "unchanged temperatures recover when the write timestamp advances");
    fake::registryInfoFails = true;
    MetricsSnapshot denied; ReadTemperatures(denied, settings);
    Check(denied.cpuTemp == 50 && !denied.gpuTemp, "unknown Registry write age releases native fallback");
    fake::registryInfoFails = false;
    fake::registryChangesDuringRead = true;
    MetricsSnapshot supplied; supplied.cpuTemp = 60; supplied.cpuTempProvider = TemperatureProvider::HwInfoSharedMemory;
    HwInfoTemperatureDiagnostics diagnostics;
    ReadHwInfoGadgetRegistry(supplied, settings, std::wstring(L"Test GPU"), diagnostics);
    Check(supplied.cpuTemp == 60 && supplied.cpuTempProvider == TemperatureProvider::HwInfoSharedMemory && !supplied.gpuTemp,
          "concurrent Registry writes discard partial results and preserve supplied Shared Memory data");
    fake::registryChangesDuringRead = false;
    ReadHwInfoGadgetRegistry(supplied, settings, std::wstring(L"Test GPU"), diagnostics);
    Check(supplied.cpuTemp == 60 && supplied.gpuTemp == 55, "stable Registry retry restores only the missing temperature");
    fake::registryWriteTime = 0;
    MetricsSnapshot zero; ReadTemperatures(zero, settings);
    Check(zero.cpuTemp == 50, "reader refuses a zero Registry timestamp");
    fake::registryWriteTime = WallFileTime() + 3 * second;
    MetricsSnapshot future; ReadTemperatures(future, settings);
    Check(future.cpuTemp == 50, "reader refuses a future Registry timestamp");
    fake::registryWriteTime = 0; fake::TouchRegistry(); fake::registry.clear(); fake::mapping.clear();
    ResetPdh();
}

void NativeRegistryPublication() {
    struct Fixture {
        std::wstring path = L"Software\\TaskbarSystemInfoRegression-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        HKEY key = nullptr;
        bool created = false;
        ~Fixture() {
            fake::nativeRegistrySource = nullptr;
            if (key) RegCloseKey(key);
            if (created) RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str());
        }
    } fixture;
    DWORD disposition = 0;
    Check(RegCreateKeyExW(HKEY_CURRENT_USER, fixture.path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
          KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &fixture.key, &disposition) == ERROR_SUCCESS,
          "create an isolated disposable Registry fixture");
    fixture.created = disposition == REG_CREATED_NEW_KEY;
    Check(fixture.created, "never reuse or delete an existing Registry key");
    auto write = [&](PCWSTR name, const std::wstring& value) {
        Check(RegSetValueExW(fixture.key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
              static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS, "publish to the native fixture");
    };
    write(L"Sensor0", L"CPU"); write(L"Label0", L"CPU Package");
    write(L"ValueRaw0", L"72"); write(L"Value0", L"72 \u00B0C");
    fake::nativeRegistrySource = fixture.key; g_hwInfoGadgetRegistryCache = {};
    auto before = ReadRegistryWriteTime(fixture.key);
    MetricsSnapshot first; HwInfoTemperatureDiagnostics diagnostics;
    ReadHwInfoGadgetRegistry(first, ModSettings{}, std::nullopt, diagnostics);
    Check(before && *before && first.cpuTemp == 72, "production reader accepts real Windows Registry metadata and values");
    g_hwInfoGadgetRegistryCache.lastWriteChange = SampleTime::clock::now() - std::chrono::seconds(61);
    Sleep(30); write(L"ValueRaw0", L"72");
    auto after = ReadRegistryWriteTime(fixture.key);
    MetricsSnapshot rewritten;
    ReadHwInfoGadgetRegistry(rewritten, ModSettings{}, std::nullopt, diagnostics);
    Check(after && (after == before ? !rewritten.cpuTemp : rewritten.cpuTemp == 72),
          "native identical-value writes revive readings only if Windows advances the key timestamp");
    write(L"Color0", L"FFFFFF");
    after = ReadRegistryWriteTime(fixture.key);
    ReadHwInfoGadgetRegistry(rewritten, ModSettings{}, std::nullopt, diagnostics);
    Check(after && *after > *before && rewritten.cpuTemp == 72,
          "a real key change restores freshness without requiring the selected temperature to change");
    fake::nativeRegistrySource = nullptr;
    RegCloseKey(fixture.key); fixture.key = nullptr;
    Check(RegDeleteKeyW(HKEY_CURRENT_USER, fixture.path.c_str()) == ERROR_SUCCESS, "remove the native fixture after verification");
    fixture.created = false; g_hwInfoGadgetRegistryCache = {};
}

MetricsSnapshot PdhSnapshot() {
    MetricsSnapshot snapshot;
    snapshot.cpu = 10; snapshot.cpuAvailable = true;
    snapshot.ram = 50; snapshot.ramAvailable = true;
    ReadPdhMetrics(snapshot, ModSettings{});
    return snapshot;
}
void PdhRecovery() {
    ResetPdh();
    auto first = PdhSnapshot();
    Check(first.cpuAvailable && first.ramAvailable && !first.gpuAvailable,
          "GPU priming must retain CPU/RAM without publishing a bogus GPU rate");
    auto* cpu = reinterpret_cast<fake::Query*>(g_cpuPdhQuery);
    auto* gpu = reinterpret_cast<fake::Query*>(g_pdhQuery);
    Check(cpu != gpu, "CPU and GPU queries must be independent");
    auto normal = PdhSnapshot();
    Check(normal.gpuAvailable && normal.vramAvailable, "healthy production PDH path");
    fake::invalidEngine = true;
    Check(!PdhSnapshot().gpuAvailable,
          "an invalid engine sample must not be disguised as idle");
    fake::invalidEngine = false;
    fake::stale = gpu;
    auto missing = PdhSnapshot();
    Check(!missing.gpuAvailable && !missing.vramAvailable, "empty stale arrays must not fake idle");
    Check(gpu->closed && !cpu->closed && !g_pdhQuery,
          "fresh memory probe must recover stale GPU without resetting CPU");
    MetricsSnapshot thermal;
    ReadWindowsThermalZones(thermal, ModSettings{});
    Check(thermal.cpuTemp && Near(*thermal.cpuTemp, 50),
          "CPU temperature must remain available during GPU recovery");
    g_nextPdhCounterRetry = {};
    CacheAdapter();
    Check(!PdhSnapshot().gpuAvailable, "rebuilt GPU query must prime before publishing");
    Check(PdhSnapshot().vramAvailable, "VRAM must recover on the next sample");

    ResetPdh(); PdhSnapshot(); PdhSnapshot();
    cpu = reinterpret_cast<fake::Query*>(g_cpuPdhQuery);
    gpu = reinterpret_cast<fake::Query*>(g_pdhQuery);
    fake::invalidEngineQuery = gpu;
    int invalidOpens = fake::opens;
    auto invalidSamplesStarted = SampleTime::clock::now();
    for (int i = 0; i < 3; ++i) {
        auto invalid = PdhSnapshot();
        Check(!invalid.gpuAvailable && invalid.vramAvailable && invalid.cpuAvailable,
              "invalid engine CStatus retains working VRAM/CPU without fake idle");
    }
    Check(fake::opens == invalidOpens + 1 && !gpu->closed,
          "repeated invalid engine samples prime one fresh rate-counter probe before resetting");
    auto* probe = reinterpret_cast<fake::Query*>(g_gpuEngineRecoveryProbe.query);
    Check(probe && probe->collects == 1 &&
          g_gpuEngineRecoveryProbe.nextSample >= invalidSamplesStarted + std::chrono::seconds(1),
          "fresh engine probe collects its baseline and schedules the rate sample at least one interval later");
    // Keep the pre-deadline assertions deterministic even if the host pauses.
    g_gpuEngineRecoveryProbe.nextSample = SampleTime::clock::now() + std::chrono::hours(1);
    for (int i = 0; i < 50; ++i) PdhSnapshot();
    Check(probe->collects == 1 && !gpu->closed && fake::opens == invalidOpens + 1,
          "an engine probe waits for the next sample interval without synchronous double collection");
    g_gpuEngineRecoveryProbe.nextSample = {};
    auto recovering = PdhSnapshot();
    Check(probe->collects == 2 && probe->closed && gpu->closed && !cpu->closed && !g_pdhQuery &&
          recovering.cpuAvailable && recovering.vramAvailable && !recovering.gpuAvailable,
          "a working fresh engine query recovers invalid CStatus without resetting CPU or faking a rate");
    g_nextPdhCounterRetry = {}; CacheAdapter();
    Check(!PdhSnapshot().gpuAvailable, "engine recovery re-primes the rebuilt query");
    auto recovered = PdhSnapshot();
    Check(recovered.gpuAvailable && recovered.vramAvailable && Near(recovered.gpu, 14),
          "GPU usage returns on the next valid rebuilt sample");

    ResetPdh(); PdhSnapshot(); PdhSnapshot();
    gpu = reinterpret_cast<fake::Query*>(g_pdhQuery);
    fake::invalidEngine = true; invalidOpens = fake::opens;
    PdhSnapshot(); PdhSnapshot();
    fake::invalidEngine = false; PdhSnapshot();
    Check(fake::opens == invalidOpens && g_consecutiveInvalidGpuSamples == 0,
          "a valid engine value breaks an isolated invalid-sample streak");
    fake::invalidEngine = true;
    PdhSnapshot(); PdhSnapshot(); PdhSnapshot();
    probe = reinterpret_cast<fake::Query*>(g_gpuEngineRecoveryProbe.query);
    g_gpuEngineRecoveryProbe.nextSample = {};
    auto stillInvalid = PdhSnapshot();
    Check(probe->closed && !gpu->closed && !g_gpuEngineRecoveryProbe.query &&
          !stillInvalid.gpuAvailable && stillInvalid.vramAvailable,
          "an invalid fresh engine query cannot justify a reset or false idle");
    for (int i = 0; i < 50; ++i) PdhSnapshot();
    Check(fake::opens == invalidOpens + 1 && !gpu->closed,
          "persistent invalid CStatus throttles fresh probes and never causes a reset storm");
    g_nextPdhRecovery = {}; PdhSnapshot();
    probe = reinterpret_cast<fake::Query*>(g_gpuEngineRecoveryProbe.query);
    fake::invalidEngine = false; PdhSnapshot();
    Check(probe->closed && !g_gpuEngineRecoveryProbe.query && !gpu->closed,
          "a healthy old query cancels an in-progress engine probe");
    fake::invalidEngine = true; g_nextPdhRecovery = {};
    PdhSnapshot(); PdhSnapshot(); PdhSnapshot();
    probe = reinterpret_cast<fake::Query*>(g_gpuEngineRecoveryProbe.query);
    CloseMetricSources();
    Check(probe->closed && gpu->closed && !g_gpuEngineRecoveryProbe.query,
          "teardown closes a pending engine probe and resets its state");

    ResetPdh();
    PdhSnapshot();
    gpu = reinterpret_cast<fake::Query*>(g_pdhQuery);
    fake::stale = gpu;
    fake::freshMemory = false;
    fake::hardArrays = true;
    PdhSnapshot(); PdhSnapshot();
    Check(g_consecutivePdhReadFailures == 2, "track consecutive hard errors");
    fake::hardArrays = false;
    int before = fake::opens;
    for (int i = 0; i < 50; ++i) PdhSnapshot();
    Check(!gpu->closed && fake::opens == before + 1,
          "a parked GPU must keep its query and throttle fresh probes");
    Check(g_consecutivePdhReadFailures == 0,
          "soft absence must break the consecutive-hard-error streak");
    fake::stale = nullptr;
    fake::enginesAvailable = false;
    auto idle = PdhSnapshot();
    Check(idle.gpuAvailable && idle.gpu == 0 && idle.vramAvailable,
          "healthy VRAM plus no engine instances may report idle");
    g_gpuAdapterIdentityChanged = true;
    PdhSnapshot();
    Check(gpu->closed, "confirmed LUID change must rebuild immediately");

    ResetPdh();
    PdhSnapshot();
    gpu = reinterpret_cast<fake::Query*>(g_pdhQuery);
    gpu->fail = true;
    PdhSnapshot(); PdhSnapshot();
    Check(!gpu->closed, "isolated hard errors must not churn queries");
    PdhSnapshot();
    Check(gpu->closed, "three hard errors must recover GPU");
    ResetPdh();
    PdhSnapshot();
    fake::growArrayAttempts = 2;
    PDH_STATUS status{};
    auto bytes = ReadVramUsedBytes(g_vramCounter, TestAdapter(), status);
    Check(bytes.has_value() && status == ERROR_SUCCESS,
          "growing wildcard arrays must retry safely");
    fake::growArrayAttempts = 20;
    ReadVramUsedBytes(g_vramCounter, TestAdapter(), status);
    Check(status == static_cast<PDH_STATUS>(PDH_MORE_DATA) &&
          !IsHardPdhArrayFailure(status),
          "buffer churn beyond the retry bound must not reset the query");
    CloseMetricSources();
}

int kmtOpens = 0;
int kmtCloses = 0;
int kmtQueries = 0;
int kmtEnumerations = 0;
LONG kmtQueryStatus = 0;
LONG kmtOpenStatus = 0;
bool kmtEmptyHandle = false;
ULONG kmtTemperature = 535;
GpuAdapterInfo kmtAdapter;
LONG WINAPI TestKmtEnumerate(D3DKMT_ENUMADAPTERS2* request) {
    ++kmtEnumerations;
    request->NumAdapters = 1;
    request->pAdapters[0] = {99, kmtAdapter.luidValue, 1, FALSE};
    return 0;
}
LONG WINAPI TestKmtOpen(D3DKMT_OPENADAPTERFROMLUID* request) {
    ++kmtOpens;
    request->hAdapter = kmtOpenStatus || kmtEmptyHandle
                           ? 0 : static_cast<D3DKMT_HANDLE>(kmtOpens);
    return kmtOpenStatus;
}
LONG WINAPI TestKmtClose(const D3DKMT_CLOSEADAPTER*) {
    ++kmtCloses;
    return 0;
}
LONG WINAPI TestKmtQuery(D3DKMT_QUERYADAPTERINFO* request) {
    if (request->Type == kAdapterRegistryInfoQueryType) {
        auto* info = static_cast<D3DKMT_ADAPTERREGISTRYINFO*>(request->pPrivateDriverData);
        std::wcscpy(info->AdapterString, L"Test GPU");
        return 0;
    }
    if (request->Type == kAdapterSegmentSizeQueryType) {
        auto* info = static_cast<D3DKMT_SEGMENTSIZEINFO*>(request->pPrivateDriverData);
        info->DedicatedVideoMemorySize = kmtAdapter.dedicatedVideoMemory;
        info->SharedSystemMemorySize = kmtAdapter.sharedSystemMemory;
        return 0;
    }
    if (request->Type == kAdapterTypeQueryType) {
        *static_cast<D3DKMT_ADAPTERTYPE*>(request->pPrivateDriverData) = {};
        return 0;
    }
    ++kmtQueries;
    if (kmtQueryStatus) return kmtQueryStatus;
    auto* data = static_cast<D3DKMT_ADAPTER_PERFDATA*>(request->pPrivateDriverData);
    data->Temperature = kmtTemperature;
    return 0;
}
void ResetNativeTemperature() {
    ResetPdh();
    kmtOpens = kmtCloses = kmtQueries = kmtEnumerations = 0;
    kmtQueryStatus = kmtOpenStatus = 0;
    kmtEmptyHandle = false;
    kmtTemperature = 535;
    kmtAdapter = TestAdapter();
    g_d3dkmtEnumAdapters2 = TestKmtEnumerate;
    g_d3dkmtOpenAdapterFromLuid = TestKmtOpen;
    g_d3dkmtCloseAdapter = TestKmtClose;
    g_d3dkmtQueryAdapterInfo = TestKmtQuery;
}
MetricsSnapshot NativeTemperatureSnapshot() {
    MetricsSnapshot snapshot;
    ReadWindowsGpuTemperature(snapshot, ModSettings{});
    return snapshot;
}
void CheckTemperatureRetryDelay(int seconds) {
    auto remaining = g_gpuTemperatureRetry.nextAttempt - SampleTime::clock::now();
    Check(remaining > std::chrono::seconds(seconds - 1) &&
              remaining <= std::chrono::seconds(seconds),
          "native temperature retry deadline must use the bounded delay");
}
void NativeTemperatureRecovery() {
    ResetNativeTemperature();
    Check(NativeTemperatureSnapshot().gpuTemp == 53.5,
          "native GPU temperature must convert tenths of a degree");
    Check(NativeTemperatureSnapshot().gpuTemp == 53.5 && kmtOpens == 1,
          "native handle must be reused between samples");
    kmtQueryStatus = kStatusInvalidHandle;
    auto adapterDeadline = g_nextGpuAdapterResolve;
    Check(!NativeTemperatureSnapshot().gpuTemp && kmtCloses == 1 && !g_cachedD3dkmtAdapterHandle &&
              g_cachedGpuAdapterResolved && g_cachedGpuAdapterInfo,
          "a temperature failure must not invalidate the shared GPU adapter cache");
    Check(g_nextGpuAdapterResolve == adapterDeadline,
          "temperature failure must preserve normal adapter refresh scheduling");
    CheckTemperatureRetryDelay(5);
    kmtQueryStatus = 0;
    Check(!NativeTemperatureSnapshot().gpuTemp && kmtOpens == 1,
          "a failed native handle must not be reopened on every sample");
    g_gpuTemperatureRetry.nextAttempt = {};
    Check(NativeTemperatureSnapshot().gpuTemp == 53.5 && kmtOpens == 2,
          "native temperature must resume with a fresh handle");
    Check(g_gpuTemperatureRetry.failures == 0 &&
              g_gpuTemperatureRetry.nextAttempt == SampleTime{},
          "successful temperature recovery must clear the backoff");
    CloseMetricSources();
    Check(kmtCloses == 2, "shutdown must close the recovered native handle");
    Check(!g_gpuTemperatureRetry.adapterLuid,
          "provider shutdown must clear native temperature retry state");

    for (LONG unsupported : {kStatusNotImplemented, kStatusNotSupported}) {
        ResetNativeTemperature();
        kmtQueryStatus = unsupported;
        PdhSnapshot();
        auto healthy = PdhSnapshot();
        auto* gpuQuery = g_pdhQuery;
        int queryOpens = fake::opens;
        adapterDeadline = g_nextGpuAdapterResolve;
        ReadTemperatures(healthy, ModSettings{}); // Default Automatic fallback.
        Check(!healthy.gpuTemp && kmtQueries == 1 && kmtOpens == 1,
              "Automatic must try the native fallback once when HWiNFO is absent");
        CheckTemperatureRetryDelay(60);
        bool otherMetricsStayedAvailable = true;
        for (int i = 0; i < 80; ++i) {
            auto snapshot = PdhSnapshot();
            ReadTemperatures(snapshot, ModSettings{});
            otherMetricsStayedAvailable &= snapshot.cpuAvailable && snapshot.ramAvailable &&
                snapshot.gpuAvailable && snapshot.vramAvailable && !snapshot.gpuTemp;
        }
        Check(otherMetricsStayedAvailable,
              "persistent temperature refusal must preserve CPU/RAM/GPU/VRAM");
        Check(fake::opens == queryOpens && g_pdhQuery == gpuQuery,
              "persistent temperature refusal must preserve the independent PDH queries");
        Check(kmtQueries == 1 && kmtOpens == 1 && kmtEnumerations == 0 &&
                  g_nextGpuAdapterResolve == adapterDeadline,
              "unsupported temperature must not churn handles or enumerate adapters every sample");
        g_nextGpuAdapterResolve = {};
        Check(GetGpuAdapterInfo(L"").has_value() && kmtEnumerations == 1,
              "the independent periodic adapter refresh must still run");
        Check(!NativeTemperatureSnapshot().gpuTemp && kmtQueries == 1,
              "same-LUID cache refresh must not reset temperature unavailability");
        kmtQueryStatus = 0;
        g_gpuTemperatureRetry.nextAttempt = {};
        Check(NativeTemperatureSnapshot().gpuTemp == 53.5 && kmtQueries == 2,
              "periodic reprobe must recover when a new driver preserves the LUID");

        kmtQueryStatus = unsupported;
        NativeTemperatureSnapshot();
        ++kmtAdapter.luidValue.LowPart;
        kmtAdapter.luid = FormatAdapterLuid(kmtAdapter.luidValue);
        g_nextGpuAdapterResolve = {};
        kmtQueryStatus = 0;
        Check(NativeTemperatureSnapshot().gpuTemp == 53.5 &&
                  SameLuid(*g_gpuTemperatureRetry.adapterLuid, kmtAdapter.luidValue),
              "a newly resolved LUID must bypass the old adapter's cooldown");
    }

    ResetNativeTemperature();
    kmtQueryStatus = static_cast<LONG>(0xC000000Du); // Ambiguous INVALID_PARAMETER.
    for (int delay : {5, 10, 20, 40, 60, 60}) {
        g_gpuTemperatureRetry.nextAttempt = {};
        Check(!NativeTemperatureSnapshot().gpuTemp && g_cachedGpuAdapterResolved,
              "unknown persistent failures must not invalidate adapter identity");
        CheckTemperatureRetryDelay(delay);
    }
    kmtQueryStatus = 0;
    g_gpuTemperatureRetry.nextAttempt = {};
    Check(NativeTemperatureSnapshot().gpuTemp == 53.5,
          "unknown errors must never permanently disable native temperature");

    ResetNativeTemperature();
    kmtOpenStatus = kStatusInvalidHandle;
    Check(!NativeTemperatureSnapshot().gpuTemp && g_cachedGpuAdapterResolved &&
              kmtQueries == 0 && kmtCloses == 0,
          "failed open must not invalidate adapter identity or query an invalid handle");
    CheckTemperatureRetryDelay(5);
    kmtOpenStatus = 0;
    Check(!NativeTemperatureSnapshot().gpuTemp && kmtOpens == 1,
          "open failures must honor the retry interval");
    g_gpuTemperatureRetry.nextAttempt = {};
    Check(NativeTemperatureSnapshot().gpuTemp == 53.5,
          "native open must recover with the same cached adapter identity");

    ResetNativeTemperature();
    kmtEmptyHandle = true;
    Check(!NativeTemperatureSnapshot().gpuTemp && kmtQueries == 0 &&
              g_cachedGpuAdapterResolved,
          "a successful open returning no handle must be treated as transient");
    CheckTemperatureRetryDelay(5);

    for (ULONG invalidTemperature : {0ul, 2001ul}) {
        ResetNativeTemperature();
        kmtTemperature = invalidTemperature;
        Check(!NativeTemperatureSnapshot().gpuTemp, "invalid native temperatures must stay unavailable");
        CheckTemperatureRetryDelay(60);
        Check(!NativeTemperatureSnapshot().gpuTemp && kmtQueries == 1,
              "invalid readings must not be polled on every sample");
    }
    CloseMetricSources();
    g_d3dkmtEnumAdapters2 = nullptr;
    g_d3dkmtOpenAdapterFromLuid = nullptr;
    g_d3dkmtCloseAdapter = nullptr;
    g_d3dkmtQueryAdapterInfo = nullptr;
}

void PlacementClassOwnership() {
    for (auto name : {kMoveWindowClass, kPlacementWindowClass}) {
        bool registered = false;
        WNDCLASSW cls{};
        cls.hInstance = PlacementModule();
        cls.lpfnWndProc = DefWindowProcW;
        cls.lpszClassName = name;
        Check(RegisterClassW(&cls) != 0, "register a foreign class with the same name");
        Check(!EnsurePlacementClass(cls, registered) && !registered,
              "a fresh module never adopts a previously registered class");
        Check(UnregisterClassW(name, cls.hInstance), "remove the foreign test class");
        Check(EnsurePlacementClass(cls, registered) && registered,
              "the current module records successful registration");
        Check(EnsurePlacementClass(cls, registered), "reuse a class registered by this load");
        HWND window = CreateWindowExW(0, name, L"", 0, 0, 0, 0, 0,
                                     HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
        Check(window != nullptr, "create a hidden class ownership fixture");
        ReleasePlacementClass(name, registered);
        Check(registered, "failed unregistration retains ownership for a retry");
        Check(DestroyWindow(window), "destroy the class ownership fixture");
        ReleasePlacementClass(name, registered);
        Check(!registered, "successful unregistration clears ownership");
        Check(EnsurePlacementClass(cls, registered), "register again after teardown");
        ReleasePlacementClass(name, registered);
        Check(!registered, "repeated load/unload leaves no class behind");
    }
}

void MoveWindowLifecycle() {
    auto settings = std::make_shared<ModSettings>();
    Check(settings->moveHotkey.empty(), "moving is opt-in for new and upgraded installations");
    { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
    g_unloading = false;
    EnsurePlacementControl(*settings);
    Check(g_placementControlWindow && !g_hotkeyRegistered,
          "the default control does not register a global shortcut");
    auto epoch = g_moveEpoch.load();
    failNextAllocation = true;
    SendMessageW(g_placementControlWindow, WM_HOTKEY, kMoveHotkeyId, 0);
    bool allocationThrown = !failNextAllocation;
    failNextAllocation = false;
    Check(allocationThrown && g_moveEpoch.load() == epoch + 1 && !g_moveEditorWindow,
          "an allocation failure in WM_HOTKEY is contained and cancels safely");
    Check(!EnumerateDisplayMonitors().empty(),
          "native display enumeration recovers after a callback allocation failure");
    settings->moveHotkey = L"Ctrl+Alt+Shift+F24";
    { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
    g_unloading = false;
    EnsurePlacementControl(*settings);
    Check(g_placementControlWindow != nullptr, "create production placement control on the owning thread");
    Check(g_hotkeyRegistered, "register the test move combination");
    HWND control = g_placementControlWindow;
    Check(!RegisterHotKey(control, 991, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24) &&
          GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED, "a occupied global combination really conflicts");
    for (int i = 0; i < 10; ++i) QueueTaskbarPlacement();
    MSG message{}; int queued = 0;
    while (PeekMessageW(&message, control, kGeometryMessage, kGeometryMessage, PM_REMOVE)) {
        ++queued; DispatchMessageW(&message);
    }
    Check(queued == 1 && !g_geometryQueued, "layout notifications coalesce into one native message");
    settings->moveHotkey.clear(); EnsurePlacementControl(*settings);
    Check(!g_hotkeyRegistered && RegisterHotKey(control, 991, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24),
          "disabling the shortcut releases its registration");
    UnregisterHotKey(control, 991);
    Check(RegisterHotKey(control, 991, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24), "occupy a key before mod registration");
    settings->moveHotkey = L"Ctrl+Alt+Shift+F24"; EnsurePlacementControl(*settings);
    Check(!g_hotkeyRegistered, "mod leaves an occupied key unregistered");
    UnregisterHotKey(control, 991);
    EnsurePlacementControl(*settings);
    Check(!g_hotkeyRegistered, "layout refresh does not repeatedly register a rejected key");
    g_hotkeyRefreshPending = true; EnsurePlacementControl(*settings);
    Check(g_hotkeyRegistered && !g_hotkeyRefreshPending, "settings reload retries a previously occupied unchanged key once");
    settings->moveHotkey.clear(); EnsurePlacementControl(*settings);
    WNDCLASSW parentClass{}; parentClass.hInstance = GetModuleHandleW(nullptr);
    parentClass.lpfnWndProc = DefWindowProcW; parentClass.lpszClassName = L"PrivateMoveTestHost";
    Check(RegisterClassW(&parentClass) != 0, "register an isolated hidden editor host");
    HWND parent = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                  0, 0, 500, 100, nullptr, nullptr, parentClass.hInstance, nullptr);
    WNDCLASSW cls{}; cls.hInstance = PlacementModule(); cls.lpfnWndProc = MoveEditorProc;
    cls.lpszClassName = kMoveWindowClass;
    Check(EnsurePlacementClass(cls, g_moveClassRegistered), "register the real preview callback");
    g_moveEditor = std::make_shared<MoveEditorState>();
    HWND preview = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, cls.lpszClassName, L"", WS_POPUP,
                                   0, 0, 410, 38, parent, nullptr, cls.hInstance, nullptr);
    Check(preview != nullptr, "create preview under a hidden parent without desktop interaction");
    g_moveEditor->window = preview; g_moveEditorWindow = preview;
    PlacementProfiles profiles{1, 10, L"test-display", {{L"test-display", .5}}};
    SetPlacementProfiles(profiles);
    SendMessageW(preview, WM_KEYDOWN, VK_RETURN, 0);
    Check(IsWindow(preview) && PlacementProfilesSnapshot().target == L"test-display",
          "Enter on an invalid candidate leaves saved state and editor intact");
    g_moveEditor->candidate = {0, 410, 0}; g_moveEditor->target.window = parent;
    SendMessageW(preview, WM_KEYDOWN, VK_RETURN, 0);
    Check(IsWindow(preview) && !g_moveEditor->candidate.width && PlacementProfilesSnapshot().target == L"test-display",
          "Enter revalidates a stale candidate and rejects a target without a taskbar root");
    // Exercise the real capture callback at both transparent side margins of
    // height-scaled content. No desktop cursor movement or Explorer hook is used.
    g_moveEditor->target.geometry = {1200, 33, {}, true};
    g_moveEditor->candidate = {350, 410, 0};
    for (int clickX : {2, 408}) {
        auto before = CurrentMovePreviewLayout(*g_moveEditor);
        Check(before.content.left > before.body.left && before.content.right < before.body.right,
              "height-scaled preview exposes centered side margins");
        SendMessageW(preview, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(clickX, 10));
        double fraction = g_moveEditor->grabFraction;
        Check(clickX == 2 ? fraction < 0 : fraction > 1,
              "native capture retains the point in a centered side margin");
        POINT pointer{before.bounds.left + clickX, before.bounds.top + 10};
        WidgetPointerAnchor anchor{ScreenPointToTaskbarX(g_moveEditor->target, pointer), fraction};
        auto moved = ResolveWidgetPlacement(*settings, g_moveEditor->target.geometry,
            {0, 350, std::nullopt, anchor}, g_moveEditor->visual.fontMetrics);
        auto draft = *g_moveEditor; draft.candidate = moved.placement;
        auto after = CurrentMovePreviewLayout(draft);
        auto heldPoint = [&](const MovePreviewLayout& shape) {
            double point = shape.content.left + fraction * (shape.content.right - shape.content.left);
            return shape.bounds.left + std::clamp(point, static_cast<double>(shape.body.left), static_cast<double>(shape.body.right));
        };
        Check(moved.placement.width > 0 && std::abs(heldPoint(after) - pointer.x) <= 1,
              "grabbing a centered margin does not move an unmoved preview");
        draft.target.geometry.height = 20;
        auto compact = ResolveWidgetPlacement(*settings, draft.target.geometry,
            {0, moved.placement.left, std::nullopt, anchor}, g_moveEditor->visual.fontMetrics);
        draft.candidate = compact.placement;
        Check(compact.placement.width > 0 && std::abs(heldPoint(CurrentMovePreviewLayout(draft)) - pointer.x) <= 1,
              "a held side margin snaps to the destination boundary when reflow removes it");
        ReleaseCapture();
    }
    g_moveEditor->target.geometry = {}; g_moveEditor->candidate = {};
    SendMessageW(preview, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(80, 10));
    Check(GetCapture() == preview && g_moveEditor->dragging, "drag captures the pointer in the real callback");
    Check(g_previewGraphicsToken != 0 && g_moveEditor->surface && g_moveEditor->surface->bitmap,
          "drag uses the real premultiplied-alpha preview renderer");
    std::weak_ptr<PreviewSurface> surfaceOwner = g_moveEditor->surface;
    SendMessageW(preview, WM_LBUTTONUP, 0, 0);
    Check(GetCapture() != preview && !g_moveEditor->dragging, "mouse release retains preview and releases capture");
    SendMessageW(preview, WM_KEYDOWN, VK_HOME, 0);
    Check(g_moveEditor && g_moveEditor->reset && PlacementProfilesSnapshot().target == L"test-display",
          "Home stages a reset without clearing persistence");
    SendMessageW(preview, WM_KEYDOWN, VK_ESCAPE, 0);
    Check(!IsWindow(preview) && !g_moveEditorWindow && !g_moveEditor &&
          PlacementProfilesSnapshot().target == L"test-display", "Esc closes the preview and cancels Home");
    Check(g_previewGraphicsToken == 0, "closing the preview releases its graphics runtime");
    Check(surfaceOwner.expired(), "controlled preview close destroys its GDI surface before module shutdown");
    g_moveEditor = std::make_shared<MoveEditorState>();
    preview = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, cls.lpszClassName, L"", WS_POPUP,
                             0, 0, 410, 38, parent, nullptr, cls.hInstance, nullptr);
    Check(preview != nullptr, "reopen the editor for a module-unload cancellation");
    g_moveEditor->window = preview; g_moveEditorWindow = preview;
    SendMessageW(preview, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(80, 10));
    surfaceOwner = g_moveEditor->surface;
    Check(!surfaceOwner.expired(), "unload fixture owns a real preview surface");
    g_unloading = true; CancelMoveEditor(); g_unloading = false;
    Check(!IsWindow(preview) && !g_moveEditor && !g_moveEditorWindow &&
          surfaceOwner.expired() && !g_previewGraphicsToken && GetCapture() != preview,
          "module-unload cancellation releases the window, capture, GDI surface and graphics runtime");
    Check(SavePlacementProfiles(profiles), "save a confirmed profile through the local storage API");
    SetPlacementProfiles({}); LoadPlacementProfiles();
    Check(PlacementProfilesSnapshot().target == L"test-display", "reload persisted target after a fresh in-memory state");
    fake::localSaveFails = true;
    Check(!SavePlacementProfiles({}) && ParseProfiles(fake::localStorage[L"placement.v1"])->target == L"test-display",
          "storage failure preserves the previously persisted target");
    fake::localSaveFails = false;
    RemovePlacementControl();
    Check(!IsWindow(control) && !g_placementControlWindow && !g_hotkeyRegistered && !g_geometryQueued,
          "teardown releases hidden control, registrations and queued state");
    Check(!g_moveClassRegistered && !g_placementClassRegistered,
          "teardown unregisters both classes owned by this load");
    DestroyWindow(parent); UnregisterClassW(parentClass.lpszClassName, parentClass.hInstance);
    SetPlacementProfiles({}); fake::localStorage.clear();
}

void WindowNotifications() {
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"TaskbarSystemInfoRegression";
    Check(RegisterClassW(&cls) != 0, "register hidden test window");
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"", WS_OVERLAPPED,
                                  0, 0, 100, 100, nullptr, nullptr, cls.hInstance, nullptr);
    Check(window != nullptr, "create hidden test window");
    g_unloading = false;
    g_notificationWindow = window;
    g_taskbarRefreshMessage = RegisterWindowMessageW(L"TaskbarSystemInfoRegressionRefresh");
    Check(SetWindowSubclass(window, TaskbarNotificationsProc, 1, 0), "attach production subclass");
    g_placementApplyPending = false;
    g_placementFailures = 7;
    auto epoch = g_moveEpoch.load();
    SendMessageW(window, WM_SETTINGCHANGE, SPI_SETMOUSE, 0);
    Check(g_moveEpoch.load() == epoch, "unrelated settings broadcasts do not cancel moving");
    SendMessageW(window, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
    Check(g_moveEpoch.load() == epoch + 1, "work-area changes invalidate the editor");
    SendMessageW(window, WM_DISPLAYCHANGE, 32, 0);
    Check(g_placementApplyPending && g_placementFailures == 0,
          "same-count display change must invalidate placement and retry backoff");
    Check(RemoveTaskbarNotifications() && !g_notificationWindow,
          "notification callback must be detached synchronously");
    g_placementApplyPending = false;
    SendMessageW(window, WM_DISPLAYCHANGE, 32, 0);
    Check(!g_placementApplyPending, "detached callback must never run again");
    MSG message;
    while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    Check(!g_placementApplyPending, "queued numeric refresh must be harmless after detach");
    DestroyWindow(window);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
}
} // namespace

int main() {
    try {
        PlacementClassOwnership();
        std::cout << "PASS: native window class collision refusal and ownership retries\n";
        HistoryAndScheduling();
        std::cout << "PASS: timestamped history, scheduling, formatting, layout bounds\n";
        AdaptivePlacement();
        std::cout << "PASS: adaptive placement, DPI matrix, reservation bounds and restoration\n";
        ResponsiveLayouts();
        std::cout << "PASS: responsive layouts, essential fields and destination preview matrix\n";
        PlacementConfirmationMatrix();
        std::cout << "PASS: 100000 generated placement/serialization/confirmation cases\n";
        DraggedLayouts();
        std::cout << "PASS: pointer anchoring across layouts, centered content and destination DPI\n";
        MovePreferences();
        std::cout << "PASS: move hotkeys, per-display profiles and corruption rejection\n";
        SensorFreshness();
        std::cout << "PASS: HWiNFO publication expiry, polling intervals, recovery and fallback\n";
        SensorIdentity();
        std::cout << "PASS: production HWiNFO mapping and registry cache reordering\n";
        SharedMemoryCopyBudget();
        std::cout << "PASS: stale Shared Memory skips table copies, scan scheduling and mutex-free race/recovery\n";
        RegistryFreshness();
        std::cout << "PASS: Registry wall/monotonic freshness, Automatic fallback, recovery and concurrent writer\n";
        NativeRegistryPublication();
        std::cout << "PASS: real Windows Registry timestamps, identical-value publication and disposable-key cleanup\n";
        PdhRecovery();
        std::cout << "PASS: production PDH stale/parked/LUID/error/priming paths\n";
        NativeTemperatureRecovery();
        std::cout << "PASS: native temperature refusal/backoff/open failure/same-LUID recovery\n";
        MoveWindowLifecycle();
        std::cout << "PASS: native move preview, capture, cancel/reset, unload, hotkey conflict, persistence and teardown\n";
        WindowNotifications();
        std::cout << "PASS: native hidden-window notification and detach lifecycle\n";
        std::cout << checks << " behavioral checks passed (synthetic providers, no Explorer injection).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
