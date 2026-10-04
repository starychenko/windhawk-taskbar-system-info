// Isolated XAML Island: renders the actual widget without injecting Explorer.
#include <windhawk_api.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <map>
#include <string>
// The isolated fixture saves only to process-local storage, never Windhawk.
namespace uiStorage {
std::map<std::wstring, std::wstring> values;
BOOL Set(PCWSTR key, PCWSTR value) { values[key] = value; return TRUE; }
}
#define Wh_SetStringValue uiStorage::Set
#include "../taskbar-system-info.wh.cpp"
#undef Wh_SetStringValue
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <iostream>
#include <random>
#include <stdexcept>

void PublishDocumentationSamples() {
    // Captured on 2026-10-04: Ryzen 9 5900X, 64 GB RAM, RX 7900 XTX
    // with 24 GB VRAM. Current readings match that snapshot; history is
    // illustrative. Boundary-value tests keep their own samples.
    g_cpuHistory.clear(); g_gpuHistory.clear();
    { std::lock_guard lock(g_metricsMutex); g_publishedMetrics.clear(); }
    auto now = SampleTime::clock::now();
    for (int i = 0; i <= 60; ++i) {
        MetricsSnapshot sample;
        sample.capturedAt = now - std::chrono::seconds(60 - i);
        sample.cpuAvailable = sample.gpuAvailable = sample.ramAvailable = sample.vramAvailable = true;
        sample.cpu = 4.41917 + 1.5 * (std::sin(i / 7.0) - std::sin(60 / 7.0));
        sample.gpu = .21605 + .2 * (std::sin(i / 5.0) - std::sin(60 / 5.0));
        sample.ram = 36.4027065; sample.ramUsedGb = 23.2658348; sample.ramTotalGb = 63.9123764;
        sample.vram = 9.61958; sample.vramUsedGb = 2.30318; sample.vramTotalGb = 23.9427;
        sample.cpuTemp = 47; sample.gpuTemp = 37;
        PublishMetrics(sample);
    }
    UpdateWidgetText(true);
}

void Pump() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
template <class Operation>
auto Finish(Operation operation) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (operation.Status() == AsyncStatus::Started) {
        Pump();
        if (std::chrono::steady_clock::now() > deadline) {
            throw std::runtime_error("XAML rendering timed out");
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    return operation.GetResults();
}
void SaveImage(FrameworkElement element, const std::wstring& folderPath, PCWSTR name) {
    using namespace Windows::Storage;
    using namespace Windows::Storage::Streams;
    using namespace Windows::Graphics::Imaging;
    Windows::UI::Xaml::Media::Imaging::RenderTargetBitmap bitmap;
    Finish(bitmap.RenderAsync(element));
    if (!bitmap.PixelWidth() || !bitmap.PixelHeight()) {
        throw std::runtime_error("XAML rendered an empty image");
    }
    auto buffer = Finish(bitmap.GetPixelsAsync());
    std::vector<uint8_t> pixels(buffer.Length());
    DataReader::FromBuffer(buffer).ReadBytes(pixels);
    auto folder = Finish(StorageFolder::GetFolderFromPathAsync(folderPath));
    auto file = Finish(folder.CreateFileAsync(name, CreationCollisionOption::ReplaceExisting));
    auto stream = Finish(file.OpenAsync(FileAccessMode::ReadWrite));
    auto encoder = Finish(BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream));
    encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied,
                         bitmap.PixelWidth(), bitmap.PixelHeight(), 96, 96, pixels);
    Finish(encoder.FlushAsync());
    stream.Close();
}
void SaveNativePreview(HWND window, const std::wstring& folderPath, PCWSTR name) {
    using namespace Windows::Storage;
    using namespace Windows::Storage::Streams;
    using namespace Windows::Graphics::Imaging;
    RECT rect; GetClientRect(window, &rect);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rect.right; info.bmiHeader.biHeight = -rect.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr; HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !bitmap || !pixels) throw std::runtime_error("Native preview bitmap allocation failed");
    HGDIOBJ old = SelectObject(dc, bitmap);
    HBRUSH backdrop = CreateSolidBrush(g_moveEditor->visual.light ? RGB(225, 233, 245) : RGB(17, 28, 50));
    FillRect(dc, &rect, backdrop); DeleteObject(backdrop);
    SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    std::vector<uint8_t> copy(static_cast<uint8_t*>(pixels),
                              static_cast<uint8_t*>(pixels) + rect.right * rect.bottom * 4);
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    auto folder = Finish(StorageFolder::GetFolderFromPathAsync(folderPath));
    auto file = Finish(folder.CreateFileAsync(name, CreationCollisionOption::ReplaceExisting));
    auto stream = Finish(file.OpenAsync(FileAccessMode::ReadWrite));
    auto encoder = Finish(BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream));
    encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore, rect.right, rect.bottom,
                         96, 96, copy);
    Finish(encoder.FlushAsync()); stream.Close();
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    init_apartment(apartment_type::single_threaded);
    Windows::UI::Xaml::Hosting::WindowsXamlManager manager{nullptr};
    Windows::UI::Xaml::Hosting::DesktopWindowXamlSource island{nullptr};
    HWND window = nullptr;
    int result = 0;
    try {
        manager = Windows::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"TaskbarSystemInfoVisualCheck";
        RegisterClassW(&cls);
        window = CreateWindowExW(0, cls.lpszClassName, L"", WS_POPUP,
                                  0, 0, 520, 64, nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) throw std::runtime_error("Creating test host failed");
        island = Windows::UI::Xaml::Hosting::DesktopWindowXamlSource();
        auto native = island.as<IDesktopWindowXamlSourceNative>();
        check_hresult(native->AttachToWindow(window));
        HWND child = nullptr;
        check_hresult(native->get_WindowHandle(&child));
        SetWindowPos(child, nullptr, 0, 0, 520, 64, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        Grid frame;
        Grid root;
        root.Name(L"RootGrid");
        frame.Children().Append(root);
        island.Content(frame);
        auto initialSettings = std::make_shared<ModSettings>();
        initialSettings->moveHotkey.clear();
        initialSettings->fontFamily = L"Segoe UI Variable Text";
        initialSettings->graphColor = kDefaultGraphColor;
        initialSettings->warningColor = kDefaultWarningColor;
        initialSettings->criticalColor = kDefaultCriticalColor;
        g_settings = initialSettings;
        // Satisfy StartMetricsWorker's already-running branch: no providers are
        // started in this fixture, and only synthetic snapshots are published.
        g_metricsWorker.emplace();
        if (!InjectWidget(frame)) throw std::runtime_error("InjectWidget failed");
        struct Scenario { PCWSTR name; int width; int height; int font; bool light; bool unavailable; };
        const Scenario cases[] = {
            {L"dark-410x48.png", 410, 48, 11, false, false},
            {L"light-410x48.png", 410, 48, 11, true, false},
            {L"short-410x30.png", 410, 30, 11, false, false},
            {L"narrow-330x48.png", 330, 48, 11, false, false},
            {L"large-font-430x48.png", 430, 48, 13, false, false},
            {L"unavailable-410x48.png", 410, 48, 11, false, true},
        };
        for (const auto& scenario : cases) {
            auto settings = std::make_shared<ModSettings>(*initialSettings);
            settings->width = scenario.width;
            settings->fontSize = scenario.font;
            settings->leftOffset = 0;
            {
                std::lock_guard lock(g_settingsMutex);
                g_settings = settings;
            }
            root.RequestedTheme(scenario.light ? ElementTheme::Light : ElementTheme::Dark);
            root.Background(SolidColorBrush(scenario.light ? Color{255, 242, 242, 242}
                                                          : Color{255, 32, 32, 32}));
            frame.Width(scenario.width);
            frame.Height(scenario.height);
            root.Width(scenario.width);
            root.Height(scenario.height);
            frame.Measure(Size{static_cast<float>(scenario.width), static_cast<float>(scenario.height)});
            frame.Arrange(Rect{0, 0, static_cast<float>(scenario.width), static_cast<float>(scenario.height)});
            frame.UpdateLayout();
            Pump();
            ApplyWidgetSettings();
            if (g_widget.Background())
                throw std::runtime_error("Normal widget acquired a background instead of staying transparent");
            {
                std::lock_guard lock(g_metricsMutex);
                g_publishedMetrics.clear();
            }
            g_cpuHistory.clear(); g_gpuHistory.clear();
            auto now = SampleTime::clock::now();
            for (int i = 0; i <= 60; ++i) {
                MetricsSnapshot sample;
                sample.capturedAt = now - std::chrono::seconds(60 - i);
                sample.cpuAvailable = !scenario.unavailable;
                sample.gpuAvailable = !scenario.unavailable && (i < 25 || i > 30);
                sample.ramAvailable = sample.vramAvailable = !scenario.unavailable;
                sample.cpu = i % 17; sample.gpu = i % 11;
                sample.ram = 52; sample.ramUsedGb = 16.7; sample.ramTotalGb = 32;
                sample.vram = 80; sample.vramUsedGb = 0.4; sample.vramTotalGb = 0.5;
                if (!scenario.unavailable) { sample.cpuTemp = 72; sample.gpuTemp = 56; }
                PublishMetrics(sample);
            }
            UpdateWidgetText(true);
            frame.UpdateLayout();
            Pump();
            if (g_widgetHost.Visibility() == Visibility::Visible && g_widgetHost.ActualHeight() > scenario.height + 0.1) {
                throw std::runtime_error("Widget overflows short taskbar");
            }
            if (scenario.height == 30 && (g_widgetHost.Visibility() != Visibility::Visible ||
                !g_widgetLayout || g_widgetLayout->scale * scenario.font < 9 - 1e-6))
                throw std::runtime_error("Short taskbar must select a readable adaptive layout");
            SaveImage(frame, argv[1], scenario.name);
            if (!scenario.unavailable) {
                PublishDocumentationSamples(); frame.UpdateLayout(); Pump();
            }
            std::wstring documentationName = L"documentation-" + std::wstring(scenario.name);
            SaveImage(frame, argv[1], documentationName.c_str());
            std::wcout << L"RENDERED " << scenario.name << L" host-height="
                       << g_widgetHost.ActualHeight() << L"\n";
        }
        // Exercise the actual XAML controls and native renderer across both
        // dimensions. Desktop DPI is unchanged; preview DPI is a value input.
        std::array<bool, 4> renderedModes{};
        int adaptiveCases = 0;
        for (int fontSize : {9, 11, 13}) {
            for (int height : {20, 24, 30, 38, 48}) {
                for (int gap : {80, 150, 180, 200, 260, 330, 410}) {
                    auto settings = std::make_shared<ModSettings>(*initialSettings);
                    settings->width = 410; settings->fontSize = fontSize; settings->leftOffset = 6;
                    g_settings = settings;
                    frame.Width(gap + 12); root.Width(gap + 12);
                    frame.Height(height); root.Height(height);
                    frame.Measure(Size{static_cast<float>(gap + 12), static_cast<float>(height)});
                    frame.Arrange(Rect{0, 0, static_cast<float>(gap + 12), static_cast<float>(height)});
                    frame.UpdateLayout(); Pump(); ApplyWidgetSettings();
                    MetricsSnapshot maximum; maximum.capturedAt = SampleTime::clock::now();
                    maximum.cpuAvailable = maximum.gpuAvailable = maximum.ramAvailable = maximum.vramAvailable = true;
                    maximum.cpu = maximum.gpu = maximum.ram = maximum.vram = 100;
                    maximum.cpuTemp = 200; maximum.gpuTemp = -74;
                    maximum.ramUsedGb = maximum.ramTotalGb = 1024;
                    maximum.vramUsedGb = maximum.vramTotalGb = 96;
                    PublishMetrics(maximum); UpdateWidgetText(true); frame.UpdateLayout(); Pump();
                    auto applies = g_layoutApplyCount;
                    for (int i = 0; i < 5; ++i) { ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump(); }
                    if (g_layoutApplyCount != applies || settings->width != 410 || settings->leftOffset != 6)
                        throw std::runtime_error("Stable geometry reapplied its layout or rewrote user preferences");
                    ++adaptiveCases;
                    if (g_widgetHost.Visibility() == Visibility::Collapsed) continue;
                    const auto layout = *g_widgetLayout;
                    auto hostBounds = g_widgetHost.TransformToVisual(root).TransformBounds(
                        {0, 0, static_cast<float>(g_widgetHost.ActualWidth()), static_cast<float>(g_widgetHost.ActualHeight())});
                    if (layout.scale * fontSize < 9 - 1e-6 || hostBounds.X < 5.9 ||
                        hostBounds.X + hostBounds.Width > gap + 6.1 || hostBounds.Y < -.1 ||
                        hostBounds.Y + hostBounds.Height > height + .1)
                        throw std::runtime_error("Actual adaptive host violates readability or panel bounds");
                    CaptureWidgetPreviewContext capture; CaptureWidgetPreview(&capture);
                    for (double dpi : {1., 1.25, 1.5, 1.75, 2.}) {
                        MoveEditorState editor; editor.visual = capture.frame;
                        editor.target.geometry = {static_cast<double>(gap + 12), static_cast<double>(height), {}, true};
                        editor.target.origin = {-1000, 100}; editor.target.scale = dpi;
                        editor.candidate = {g_widgetHost.Margin().Left, g_widgetHost.Width(), g_reservedMargin};
                        auto previewLayout = ResolveMovePreviewLayout(editor, {-2000, 0, 2000, 2000});
                        if (!SameWidgetLayout(layout, previewLayout.widget) ||
                            std::abs(previewLayout.contentScale - layout.scale * dpi) > 1e-6 ||
                            !PaintMovePreviewSurface(editor, previewLayout))
                            throw std::runtime_error("Native preview differs from the actual destination XAML layout");
                        LONG contentWidth = previewLayout.content.right - previewLayout.content.left;
                        POINT pointer{previewLayout.bounds.left + previewLayout.content.left + contentWidth / 3, 100};
                        double captured = static_cast<double>(contentWidth / 3) / contentWidth;
                        auto drag = ResolveWidgetPlacement(*settings, editor.target.geometry,
                            {0, editor.candidate.left, std::nullopt,
                             WidgetPointerAnchor{ScreenPointToTaskbarX(editor.target, pointer), captured}}, capture.frame.fontMetrics);
                        editor.candidate = drag.placement;
                        auto afterDrag = ResolveMovePreviewLayout(editor, {-2000, 0, 2000, 2000});
                        double heldPoint = afterDrag.bounds.left + afterDrag.content.left +
                            captured * (afterDrag.content.right - afterDrag.content.left);
                        if (!SameWidgetLayout(layout, drag.layout) || std::abs(heldPoint - pointer.x) > 1)
                            throw std::runtime_error("Grabbing the actual preview content shifts an adaptive widget");
                        Gdiplus::Graphics measure(editor.surface->dc);
                        Gdiplus::StringFormat textFormat(Gdiplus::StringFormat::GenericTypographic());
                        for (int index = 0; index < 12; ++index) {
                            const auto& text = editor.visual.texts[index];
                            if (text.bounds.Width <= 0) continue;
                            auto font = MovePreviewFont(text);
                            Gdiplus::RectF glyphs;
                            measure.MeasureString(text.text.c_str(), static_cast<int>(text.text.size()), font.get(),
                                Gdiplus::PointF(0, 0), &textFormat, &glyphs);
                            if (glyphs.Width > text.bounds.Width + .1 || glyphs.Height > text.bounds.Height + .1) {
                                std::wcerr << L"CELL font=" << fontSize << L" height=" << height << L" gap=" << gap
                                    << L" mode=" << static_cast<int>(layout.mode) << L" index=" << index << L" text=" << text.text
                                    << L" glyph=" << glyphs.Width << L"," << glyphs.Height << L" cell=" << text.bounds.Width
                                    << L"," << text.bounds.Height << L" measured-height=" << capture.frame.fontMetrics.textHeight << L"\n";
                                throw std::runtime_error("Native adaptive cells clip a maximum value or label");
                            }
                        }
                    }
                    size_t mode = static_cast<size_t>(layout.mode);
                    if (fontSize == 11 && layout.scale > .999 && !renderedModes[mode]) {
                        renderedModes[mode] = true;
                        for (int theme = 0; theme < 3; ++theme) {
                            root.RequestedTheme(theme == 1 ? ElementTheme::Light : ElementTheme::Dark);
                            root.Background(SolidColorBrush(theme == 1 ? Color{255, 242, 242, 242} : Color{255, 32, 32, 32}));
                            frame.UpdateLayout(); ApplyWidgetSettings(); Pump();
                            if (theme == 2) {
                                // Set the simulated palette after queued ActualThemeChanged
                                // callbacks have settled; do not change Windows settings.
                                for (TextBlock text : {g_cpuLabel, g_cpuUsageText, g_cpuTempText, g_ramLabel, g_ramPercentText,
                                    g_ramCapacityText, g_gpuLabel, g_gpuUsageText, g_gpuTempText, g_vramLabel,
                                    g_vramPercentText, g_vramCapacityText}) {
                                    text.Foreground(SolidColorBrush(ColorFromColorRef(GetSysColor(COLOR_WINDOWTEXT)))); text.Opacity(1);
                                }
                                for (auto graph : {g_cpuGraph, g_gpuGraph}) graph.Stroke(SolidColorBrush(ColorFromColorRef(GetSysColor(COLOR_HIGHLIGHT))));
                                for (auto bar : {g_ramTrack, g_ramFill, g_vramTrack, g_vramFill}) bar.Fill(SolidColorBrush(ColorFromColorRef(GetSysColor(COLOR_HIGHLIGHT))));
                                root.Background(SolidColorBrush(ColorFromColorRef(GetSysColor(COLOR_WINDOW))));
                            }
                            frame.UpdateLayout();
                            std::wstring name = L"layout-" + std::to_wstring(mode) + L"-" + std::to_wstring(theme) + L".png";
                            SaveImage(frame, argv[1], name.c_str());
                            // Keep the boundary-value renders above; documentation
                            // gets a separate copy with ordinary illustrative readings.
                            PublishDocumentationSamples(); frame.UpdateLayout(); Pump();
                            if (!g_widgetLayout || g_widgetLayout->mode != layout.mode)
                                throw std::runtime_error("Documentation sample changed the selected adaptive layout");
                            name = L"documentation-layout-" + std::to_wstring(mode) + L"-" + std::to_wstring(theme) + L".png";
                            SaveImage(frame, argv[1], name.c_str());
                            maximum.capturedAt = SampleTime::clock::now();
                            PublishMetrics(maximum); UpdateWidgetText(true); frame.UpdateLayout(); Pump();
                        }
                    }
                }
            }
        }
        if (std::find(renderedModes.begin(), renderedModes.end(), false) != renderedModes.end())
            throw std::runtime_error("Actual XAML matrix did not exercise all four layouts");
        // Reproduce the hidden Verdana-9 widget from the review using actual
        // XAML measurements. The full-width preference must not cap one row.
        {
            auto wideCompact = std::make_shared<ModSettings>(*initialSettings);
            wideCompact->fontFamily = L"Verdana"; wideCompact->fontSize = 9;
            wideCompact->width = 330; wideCompact->leftOffset = 6; g_settings = wideCompact;
            root.RequestedTheme(ElementTheme::Dark);
            root.Background(SolidColorBrush(Color{255, 32, 32, 32}));
            frame.Width(1500); root.Width(1500); frame.Height(20); root.Height(20);
            frame.Measure(Size{1500, 20}); frame.Arrange(Rect{0, 0, 1500, 20});
            frame.UpdateLayout(); Pump(); ApplyWidgetSettings(); UpdateWidgetText(true); frame.UpdateLayout(); Pump();
            if (g_widgetHost.Visibility() != Visibility::Visible || g_widgetLayout->mode != WidgetLayoutMode::CompactOneRow ||
                std::abs(g_widgetLayout->scale - 1) > 1e-6 || g_widgetHost.Width() <= 330 || wideCompact->width != 330)
                throw std::runtime_error("A wide free gap hides or shrinks the measured Verdana-9 compact layout");
            CaptureWidgetPreviewContext capture; CaptureWidgetPreview(&capture);
            MoveEditorState editor; editor.visual = capture.frame; editor.target.geometry = {1500, 20, {}, true};
            editor.target.scale = 1.75; editor.candidate = {g_widgetHost.Margin().Left, g_widgetHost.Width(), 0};
            auto preview = ResolveMovePreviewLayout(editor, {0, 0, 4000, 2000});
            if (!SameWidgetLayout(preview.widget, *g_widgetLayout) || !PaintMovePreviewSurface(editor, preview))
                throw std::runtime_error("Wide compact preview differs from the measured XAML widget");
            SaveImage(frame, argv[1], L"compact-verdana9-width330.png");
            auto before = g_layoutApplyCount;
            ApplyTaskbarPlacement(*wideCompact); frame.UpdateLayout(); Pump();
            if (before != g_layoutApplyCount) throw std::runtime_error("Wide compact layout repeatedly rebuilds its geometry");
            frame.Width(342); root.Width(342);
            frame.Measure(Size{342, 20}); frame.Arrange(Rect{0, 0, 342, 20}); frame.UpdateLayout(); Pump();
            if (g_widgetHost.Visibility() != Visibility::Collapsed)
                throw std::runtime_error("Measured Verdana-9 layout ignores an actually insufficient free gap");
            frame.Width(1500); root.Width(1500);
            frame.Measure(Size{1500, 20}); frame.Arrange(Rect{0, 0, 1500, 20}); frame.UpdateLayout(); Pump();
            if (g_widgetHost.Visibility() != Visibility::Visible || std::abs(g_widgetLayout->scale - 1) > 1e-6)
                throw std::runtime_error("Readable compact layout does not return after the gap expands");
        }
        std::wcout << L"PASS: Verdana-9 at width preference 330 uses its measured width, hides and restores correctly\n";
        for (PCWSTR family : {L"Consolas", L"Verdana"}) {
            auto alternate = std::make_shared<ModSettings>(*initialSettings);
            alternate->fontFamily = family; alternate->fontSize = 13; alternate->leftOffset = 6;
            g_settings = alternate;
            frame.Width(422); root.Width(422); frame.Height(30); root.Height(30);
            frame.Measure(Size{422, 30}); frame.Arrange(Rect{0, 0, 422, 30});
            frame.UpdateLayout(); ApplyWidgetSettings(); frame.UpdateLayout(); Pump();
            auto measured = MeasureWidgetFont(*alternate);
            CaptureWidgetPreviewContext capture; CaptureWidgetPreview(&capture);
            if (g_measuredFontFamily != family || g_widgetHost.Visibility() != Visibility::Visible ||
                capture.frame.configuredFont != family || g_widgetLayout->scale * 13 < 9 - 1e-6)
                throw std::runtime_error("Changing font family retained old measurements or unreadable layout");
            MoveEditorState editor; editor.visual = capture.frame;
            editor.target.geometry = {422, 30, {}, true}; editor.target.scale = 1.75;
            editor.candidate = {g_widgetHost.Margin().Left, g_widgetHost.Width(), g_reservedMargin};
            auto nativeLayout = ResolveMovePreviewLayout(editor, {0, 0, 2000, 2000});
            if (!SameWidgetLayout(nativeLayout.widget, *g_widgetLayout) || !PaintMovePreviewSurface(editor, nativeLayout))
                throw std::runtime_error("Alternate font preview diverged from XAML");
            auto before = g_layoutApplyCount;
            ApplyTaskbarPlacement(*alternate); frame.UpdateLayout(); Pump();
            if (g_layoutApplyCount != before || measured.widths != MeasureWidgetFont(*alternate).widths)
                throw std::runtime_error("Font measurement cache is unstable");
        }
        // Capture a compact source, then reflow its logical snapshot into
        // two destination heights. Source-hidden capacities/history must survive.
        auto crossSettings = std::make_shared<ModSettings>(*initialSettings);
        crossSettings->leftOffset = 6; g_settings = crossSettings;
        frame.Width(422); root.Width(422); frame.Height(20); root.Height(20);
        frame.Measure(Size{422, 20}); frame.Arrange(Rect{0, 0, 422, 20});
        frame.UpdateLayout(); ApplyWidgetSettings(); frame.UpdateLayout(); Pump();
        if (g_widgetLayout->mode != WidgetLayoutMode::CompactOneRow)
            throw std::runtime_error("Low source taskbar did not switch to one row");
        auto heldHistory = g_cpuHistory;
        CaptureWidgetPreviewContext crossCapture; CaptureWidgetPreview(&crossCapture);
        for (int destinationHeight : {30, 48}) {
            TaskbarGeometry destination{422, static_cast<double>(destinationHeight), {}, true};
            auto resolved = ResolveWidgetPlacement(*crossSettings, destination, {6, 6}, crossCapture.frame.fontMetrics);
            MoveEditorState editor; editor.visual = crossCapture.frame;
            editor.target.geometry = destination; editor.target.scale = 2;
            editor.candidate = resolved.placement;
            auto preview = ResolveMovePreviewLayout(editor, {0, 0, 4000, 2000});
            if (!PaintMovePreviewSurface(editor, preview)) throw std::runtime_error("Cross-height preview failed");
            frame.Height(destinationHeight); root.Height(destinationHeight);
            frame.Measure(Size{422, static_cast<float>(destinationHeight)});
            frame.Arrange(Rect{0, 0, 422, static_cast<float>(destinationHeight)});
            frame.UpdateLayout(); Pump();
            bool fits = false; VerifyMovedWidget(&fits);
            if (!fits || !SameWidgetLayout(*g_widgetLayout, preview.widget) ||
                g_cpuHistory.size() != heldHistory.size() ||
                (!heldHistory.empty() && g_cpuHistory.back().time != heldHistory.back().time))
                throw std::runtime_error("Destination verification differs from preview or changes history age");
            PreviewPlacementVerification confirmation{resolved.placement, preview.widget};
            VerifyPreviewedWidget(&confirmation);
            if (!confirmation.fits)
                throw std::runtime_error("Commit verification rejects a destination matching its preview");
            confirmation.expected.left += 1;
            VerifyPreviewedWidget(&confirmation);
            if (confirmation.fits)
                throw std::runtime_error("Commit verification accepts a shifted widget");
            confirmation.expected = resolved.placement; confirmation.layout.scale *= .9;
            VerifyPreviewedWidget(&confirmation);
            if (confirmation.fits)
                throw std::runtime_error("Commit verification accepts a different preview scale");
            if (destinationHeight == 48 && (editor.visual.texts[5].bounds.Width <= 0 ||
                editor.visual.texts[5].text != std::wstring(g_ramCapacityText.Text()) ||
                editor.visual.graphs[0].runs.empty()))
                throw std::runtime_error("Full destination lost source-hidden details or graph history");
        }
        std::wcout << L"PASS: one-row source reflows into 30/48-DIP destinations with intact details and history\n";
        StopPreviewGraphics();
        std::wcout << L"PASS: " << adaptiveCases << L" XAML width/height/font cases, five preview DPIs and four rendered layouts\n";
        {
            ModSettings settings = *initialSettings;
            std::array<WidgetFontMetrics, 5> fonts;
            for (int size = 9; size <= 13; ++size) {
                settings.fontSize = size; fonts[size - 9] = MeasureWidgetFont(settings);
            }
            std::mt19937 random(20261004);
            auto pick = [&](int low, int high) { return std::uniform_int_distribution<int>(low, high)(random); };
            int visible = 0;
            for (int i = 0; i < 200000; ++i) {
                settings.width = pick(330, 800); settings.fontSize = pick(9, 13);
                settings.reserveSpace = pick(0, 1); settings.reserveGap = pick(0, 100);
                TaskbarGeometry geometry{static_cast<double>(pick(500, 2500)),
                                         static_cast<double>(pick(15, 64)), {}, true};
                double start = pick(0, static_cast<int>(geometry.width) / 3), stop = start + pick(30, 180);
                geometry.occupied = {{start, stop, true}, {geometry.width - pick(70, 160), geometry.width}};
                if (pick(0, 1)) geometry.occupied.push_back({stop + 80, stop + 160});
                bool overlapping = false;
                for (const auto& a : geometry.occupied) for (const auto& b : geometry.occupied)
                    if (a.movable != b.movable && Intersects(a.left, a.right, b)) overlapping = true;
                double wanted = pick(0, 2500);
                if (overlapping) continue;
                const auto& font = fonts[settings.fontSize - 9];
                auto draft = ResolveWidgetPlacement(settings, geometry, {wanted, 0}, font);
                if (!draft.placement.width) continue;
                ++visible;
                auto saved = ConfirmPlacementPreference({}, settings, L"measured-matrix", draft.placement.left,
                                                        geometry.width, false, draft.placement.width);
                auto encoded = ParseProfiles(SerializeProfiles(*saved));
                auto restored = ResolveWidgetPlacement(settings, geometry,
                    {0, draft.placement.left, encoded->positions.at(L"measured-matrix")}, font);
                if (!PlacementFits(geometry, draft.placement) || !SameWidgetLayout(draft.layout, restored.layout) ||
                    std::abs(draft.layout.scale - restored.layout.scale) > 1e-6 ||
                    std::abs(draft.placement.left - restored.placement.left) > kPlacementTolerance ||
                    std::abs(draft.placement.width - restored.placement.width) > kPlacementTolerance ||
                    std::abs(draft.placement.reserved - restored.placement.reserved) > kPlacementTolerance)
                    throw std::runtime_error("Measured-font placement changed after confirmation");
            }
            std::wcout << L"PASS: 200000 generated real-font geometries, " << visible
                       << L" visible previews preserve placement/layout after serialization\n";
        }
        // Reproduce the failed confirmation with real measured text, movable
        // buttons and two fixed obstacles. Exercise apply/verify/persist together.
        {
            auto reserved = std::make_shared<ModSettings>(*initialSettings);
            reserved->width = 735; reserved->fontSize = 11; reserved->leftOffset = 444;
            reserved->reserveSpace = true; reserved->reserveGap = 50; g_settings = reserved;
            auto previousWindow = g_taskbarWindow.load();
            auto previousKeys = g_monitorKeys;
            g_taskbarWindow = window;
            MONITORINFOEXW info{}; info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
                throw std::runtime_error("Isolated confirmation monitor lookup failed");
            g_monitorKeys[info.szDevice] = L"isolated-confirmation-display";
            SetPlacementProfiles({});
            SetWindowPos(window, nullptr, 0, 0, 957, 29, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(child, nullptr, 0, 0, 957, 29, SWP_NOZORDER | SWP_NOACTIVATE);
            frame.Width(957); root.Width(957); frame.Height(29); root.Height(29);
            Canvas movable; movable.Name(L"TaskbarFrameRepeater");
            movable.Width(957); movable.Height(29); movable.HorizontalAlignment(HorizontalAlignment::Left);
            Button app; app.Width(96); app.Height(29); Canvas::SetLeft(app, 316);
            movable.Children().Append(app); root.Children().Append(movable);
            Canvas fixed; fixed.Width(957); fixed.Height(29);
            Button obstacle; obstacle.Width(80); obstacle.Height(29); Canvas::SetLeft(obstacle, 492);
            fixed.Children().Append(obstacle);
            XamlRectangle fixedTray; fixedTray.Name(L"SystemTrayFrame");
            fixedTray.Width(155); fixedTray.Height(29); Canvas::SetLeft(fixedTray, 802);
            fixed.Children().Append(fixedTray); root.Children().Append(fixed);
            frame.Measure(Size{957, 29}); frame.Arrange(Rect{0, 0, 957, 29});
            frame.UpdateLayout(); Pump(); ApplyWidgetSettings(); frame.UpdateLayout(); Pump();
            bool fits = false; VerifyMovedWidget(&fits);
            if (!fits) throw std::runtime_error("Reserved confirmation fixture has no valid preview");
            auto expected = TaskbarPlacement{g_widgetHost.Margin().Left, g_widgetHost.Width(), g_reservedMargin};
            auto expectedLayout = *g_widgetLayout;
            auto before = PlacementProfilesSnapshot();
            auto next = ConfirmPlacementPreference(before, *reserved, L"isolated-confirmation-display",
                                                    expected.left, 957, false, expected.width);
            if (!next || !CompleteMoveTransaction(before, *next, g_moveEpoch.load(),
                [&] { ApplyWidgetSettings(); return true; },
                [&] {
                    PreviewPlacementVerification verification{expected, expectedLayout};
                    VerifyPreviewedWidget(&verification); return verification.fits;
                }, [&] { ApplyWidgetSettings(); return true; }))
                throw std::runtime_error("Saving a valid reserved preview changed its layout or position");
            auto stored = ParseProfiles(uiStorage::values.at(L"placement.v1"));
            if (!stored || stored->target != next->target || stored->positions != next->positions)
                throw std::runtime_error("Verified reserved placement was not persisted");
            SetPlacementProfiles(*stored); ApplyWidgetSettings();
            PreviewPlacementVerification reloaded{expected, expectedLayout}; VerifyPreviewedWidget(&reloaded);
            if (!reloaded.fits) throw std::runtime_error("Reloading a reserved placement changed its preview");
            SaveImage(frame, argv[1], L"reserved-confirmation.png");
            std::wcout << L"PASS: reserved XAML confirmation/reload preserves x=" << expected.left
                       << L", width=" << expected.width << L" and its preview layout\n";
            SetPlacementProfiles({}); uiStorage::values.clear();
            root.Children().RemoveAtEnd(); root.Children().RemoveAtEnd();
            g_monitorKeys = std::move(previousKeys); g_taskbarWindow = previousWindow;
            SetWindowPos(window, nullptr, 0, 0, 520, 64, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(child, nullptr, 0, 0, 520, 64, SWP_NOZORDER | SWP_NOACTIVATE);
            g_settings = crossSettings; ApplyWidgetSettings(); frame.UpdateLayout(); Pump();
        }
        // Model taskbar buttons and tray inside the same selected XAML root.
        // Exercise the production size handlers without refreshing settings.
        Grid buttons;
        buttons.Name(L"TaskbarFrameRepeater");
        Button buttonContent;
        buttonContent.Width(300);
        buttons.Children().Append(buttonContent);
        buttons.Height(38);
        buttons.HorizontalAlignment(HorizontalAlignment::Left);
        buttons.VerticalAlignment(VerticalAlignment::Center);
        buttons.Background(SolidColorBrush(Color{255, 70, 70, 70}));
        Grid tray;
        tray.Name(L"SystemTrayFrame");
        tray.Width(120);
        tray.Height(38);
        tray.HorizontalAlignment(HorizontalAlignment::Right);
        tray.VerticalAlignment(VerticalAlignment::Center);
        tray.Background(SolidColorBrush(Color{255, 90, 90, 90}));
        root.Children().Append(buttons);
        root.Children().Append(tray);
        // A real repeater may stretch even when its buttons occupy only 300 DIPs.
        // Both reported offsets must stay visible with reservation enabled.
        buttonContent.HorizontalAlignment(HorizontalAlignment::Left);
        buttons.HorizontalAlignment(HorizontalAlignment::Stretch);
        frame.Width(1920); root.Width(1920);
        frame.Height(48); root.Height(48);
        auto regressionSettings = std::make_shared<ModSettings>(*initialSettings);
        regressionSettings->reserveSpace = true;
        for (int offset : {0, 2000}) {
            regressionSettings->leftOffset = offset;
            { std::lock_guard lock(g_settingsMutex); g_settings = regressionSettings; }
            if (!RemoveWidget() || !InjectWidget(frame))
                throw std::runtime_error("Stretch regression reinjection failed");
            frame.Measure(Size{1920, 48}); frame.Arrange(Rect{0, 0, 1920, 48});
            frame.UpdateLayout(); Pump(); ApplyWidgetSettings();
            frame.UpdateLayout(); Pump();
            if (g_widgetHost.Visibility() != Visibility::Visible ||
                g_widgetHost.Width() < 348.5)
                throw std::runtime_error("Stretched repeater hides widget with usable space");
        }
        buttons.HorizontalAlignment(HorizontalAlignment::Left);
        if (!RemoveWidget() || !InjectWidget(frame))
            throw std::runtime_error("Adaptive reinjection failed");
        auto settings = std::make_shared<ModSettings>(*initialSettings);
        settings->leftOffset = 3500;
        settings->reserveSpace = true;
        { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
        root.RequestedTheme(ElementTheme::Dark);
        MetricsSnapshot populated;
        populated.capturedAt = SampleTime::clock::now();
        populated.cpuAvailable = populated.gpuAvailable = true;
        populated.ramAvailable = populated.vramAvailable = true;
        populated.cpu = 17; populated.gpu = 25;
        populated.ram = 52; populated.ramUsedGb = 16.7; populated.ramTotalGb = 32;
        populated.vram = 9; populated.vramUsedGb = 2.1; populated.vramTotalGb = 24;
        populated.cpuTemp = 72; populated.gpuTemp = 56;
        PublishMetrics(populated); UpdateWidgetText(true);
        const int widths[] = {1920, 1280, 2560, 700, 400, 1920};
        bool firstLayout = true;
        for (int width : widths) {
            frame.Width(width); root.Width(width);
            frame.Height(48); root.Height(48);
            frame.Measure(Size{static_cast<float>(width), 48});
            frame.Arrange(Rect{0, 0, static_cast<float>(width), 48});
            frame.UpdateLayout(); Pump();
            // Settings are applied once; subsequent layouts use SizeChanged.
            if (firstLayout) { ApplyWidgetSettings(); firstLayout = false; }
            frame.UpdateLayout(); Pump();
            double available = width - tray.ActualWidth();
            TaskbarPlacement expected{};
            if (available - 312 >= 410) expected = {available - 416, 410, 0};
            else if (available - 312 >= 348.5) expected = {306, available - 312, 0};
            else {
                auto compact = ResolveWidgetPlacement(*settings,
                    {static_cast<double>(width), 48, {{0, 300, true}, {available, static_cast<double>(width)}}, true},
                    {3500, g_previousPlacementLeft}, g_widgetFontMetrics, true);
                expected = compact.placement;
                if (width == 700 && (expected.width <= 0 || compact.layout.showGraphs))
                    throw std::runtime_error("Narrow free gap must preserve essential values with the compact layout");
            }
            std::wcout << L"LAYOUT " << width << L" root=" << root.ActualWidth()
                       << L" available=" << TaskbarAvailableWidth()
                       << L" buttons=" << buttons.ActualWidth()
                       << L" desired=" << buttons.DesiredSize().Width
                       << L" margin=" << buttons.Margin().Left
                       << L" offset=" << g_widgetHost.Margin().Left
                       << L" width=" << g_widgetHost.Width()
                       << L" reserved=" << g_reservedMargin
                       << L" expected=" << expected.left << L"," << expected.width
                       << L"," << expected.reserved << L"\n";
            if (!std::isfinite(g_widgetHost.Width()) ||
                !std::isfinite(g_widgetHost.Height()) ||
                std::abs(g_widgetHost.Margin().Left - expected.left) > 0.1 ||
                std::abs(g_reservedMargin - expected.reserved) > 0.1 ||
                std::abs(g_widgetHost.Width() - expected.width) > 0.1 ||
                (expected.width == 0 && g_widgetHost.Visibility() != Visibility::Collapsed)) {
                throw std::runtime_error("Adaptive resize did not preserve widget/buttons/tray");
            }
            if (g_widgetHost.Visibility() == Visibility::Visible) {
                auto position = g_widgetHost.TransformToVisual(root).TransformPoint({0, 0});
                auto labelPoint = g_cpuLabel.TransformToVisual(g_widget).TransformPoint({0, 0});
                auto memoryPoint = g_ramTrack.TransformToVisual(g_widget).TransformPoint({0, 0});
                if (position.X < buttonContent.ActualWidth() + 5.9 ||
                    position.X + g_widgetHost.ActualWidth() > available - 5.9 ||
                    g_widgetHost.ActualHeight() > root.ActualHeight() + 0.1)
                    throw std::runtime_error("Rendered widget loses minimum clearance from panel controls");
                if (std::abs(labelPoint.X - 6) > .1 ||
                    (g_widgetLayout->showMemoryDetails &&
                     std::abs(memoryPoint.X + g_ramTrack.ActualWidth() - (g_widgetLayout->width - 6)) > .1))
                    throw std::runtime_error("Rendered widget loses its internal side padding");
            }
            double before = g_reservedMargin;
            ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump();
            if (std::abs(before - g_reservedMargin) > 0.1)
                throw std::runtime_error("Reservation accumulated after repeated placement");
            std::wcout << L"RESIZED " << width << L" offset=" << expected.left
                       << L" width=" << expected.width << L" reserved=" << g_reservedMargin << L"\n";
            if (width == 1280 || width == 700) {
                SaveImage(frame, argv[1], width == 1280 ? L"adaptive-1280.png" : L"adaptive-700.png");
            }
        }
        // Expanding the tray and adding buttons must reclaim space immediately.
        tray.Width(240); buttonContent.Width(600);
        frame.UpdateLayout(); Pump();
        TaskbarPlacement crowded{1264, 410, 0};
        if (std::abs(g_reservedMargin - crowded.reserved) > 0.1)
            throw std::runtime_error("Tray/button size changes did not update reservation");
        // Moving a tray without changing its size must update the usable width.
        tray.Margin(Thickness{0, 0, 20, 0});
        frame.UpdateLayout(); Pump();
        crowded = {1244, 410, 0};
        if (std::abs(g_reservedMargin - crowded.reserved) > 0.1)
            throw std::runtime_error("Tray movement without resize did not update placement");
        tray.Margin(Thickness{}); frame.UpdateLayout(); Pump();
        // Reservation uses the external base margin exactly once.
        buttons.Margin(Thickness{20, 0, 10, 0});
        ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump();
        crowded = {1264, 410, 0};
        if (std::abs(buttons.Margin().Left - (20 + crowded.reserved)) > 0.1)
            throw std::runtime_error("External repeater margins were not preserved");
        // Fractional measurements must settle without accumulating margins.
        buttonContent.Width(600.125);
        tray.Margin(Thickness{0, 0, 20.125, 0});
        buttons.Margin(Thickness{20.125, 0, 10.125, 0});
        ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump();
        double stableMargin = buttons.Margin().Left;
        for (int i = 0; i < 100; ++i) {
            ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump();
        }
        if (std::abs(buttons.Margin().Left - stableMargin) > 0.01)
            throw std::runtime_error("Fractional placement margins drifted");
        settings = std::make_shared<ModSettings>(*settings);
        settings->reserveSpace = false;
        { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
        ApplyWidgetSettings(); frame.UpdateLayout(); Pump();
        if (std::abs(buttons.Margin().Left - 20.125) > 0.1 || g_reservedMargin != 0)
            throw std::runtime_error("Disabling reservation did not restore external margin");
        // Independent fixed controls, transforms and hidden controls must form
        // separate obstacles rather than treating their parent grid as full.
        Button search;
        search.Width(180); search.Height(38); search.HorizontalAlignment(HorizontalAlignment::Left);
        search.Margin(Thickness{850, 0, 0, 0});
        TranslateTransform translation; translation.X(40); search.RenderTransform(translation);
        Button hidden; hidden.Width(1920); hidden.Visibility(Visibility::Collapsed);
        root.Children().Append(search); root.Children().Append(hidden);
        settings->leftOffset = 900; settings->reserveSpace = false;
        { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
        ApplyWidgetSettings(); frame.UpdateLayout(); Pump();
        for (int i = 0; i < 3; ++i) {
            ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump();
        }
        auto searchPoint = search.TransformToVisual(root).TransformPoint({0, 0});
        auto hostPoint = g_widgetHost.TransformToVisual(root).TransformPoint({0, 0});
        if (g_widgetHost.Visibility() != Visibility::Visible ||
            (hostPoint.X < searchPoint.X + search.ActualWidth() - .1 &&
             hostPoint.X + g_widgetHost.Width() > searchPoint.X + .1))
            throw std::runtime_error("Transformed search control was not avoided");
        SaveImage(frame, argv[1], L"mapped-search.png");
        size_t cachedRebuilds = g_geometryCache.rebuilds;
        for (int i = 0; i < 10; ++i) {
            g_cpuUsageText.Text(i % 2 ? L"25%" : L"26%");
            frame.UpdateLayout(); Pump(); ApplyTaskbarPlacement(*settings);
        }
        if (g_geometryCache.rebuilds != cachedRebuilds)
            throw std::runtime_error("Metric text changes rediscovered the entire XAML tree");
        hidden.Height(38); hidden.Visibility(Visibility::Visible);
        frame.UpdateLayout(); Pump();
        if (g_widgetHost.Visibility() != Visibility::Collapsed || g_geometryCache.rebuilds != cachedRebuilds)
            throw std::runtime_error("Cached hidden control did not become an obstacle");
        hidden.Visibility(Visibility::Collapsed); frame.UpdateLayout(); Pump();
        if (g_widgetHost.Visibility() != Visibility::Visible)
            throw std::runtime_error("Hiding a cached obstacle did not restore placement");
        // Replace a child at the same count: stale detached objects must cause
        // rediscovery, not remain in the occupied-area map.
        root.Children().RemoveAtEnd();
        Button replacement; replacement.Width(1920); replacement.Height(38);
        root.Children().Append(replacement); frame.UpdateLayout(); Pump();
        if (g_widgetHost.Visibility() != Visibility::Collapsed || g_geometryCache.rebuilds <= cachedRebuilds)
            throw std::runtime_error("Same-count child replacement escaped cache invalidation");
        root.Children().RemoveAtEnd(); root.Children().RemoveAtEnd();
        // Centered layout does not necessarily translate by Margin.Left. A
        // failed reservation must restore its base once, then remain stable.
        tray.Margin(Thickness{}); tray.Width(120);
        buttonContent.Width(300); buttons.Margin(Thickness{});
        buttons.HorizontalAlignment(HorizontalAlignment::Center);
        settings->leftOffset = 750; settings->reserveSpace = true;
        { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
        frame.UpdateLayout(); Pump(); ApplyWidgetSettings();
        for (int i = 0; i < 20; ++i) { frame.UpdateLayout(); Pump(); }
        if (g_reservedMargin != 0 || g_widgetHost.Visibility() != Visibility::Visible)
            throw std::runtime_error("Rejected centered reservation did not fall back");
        double settledLeft = g_widgetHost.Margin().Left;
        for (int i = 0; i < 100; ++i) { ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump(); }
        if (g_reservedMargin != 0 || std::abs(g_widgetHost.Margin().Left - settledLeft) > .1)
            throw std::runtime_error("Rejected reservation retried in a feedback loop");
        auto buttonPoint = buttonContent.TransformToVisual(root).TransformPoint({0, 0});
        if (settledLeft < buttonPoint.X + buttonContent.ActualWidth() - .1 &&
            settledLeft + g_widgetHost.Width() > buttonPoint.X + .1)
            throw std::runtime_error("Centered fallback overlaps a real button");
        SaveImage(frame, argv[1], L"centered-fallback.png");
        // Render the production native editor without showing any desktop UI.
        StopMetricsWorker(); // Keep these illustrative render samples deterministic.
        g_lastRenderedMetricsSequence = 0;
        g_cpuHistory.clear(); g_gpuHistory.clear();
        { std::lock_guard lock(g_metricsMutex); g_publishedMetrics.clear(); }
        auto previewTime = SampleTime::clock::now();
        for (int i = 0; i <= 60; ++i) {
            MetricsSnapshot sample; sample.capturedAt = previewTime - std::chrono::seconds(60 - i);
            sample.cpuAvailable = sample.gpuAvailable = sample.ramAvailable = sample.vramAvailable = true;
            sample.cpu = 8 + i * .1 + 4 * std::sin(i / 7.0); sample.gpu = 5 + 3 * std::sin(i / 5.0);
            sample.ram = 52; sample.ramUsedGb = 16.7; sample.ramTotalGb = 32;
            sample.vram = 42; sample.vramUsedGb = 10.1; sample.vramTotalGb = 24;
            sample.cpuTemp = 72; sample.gpuTemp = 56; PublishMetrics(sample);
        }
        UpdateWidgetText(true); frame.UpdateLayout(); Pump();
        WNDCLASSW editorClass{}; editorClass.hInstance = PlacementModule();
        editorClass.lpfnWndProc = MoveEditorProc; editorClass.lpszClassName = kMoveWindowClass;
        if (!RegisterClassW(&editorClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Preview test class registration failed");
        g_moveEditor = std::make_shared<MoveEditorState>();
        HWND preview = CreateWindowExW(0, kMoveWindowClass, L"", WS_CHILD, 0, 0, 410, 38,
                                       window, nullptr, editorClass.hInstance, nullptr);
        if (!preview) throw std::runtime_error("Preview render window failed");
        g_moveEditor->window = preview; g_moveEditorWindow = preview; g_moveEditor->source = g_taskbarWindow.load();
        g_moveEditor->target.geometry = {1920, 48, {}, true};
        g_moveEditor->candidate = {500, 410, 0};
        RefreshMovePreviewVisual(); RenderMovePreview();
        if (!g_moveEditor->visual.ready || g_moveEditor->visual.texts.size() != 12 ||
            g_moveEditor->visual.graphs.size() != 2 || g_moveEditor->visual.bars.size() != 4)
            throw std::runtime_error("Move preview does not contain the real widget content");
        auto usageSlot = Controls::Primitives::LayoutInformation::GetLayoutSlot(g_cpuUsageText);
        auto usageParent = g_cpuUsageText.Parent().as<FrameworkElement>();
        auto usageRight = usageParent.TransformToVisual(g_widget).TransformPoint(
            {usageSlot.X + usageSlot.Width, usageSlot.Y});
        const auto& previewUsage = g_moveEditor->visual.texts[1];
        if (std::abs(previewUsage.bounds.X + previewUsage.bounds.Width - usageRight.X) > .1)
            throw std::runtime_error("Native preview utilization lost the right edge of its actual XAML cell");
        for (int index : {0, 3, 6, 9}) {
            const auto& label = g_moveEditor->visual.texts[index]; auto font = MovePreviewFont(label);
            Gdiplus::Graphics measure(g_moveEditor->surface->dc); Gdiplus::RectF bounds;
            Gdiplus::StringFormat textFormat(Gdiplus::StringFormat::GenericTypographic());
            measure.MeasureString(label.text.c_str(), static_cast<int>(label.text.size()), font.get(),
                                  Gdiplus::PointF(0, 0), &textFormat, &bounds);
            if (bounds.Width > label.bounds.Width + .1f || font->GetUnit() != Gdiplus::UnitPixel) {
                std::wcerr << L"Preview label " << label.text << L": glyph width=" << bounds.Width
                           << L", cell=" << label.bounds.Width << L", source font=" << label.font
                           << L", size=" << label.fontSize << L", native size=" << font->GetSize()
                           << L", unit=" << font->GetUnit() << L"\n";
                throw std::runtime_error("DPI/font conversion trims a native preview label");
            }
        }
        auto bodyLayout = CurrentMovePreviewLayout(*g_moveEditor);
        if (bodyLayout.bounds.bottom - bodyLayout.bounds.top != 44 ||
            bodyLayout.bounds.right - bodyLayout.bounds.left != 410)
            throw std::runtime_error("Move preview retained space for removed instructions");
        SendMessageW(preview, WM_SETCURSOR, reinterpret_cast<WPARAM>(preview), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        if (GetCursor() != LoadCursorW(nullptr, IDC_HAND))
            throw std::runtime_error("Move mode did not expose the hand cursor");
        auto* surface = g_moveEditor->surface.get();
        unsigned tintAlpha = surface->pixels[(bodyLayout.body.top + 2) * surface->width + surface->width / 2] >> 24;
        if ((surface->pixels[0] >> 24) != 0 || tintAlpha != 1)
            throw std::runtime_error("Armed move mode lost its minimal hit area or painted glass before dragging");
        HWND layered = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP,
                                        0, 0, 1, 1, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!layered) throw std::runtime_error("Hidden layered preview upload host could not be created");
        bool uploaded = UploadMovePreviewSurface(layered, bodyLayout, *surface);
        RECT uploadedBounds{}; GetWindowRect(layered, &uploadedBounds);
        bool stayedHidden = !IsWindowVisible(layered); DestroyWindow(layered);
        if (!uploaded || !stayedHidden || uploadedBounds.right - uploadedBounds.left != surface->width)
            throw std::runtime_error("Real layered-window alpha upload failed or showed its private test host");
        SourceWidgetOpacityContext opacity{g_moveEditor->source, true};
        OnPreviewSourceThread(opacity.source, SetSourceWidgetOpacity, &opacity);
        g_moveEditor->sourceHidden = opacity.changed; g_moveEditor->sourceOpacity = opacity.original;
        if (!opacity.changed || g_widgetHost.Opacity() != 0)
            throw std::runtime_error("Editing did not hide the duplicate source widget");
        SaveNativePreview(preview, argv[1], L"move-valid.png");
        uint64_t oldSequence = g_moveEditor->visual.sequence;
        MetricsSnapshot live; live.capturedAt = SampleTime::clock::now();
        live.cpuAvailable = live.gpuAvailable = live.ramAvailable = live.vramAvailable = true;
        live.cpu = 61; live.gpu = 33; live.ram = 67; live.ramUsedGb = 21.4; live.ramTotalGb = 32;
        live.vram = 42; live.vramUsedGb = 10.1; live.vramTotalGb = 24;
        live.cpuTemp = 72; live.gpuTemp = 56;
        PublishMetrics(live); UpdateWidgetText(true);
        g_moveEditor->dragging = true;
        SendMessageW(preview, WM_TIMER, 1, 0); // Real callback keeps updating during a drag.
        if (g_moveEditor->visual.sequence <= oldSequence || g_moveEditor->visual.texts[1].text != L"61%" ||
            g_moveEditor->visual.texts[4].text != L"67%" || g_widgetHost.Opacity() != 0)
            throw std::runtime_error("Live preview stopped following metrics while dragging");
        tintAlpha = surface->pixels[2 * surface->width + surface->width / 2] >> 24;
        if (tintAlpha < 100 || tintAlpha >= 255)
            throw std::runtime_error("Active dragging lost its translucent background");
        SaveNativePreview(preview, argv[1], L"move-live-drag.png");
        // Moving a widget with an expired publication must preserve the age of
        // history that is still inside the graph window, not move it to "now".
        {
            auto heldPublication = g_publishedMetrics.back().snapshot;
            auto heldCpu = g_cpuHistory, heldGpu = g_gpuHistory;
            auto now = SampleTime::clock::now();
            g_publishedMetrics.back().snapshot.capturedAt = now - std::chrono::seconds(35);
            g_cpuHistory = {{now - std::chrono::seconds(36), 60}, {now - std::chrono::seconds(35), 61}};
            g_gpuHistory = {{now - std::chrono::seconds(36), 30}, {now - std::chrono::seconds(35), 33}};
            UpdateWidgetText();
            auto endX = [](XamlPath graph) {
                auto figures = graph.Data().as<PathGeometry>().Figures();
                auto segments = figures.GetAt(figures.Size() - 1).Segments();
                return segments.GetAt(segments.Size() - 1).as<LineSegment>().Point().X;
            };
            float cpuEnd = endX(g_cpuGraph), gpuEnd = endX(g_gpuGraph);
            auto sequence = g_lastRenderedMetricsSequence;
            bool fits = false; VerifyMovedWidget(&fits);
            if (!fits || endX(g_cpuGraph) > cpuEnd + .1f || endX(g_gpuGraph) > gpuEnd + .1f ||
                cpuEnd <= 0 || gpuEnd <= 0 || cpuEnd >= g_graphWidth * .75 ||
                g_lastRenderedMetricsSequence != sequence || g_cpuUsageText.Text() != L"--%")
                throw std::runtime_error("Move verification rejuvenated stalled graph history");
            ApplyWidgetSettings();
            if (endX(g_cpuGraph) > cpuEnd + .1f || endX(g_gpuGraph) > gpuEnd + .1f)
                throw std::runtime_error("Applying settings rejuvenated stalled graph history");
            g_publishedMetrics.back().snapshot = heldPublication;
            g_cpuHistory = std::move(heldCpu); g_gpuHistory = std::move(heldGpu);
            ApplyWidgetSettings(); UpdateWidgetText(true);
            std::wcout << L"PASS: move verification and settings redraw preserve stalled graph age\n";
        }
        // Age the already-rendered publication without changing its sequence:
        // the watchdog must expire even when the collector publishes nothing.
        auto lastSequence = g_lastRenderedMetricsSequence;
        {
            std::lock_guard lock(g_metricsMutex);
            g_publishedMetrics.back().snapshot.capturedAt -= std::chrono::minutes(5);
        }
        for (auto& sample : g_cpuHistory) sample.time -= std::chrono::minutes(5);
        for (auto& sample : g_gpuHistory) sample.time -= std::chrono::minutes(5);
        UpdateWidgetText(); frame.UpdateLayout(); Pump();
        SendMessageW(preview, WM_TIMER, 1, 0);
        if (g_lastRenderedMetricsSequence != lastSequence ||
            g_cpuUsageText.Text() != L"--%" || g_gpuUsageText.Text() != L"--%" ||
            g_ramPercentText.Text() != L"--%" || g_vramPercentText.Text() != L"--%" ||
            g_cpuTempText.Text() != L"--\u00B0C" || g_gpuTempText.Text() != L"--\u00B0C" ||
            g_ramFill.Width() != 0 || g_vramFill.Width() != 0 ||
            g_cpuTemperatureAlert != AlertLevel::Normal || g_ramAlert != AlertLevel::Normal ||
            g_cpuGraph.Visibility() != Visibility::Collapsed || g_gpuGraph.Visibility() != Visibility::Collapsed ||
            g_moveEditor->visual.sequence <= oldSequence || g_moveEditor->visual.texts[1].text != L"--%" ||
            !g_moveEditor->visual.graphs[0].runs.empty() || g_widgetHost.Opacity() != 0)
            throw std::runtime_error("Stalled collector retained live values, alerts, history or move preview");
        SaveNativePreview(preview, argv[1], L"move-stale.png");
        // A forced redraw must not resurrect the expired values.
        UpdateWidgetText(true);
        if (g_cpuUsageText.Text() != L"--%" || !g_cpuHistory.empty())
            throw std::runtime_error("Forced redraw resurrected an expired snapshot");
        live.capturedAt = SampleTime::clock::now() - std::chrono::seconds(1);
        PublishMetrics(live);
        live.capturedAt = SampleTime::clock::now(); PublishMetrics(live);
        UpdateWidgetText(); frame.UpdateLayout(); Pump();
        SendMessageW(preview, WM_TIMER, 1, 0);
        if (g_cpuUsageText.Text() != L"61%" || g_ramFill.Width() <= 0 ||
            g_cpuGraph.Visibility() != Visibility::Visible || g_cpuHistory.size() != 2 ||
            g_moveEditor->visual.texts[1].text != L"61%" ||
            g_moveEditor->visual.graphs[0].runs.empty())
            throw std::runtime_error("Fresh publication failed to recover after a collector stall");
        SaveNativePreview(preview, argv[1], L"move-recovered.png");
        std::wcout << L"PASS: no-publication expiry clears XAML/preview, forced redraw stays stale, fresh data recovers\n";
        g_moveEditor->dragging = false; g_moveEditor->hovered = true; g_moveEditor->dirty = true; RenderMovePreview();
        if ((surface->pixels[2 * surface->width + surface->width / 2] >> 24) != 1)
            throw std::runtime_error("Released/hovered preview lost its minimal hit area or retained glass");
        SaveNativePreview(preview, argv[1], L"move-hover.png");
        g_moveEditor->candidate = {}; g_moveEditor->dragging = true; g_moveEditor->dirty = true; RenderMovePreview();
        auto invalidPixel = g_moveEditor->surface->pixels[2 * g_moveEditor->surface->width + g_moveEditor->surface->width / 2];
        if (((invalidPixel >> 16) & 255) <= (invalidPixel & 255))
            throw std::runtime_error("Invalid placement has no red surface feedback");
        SaveNativePreview(preview, argv[1], L"move-invalid.png");
        g_moveEditor->dragging = false; g_moveEditor->dirty = true; RenderMovePreview();
        if ((surface->pixels[2 * surface->width + surface->width / 2] >> 24) != 1)
            throw std::runtime_error("Invalid idle position lost its minimal hit area or retained glass");
        g_moveEditor->candidate = {500, 410, 0}; g_moveEditor->reset = true;
        g_moveEditor->dirty = true; RenderMovePreview(); SaveNativePreview(preview, argv[1], L"move-reset.png");
        g_moveEditor->reset = false;
        g_moveEditor->dragging = true; g_moveEditor->dirty = true;
        root.RequestedTheme(ElementTheme::Light); frame.UpdateLayout(); ApplyWidgetSettings();
        RefreshMovePreviewVisual(); RenderMovePreview(); SaveNativePreview(preview, argv[1], L"move-light.png");
        settings->width = 330; settings->fontSize = 11; ApplyWidgetSettings(); frame.UpdateLayout();
        g_moveEditor->visual.ready = false; g_moveEditor->candidate.width = 330;
        RefreshMovePreviewVisual(); RenderMovePreview(); SaveNativePreview(preview, argv[1], L"move-narrow.png");
        settings->width = 430; settings->fontSize = 13; ApplyWidgetSettings(); frame.UpdateLayout();
        g_cpuUsageText.Text(L"100%"); g_cpuTempText.Text(L"100\u00B0C");
        g_gpuUsageText.Text(L"100%"); g_gpuTempText.Text(L"100\u00B0C"); frame.UpdateLayout();
        g_moveEditor->visual.ready = false; g_moveEditor->candidate.width = 430;
        RefreshMovePreviewVisual(); RenderMovePreview(); SaveNativePreview(preview, argv[1], L"move-large-font.png");
        for (int index : {0, 1, 2, 6, 7, 8}) {
            const auto& text = g_moveEditor->visual.texts[index]; auto font = MovePreviewFont(text);
            Gdiplus::Graphics measure(g_moveEditor->surface->dc); Gdiplus::RectF bounds;
            Gdiplus::StringFormat textFormat(Gdiplus::StringFormat::GenericTypographic());
            measure.MeasureString(text.text.c_str(), static_cast<int>(text.text.size()), font.get(),
                                  Gdiplus::PointF(0, 0), &textFormat, &bounds);
            if (bounds.Width > text.bounds.Width + .1f)
                throw std::runtime_error("Compact compute cells trim maximum values at the largest supported font");
        }
        g_moveEditor->target.scale = 2; RenderMovePreview();
        SaveNativePreview(preview, argv[1], L"move-200pct.png");
        g_moveEditor->target.scale = 1; g_moveEditor->visual.highContrast = true;
        for (auto& text : g_moveEditor->visual.texts) text.color = ColorFromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        for (auto& bar : g_moveEditor->visual.bars) bar.color = ColorFromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        for (auto& graph : g_moveEditor->visual.graphs) graph.color = ColorFromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        g_moveEditor->dirty = true; RenderMovePreview(); SaveNativePreview(preview, argv[1], L"move-high-contrast.png");
        // Export the same editing states for the README using the workstation
        // readings, after the maximum-value and live-update assertions above.
        settings->width = 410; settings->fontSize = 11;
        root.RequestedTheme(ElementTheme::Dark); frame.UpdateLayout(); ApplyWidgetSettings();
        PublishDocumentationSamples(); frame.UpdateLayout(); Pump();
        g_moveEditor->target.scale = 1; g_moveEditor->visual.ready = false;
        g_moveEditor->candidate = {500, 410, 0};
        RefreshMovePreviewVisual(); RenderMovePreview();
        SaveNativePreview(preview, argv[1], L"documentation-move.png");
        g_moveEditor->candidate = {}; g_moveEditor->dirty = true; RenderMovePreview();
        SaveNativePreview(preview, argv[1], L"documentation-move-invalid.png");
        settings->width = 430; settings->fontSize = 13; ApplyWidgetSettings(); frame.UpdateLayout();
        g_moveEditor->candidate = {500, 430, 0}; g_moveEditor->visual.ready = false;
        g_moveEditor->target.scale = 2;
        RefreshMovePreviewVisual(); RenderMovePreview();
        SaveNativePreview(preview, argv[1], L"documentation-move-200pct.png");
        settings->width = 410; settings->fontSize = 11; ApplyWidgetSettings(); frame.UpdateLayout();
        g_moveEditor->candidate = {500, 410, 0}; g_moveEditor->target.scale = 1;
        g_moveEditor->visual.ready = false; RefreshMovePreviewVisual();
        g_moveEditor->visual.highContrast = true;
        for (auto& text : g_moveEditor->visual.texts) text.color = ColorFromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        for (auto& bar : g_moveEditor->visual.bars) bar.color = ColorFromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        for (auto& graph : g_moveEditor->visual.graphs) graph.color = ColorFromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        g_moveEditor->dirty = true; RenderMovePreview();
        SaveNativePreview(preview, argv[1], L"documentation-move-high-contrast.png");
        DestroyWindow(preview); UnregisterClassW(kMoveWindowClass, editorClass.hInstance);
        if (g_widgetHost.Opacity() != opacity.original || g_previewGraphicsToken != 0)
            throw std::runtime_error("Closing the preview lost source opacity or retained its graphics runtime");
        StopMetricsWorker();
        auto historyTime = std::chrono::steady_clock::now();
        g_cpuHistory = {{historyTime, 25}, {historyTime + std::chrono::seconds(1), 26}};
        g_gpuHistory = {{historyTime, 10}, {historyTime + std::chrono::seconds(1), std::nullopt}};
        g_lastRenderedMetricsSequence = 42;
        RemoveWidgetForMoveContext moveRemoval; RemoveWidgetForMove(&moveRemoval);
        if (!moveRemoval.succeeded || g_cpuHistory.size() != 2 || g_gpuHistory.size() != 2 ||
            g_cpuHistory.back().value != 26 || g_gpuHistory.back().value ||
            g_lastRenderedMetricsSequence != 42 || !g_geometryCache.elements.empty())
            throw std::runtime_error("Move teardown lost metric history/sequence or retained XAML cache");
        std::wcout << L"PASS: XAML resize/restore, mapping/cache, margins, reservation rollback, preview renders, history-preserving teardown\n";

    } catch (const hresult_error& error) {
        std::wcerr << L"XAML failure 0x" << std::hex << static_cast<uint32_t>(error.code())
                   << L": " << error.message().c_str() << L"\n";
        result = 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        result = 1;
    }
    g_unloading = true;
    CancelMoveEditor();
    StopMetricsWorker();
    RemoveTaskbarUiContext cleanup;
    RemoveFromCurrentTaskbar(&cleanup);
    StopFontGraphics();
    if (g_fontGraphicsToken || !cleanup.succeeded || g_widget || g_widgetHost || g_timer ||
        g_rootSizeChangedToken.value || g_actualThemeChangedToken.value ||
        g_rootLayoutUpdatedToken.value || g_systemTrayFrame) {
        std::cerr << "XAML cleanup did not release all widget resources\n";
        result = 1;
    }
    if (island) { island.Content(nullptr); island.Close(); island = nullptr; }
    if (window) DestroyWindow(window);
    if (manager) { manager.Close(); manager = nullptr; Pump(); }
    return result;
}
