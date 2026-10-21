// Isolated XAML Island: renders the actual widget without injecting Explorer.
#include <initguid.h>
#define IInspectable NativeIInspectable
#define AsyncStatus NativeAsyncStatus
#include <d2d1_1.h>
#include <windows.graphics.effects.h>
#undef IInspectable
#undef AsyncStatus
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
#define ShowWindow uiStorage::KeepHidden
namespace uiStorage {
BOOL KeepHidden(HWND, int) { return FALSE; }
}
#include "../build-ui-smoke/instrumented-mod.wh.cpp"
#undef Wh_SetStringValue
#undef ShowWindow
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Graphics.Effects.h>
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
    auto layout = CurrentMovePreviewLayout(*g_moveEditor);
    if (!PaintMovePreviewSurface(*g_moveEditor, layout)) throw std::runtime_error("Preview render failed");
    auto& surface = *g_moveEditor->surface;
    Gdiplus::Bitmap native(surface.width, surface.height, surface.width * 4,
                          PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(surface.pixels));
    { Gdiplus::Graphics graphics(dc); graphics.DrawImage(&native, 0, 0); }
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


struct SecondaryTestContext {
    std::shared_ptr<const ModSettings> settings;
    std::shared_ptr<const WidgetPresentation> model;
    std::wstring folder;
    std::string error;
};
bool CompositionBorderChecks(Grid host);
#include "preferred-layout-ui.inc.cpp"

void SecondaryUiChecks(const std::wstring& folder,
                       std::shared_ptr<const ModSettings> settings,
                       std::shared_ptr<const WidgetPresentation> model) {
    // The primary watchdog enumerates real taskbar windows. During bitmap
    // export Pump() can dispatch it and remove these synthetic controllers.
    // This fixture exercises the secondary timers/queues on their own threads;
    // coordinator discovery is exercised separately below.
    struct ResumePrimaryTimer {
        DispatcherTimer timer;
        ~ResumePrimaryTimer() { if (timer) timer.Start(); }
    } resume{g_timer};
    if (resume.timer) resume.timer.Stop();
    struct Context : SecondaryTestContext { HWND window = nullptr; } context;
    context.settings = settings; context.model = model; context.folder = folder;
    std::atomic<bool> ready{false};
    std::thread owner([&] {
        try {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
            auto manager = Windows::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
            HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0,0,1800,48,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            auto island = Windows::UI::Xaml::Hosting::DesktopWindowXamlSource();
            auto native = island.as<IDesktopWindowXamlSourceNative>();
            check_hresult(native->AttachToWindow(window));
            HWND child = nullptr; check_hresult(native->get_WindowHandle(&child));
            SetWindowPos(child,nullptr,0,0,1800,48,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
            Grid root; root.Width(1800); root.Height(48);
            root.Name(L"RootGrid"); root.RequestedTheme(ElementTheme::Light);
            root.Background(SolidColorBrush(Color{255,242,242,242}));
            Grid frame; frame.Children().Append(root);
            island.Content(frame); SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&root));
            context.window=window; ready=true;
            MSG message;
            while (GetMessageW(&message,nullptr,0,0)>0) {
                if (message.message == WM_APP + 73) break;
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            RemoveWidgetCopy(window);
            island.Content(nullptr); island.Close(); DestroyWindow(window);
            manager.Close(); Pump();
        } catch (const winrt::hresult_error& e) {
            context.error="secondary thread XAML failure " + std::to_string(static_cast<unsigned>(e.code())); ready=true;
        } catch (const std::exception& e) { context.error=e.what(); ready=true; }
    });
    while (!ready) { Pump(); Sleep(1); }
    if (context.window && context.error.empty()) {
        auto check = [](void* value) {
            auto& test = *static_cast<Context*>(value);
            std::string stage="root";
            try {
                auto& root = *reinterpret_cast<Grid*>(GetWindowLongPtrW(test.window,GWLP_USERDATA));
                auto settle = [&] { root.Measure(Size{1800,48}); root.Arrange(Rect{0,0,1800,48}); root.UpdateLayout(); Pump(); };
                settle();
                stage="attach";
                auto* copy=AttachSecondaryWidget(test.window,root);
                if (!copy) throw std::runtime_error("secondary attach failed");
                copy->settings=test.settings; copy->model=test.model;
                stage="update"; UpdateSecondaryWidget(*copy,true); stage="settle"; settle();
                if (copy->view.host.Name()!=kWidgetName || copy->view.host.Visibility()!=Visibility::Visible)
                    throw std::runtime_error("secondary public selector/visibility lost");
                auto color=copy->palette[0].Color();
                if (color.R!=0 || color.G!=95 || color.B!=184)
                    throw std::runtime_error("secondary light theme inherited primary dark colors");
                if (copy->view.texts[1].Text()!=FormatPercent(test.model->metrics.cpu))
                    throw std::runtime_error("secondary values differ from shared snapshot");
                for (bool bits : {false, true, false}) {
                    auto units = std::make_shared<ModSettings>(*test.settings);
                    units->networkBits = bits;
                    copy->settings = units; copy->styled = false;
                    UpdateSecondaryWidget(*copy, true); settle();
                    if (copy->view.texts[13].Text() != (bits ? L"1.0 Gbps" : L"119 MB/s") ||
                        copy->view.texts[15].Text() != (bits ? L"12 Kbps" : L"1.5 KB/s") ||
                        copy->view.netRows[0].Visibility() != Visibility::Visible ||
                        copy->view.netRows[1].Visibility() != Visibility::Visible)
                        throw std::runtime_error("secondary network values or unit switching lost");
                }
                SecondaryPreferenceChecks(*copy, root, test.settings);
                // Exercise the shared high-contrast styling contract without
                // changing the user's Windows accessibility settings.
                std::array<SolidColorBrush, 3> contrastPalette{
                    SolidColorBrush(Color{255,255,255,255}),
                    SolidColorBrush(Color{255,255,255,0}),
                    SolidColorBrush(Color{255,0,255,255})};
                StyleWidgetElements(copy->view, *test.settings, contrastPalette, nullptr, true);
                RenderWidgetValues(copy->view, test.model->metrics, *test.settings,
                    {AlertLevel::Warning, AlertLevel::Critical, AlertLevel::Warning, AlertLevel::Critical},
                    contrastPalette, nullptr);
                for (auto text : copy->view.texts)
                    if (text.Opacity() != 1) throw std::runtime_error("high contrast text is translucent");
                if (copy->view.graphs[0].Opacity() != 1 ||
                    std::abs(copy->view.bars[0].Opacity() - .45) > .0001 || copy->view.bars[1].Opacity() != 1)
                    throw std::runtime_error("high contrast graph or bar opacity lost");
                if (copy->view.graphs[0].Stroke() != contrastPalette[0] ||
                    copy->view.texts[2].Foreground() != contrastPalette[1] ||
                    copy->view.texts[8].Foreground() != contrastPalette[2] ||
                    copy->view.bars[1].Fill() != contrastPalette[1] ||
                    copy->view.bars[3].Fill() != contrastPalette[2])
                    throw std::runtime_error("high contrast palette or alert rendering lost");
                copy->settings = test.settings; copy->styled = false;
                UpdateSecondaryWidget(*copy, true); settle();
                // Repeat Styler's captured-value restoration on the secondary
                // owner thread, then reapply column-only placement.
                auto originalWidth = copy->view.host.Width();
                auto originalHeight = copy->view.host.Height();
                auto originalMargin = copy->view.host.Margin();
                ColumnDefinition leftColumn, rightColumn;
                leftColumn.Width({400, GridUnitType::Pixel});
                rightColumn.Width({1, GridUnitType::Star});
                root.ColumnDefinitions().Append(leftColumn);
                root.ColumnDefinitions().Append(rightColumn);
                Grid::SetColumn(copy->view.host, 1); Grid::SetColumnSpan(copy->view.host, 1);
                copy->view.host.Width(NAN); copy->view.host.Height(NAN);
                UpdateSecondaryWidget(*copy, false); settle();
                Grid::SetColumn(copy->view.host, 0); Grid::SetColumnSpan(copy->view.host, 2);
                copy->view.host.Width(originalWidth); copy->view.host.Height(originalHeight);
                copy->view.host.Margin(originalMargin);
                UpdateSecondaryWidget(*copy, false); settle();
                Grid::SetColumn(copy->view.host, 1); Grid::SetColumnSpan(copy->view.host, 1);
                UpdateSecondaryWidget(*copy, false); settle();
                if (!std::isnan(copy->view.host.Width()) || !std::isnan(copy->view.host.Height()) ||
                    copy->view.host.Margin() != Thickness{})
                    throw std::runtime_error("Secondary reapply retains restored automatic geometry");
                if (!CompositionBorderChecks(copy->view.host))
                    throw std::runtime_error("Secondary render-only border transition failed");
                Grid::SetColumn(copy->view.host, 0); Grid::SetColumnSpan(copy->view.host, 1);
                root.ColumnDefinitions().Clear();
                UpdateSecondaryWidget(*copy, false); settle();
                // Bitmap export is tested on the primary XAML thread below.
                stage="obstacle";
                Button obstacle; obstacle.Width(650); obstacle.Height(48);
                obstacle.HorizontalAlignment(HorizontalAlignment::Left); root.Children().Append(obstacle);
                // Only layout events and the owning window queue may relocate it.
                for(int i=0;i<4;++i) settle();
                auto geometry=MeasureTaskbarGeometry(root,nullptr);
                if (!PlacementFits(geometry,{copy->view.host.Margin().Left,copy->view.host.Width(),0}))
                    throw std::runtime_error("secondary overlaps new button until metric refresh");
                root.RequestedTheme(ElementTheme::Dark); root.Background(SolidColorBrush(Color{255,32,32,32}));
                for(int i=0;i<4;++i) settle();
                if (copy->palette[0].Color().R!=120) throw std::runtime_error("secondary theme event not applied");

                auto stale=std::make_shared<WidgetPresentation>(*test.model);
                stale->metrics.capturedAt=SampleTime::clock::now()-std::chrono::minutes(1);
                copy->model=stale; UpdateSecondaryWidget(*copy,true);
                if(copy->view.texts[1].Text()!=L"--%" || copy->view.graphs[0].Visibility()!=Visibility::Collapsed)
                    throw std::runtime_error("secondary retains stale metrics");
                auto removedHost = copy->view.host;
                // A transient revoke failure must leave the host/controller
                // reachable and report failure; the owner-thread retry must
                // revoke only the token that is still outstanding.
                g_borderRevokeTestHook = [](DependencyProperty const& property) {
                    if (property == Grid::BorderBrushProperty()) {
                        g_borderRevokeTestHook = nullptr;
                        throw hresult_error(E_FAIL);
                    }
                };
                SyncCopyContext failedRemoval{test.window, {}, {}, true};
                SyncCopyOnOwner(&failedRemoval);
                g_borderRevokeTestHook = nullptr;
                uint32_t retainedIndex = 0;
                if (failedRemoval.succeeded || FindWidgetCopy(test.window) != copy ||
                    !copy->cleanupPending || !copy->borderSubscriptions.brush ||
                    copy->borderSubscriptions.radius ||
                    !root.Children().IndexOf(removedHost, retainedIndex))
                    throw std::runtime_error("Failed secondary revoke discarded retry state");
                if (copy->timerRegistered || copy->subclassRegistered)
                    throw std::runtime_error("Failed XAML revoke retained native timer/subclass");
                auto oldModel = copy->model;
                SendMessageW(test.window, WM_TIMER, kCopySubclass, 0);
                if (copy->model != oldModel)
                    throw std::runtime_error("Stopped copy received a queued timer callback");
                SyncCopyContext retriedRemoval{test.window, {}, {}, true};
                // Pause at the property-removal boundary while an engine thread
                // retires the now-propertyless HWND, as mod unload can do.
                g_copyPropertyRemovedTestHook = [](HWND window) {
                    std::thread engine([window] { RetireDeadCopyWindow(window); });
                    engine.join();
                };
                SyncCopyOnOwner(&retriedRemoval);
                g_copyPropertyRemovedTestHook = nullptr;
                if (!g_pendingCopyCleanup.empty()) {
                    // The broken order queues the just-deleted controller. Do
                    // not dereference it while reporting the regression.
                    g_pendingCopyCleanup.clear();
                    throw std::runtime_error("Unload queued a deleted secondary controller");
                }
                if (!retriedRemoval.succeeded)
                    throw std::runtime_error("Secondary callback revoke retry failed");
                std::wcout << L"PASS: concurrent unload cannot queue a removed controller\n";
                std::wcout << L"PASS: secondary callback revoke failure retains state and retries\n";
                if (FindWidgetCopy(test.window) || root.Children().Size()!=1)
                    throw std::runtime_error("secondary detach leaked host or subclass");
                auto removedLayer = removedHost.Children().GetAt(1).as<Canvas>();
                auto removedBorder = removedLayer.Children().GetAt(0).as<Border>();
                SolidColorBrush sentinel(Color{255, 1, 2, 3});
                removedBorder.BorderBrush(sentinel); removedLayer.Visibility(Visibility::Visible);
                removedHost.CornerRadius({3, 3, 3, 3}); Pump();
                if (removedBorder.BorderBrush() != sentinel || removedLayer.Visibility() != Visibility::Visible)
                    throw std::runtime_error("Detached secondary host retained a border callback");
                g_secondaryAttachTestHook = [] {
                    g_secondaryAttachTestHook = nullptr;
                    throw hresult_error(E_FAIL);
                };
                g_borderRevokeTestHook = [](DependencyProperty const& property) {
                    if (property == Grid::BorderBrushProperty()) {
                        g_borderRevokeTestHook = nullptr;
                        throw hresult_error(E_FAIL);
                    }
                };
                bool attachFailed = false;
                try { AttachSecondaryWidget(test.window, root); }
                catch (const hresult_error&) { attachFailed = true; }
                copy = FindWidgetCopy(test.window);
                if (!attachFailed || !copy || !copy->cleanupPending ||
                    !copy->borderSubscriptions.brush || copy->borderSubscriptions.radius)
                    throw std::runtime_error("Partial secondary attach discarded cleanup state");
                {
                    auto savedProbe = g_testFindTaskbarFrame;
                    struct RestoreProbe {
                        decltype(g_testFindTaskbarFrame) saved;
                        ~RestoreProbe() { g_testFindTaskbarFrame = saved; }
                    } restore{savedProbe};
                    g_testFindTaskbarFrame = [](HWND window) -> FrameworkElement {
                        auto& grid = *reinterpret_cast<Grid*>(GetWindowLongPtrW(window, GWLP_USERDATA));
                        return VisualTreeHelper::GetParent(grid).as<FrameworkElement>();
                    };
                    SyncCopyContext recover{test.window, test.settings, test.model};
                    SyncCopyOnOwner(&recover);
                    copy = FindWidgetCopy(test.window);
                    if (!recover.succeeded || !copy || copy->cleanupPending ||
                        !copy->borderSubscriptions.brush || !copy->borderSubscriptions.radius ||
                        !copy->layoutToken.value || !copy->themeToken.value)
                        throw std::runtime_error("Partial secondary attach was reused without complete setup");
                }
                std::wcout << L"PASS: partial secondary attach cleans up and recreates subscriptions\n";
                copy->settings=test.settings; copy->model=test.model;
                UpdateSecondaryWidget(*copy,true);
                RemoveWidgetCopyFromRoot(root);
                if (FindWidgetCopy(test.window) || root.Children().Size()!=1)
                    throw std::runtime_error("Promoting a taskbar retained its secondary widget");
                copy=AttachSecondaryWidget(test.window,root);
                if (!copy) throw std::runtime_error("Secondary reattach after promotion failed");
                auto disabled=std::make_shared<ModSettings>(*test.settings); disabled->allMonitors=false;
                SyncCopyContext disable{test.window,disabled,test.model}; SyncCopyOnOwner(&disable);
                if (!disable.succeeded || FindWidgetCopy(test.window))
                    throw std::runtime_error("Disabling copies retained native registration");
                // A native destroy has one last synchronous owner-thread retry.
                HWND destroyed = CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
                    0, 0, 600, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
                Grid destroyedRoot;
                if (!destroyed || !AttachSecondaryWidget(destroyed, destroyedRoot))
                    throw std::runtime_error("Destroy cleanup fixture attach failed");
                g_borderRevokeTestHook = [](DependencyProperty const& property) {
                    if (property == Grid::BorderBrushProperty()) {
                        g_borderRevokeTestHook = nullptr;
                        throw hresult_error(E_FAIL);
                    }
                };
                DestroyWindow(destroyed);
                auto remainingCopies = CopyWindows();
                if (g_borderRevokeTestHook || destroyedRoot.Children().Size() ||
                    std::find(remainingCopies.begin(), remainingCopies.end(), destroyed) != remainingCopies.end())
                    throw std::runtime_error("Native destroy lost pending secondary cleanup");
                std::wcout << L"PASS: native destroy retries transient callback revocation\n";
                // A failure lasting through native destruction retains its XAML
                // controller on the owner thread, independently of the dead HWND.
                destroyed = CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
                    0, 0, 600, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
                Grid deferredRoot;
                AttachSecondaryWidget(destroyed, deferredRoot);
                g_borderRevokeTestHook = [](DependencyProperty const&) { throw hresult_error(E_FAIL); };
                g_copyPropertyRemovedTestHook = [](HWND window) {
                    std::thread engine([window] { RetireDeadCopyWindow(window); });
                    engine.join();
                };
                DestroyWindow(destroyed);
                g_copyPropertyRemovedTestHook = nullptr;
                auto deadCopies = CopyWindows();
                if (g_pendingCopyCleanup.size() != 1 ||
                    std::find(deadCopies.begin(), deadCopies.end(), destroyed) != deadCopies.end())
                    throw std::runtime_error("Concurrent destroy did not retain exactly one cleanup owner");
                g_borderRevokeTestHook = nullptr;
                if (!RetryPendingCopyCleanup() || !g_pendingCopyCleanup.empty() || deferredRoot.Children().Size())
                    throw std::runtime_error("Deferred destroyed copy did not finish XAML cleanup");
                std::wcout << L"PASS: concurrent unload and native destroy retain one cleanup owner\n";

                // The subclass can already be removed when the HWND dies.
                destroyed = CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
                    0, 0, 600, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
                Grid detachedRoot;
                AttachSecondaryWidget(destroyed, detachedRoot);
                g_borderRevokeTestHook = [](DependencyProperty const&) { throw hresult_error(E_FAIL); };
                if (RemoveWidgetCopy(destroyed))
                    throw std::runtime_error("Injected persistent revocation unexpectedly succeeded");
                DestroyWindow(destroyed);
                g_borderRevokeTestHook = nullptr;
                if (!RemoveAllWidgetCopies() || !g_pendingCopyCleanup.empty() || detachedRoot.Children().Size())
                    throw std::runtime_error("Dead HWND without subclass lost the retained controller");
                std::wcout << L"PASS: repeated revoke failures stop native callbacks and recover after HWND destruction\n";
                // Recreate after teardown, then let mod-thread cleanup marshal back.
                copy=AttachSecondaryWidget(test.window,root);
                if (!copy) throw std::runtime_error("Secondary reattach after disable failed");
                copy->settings=test.settings; copy->model=test.model;
                UpdateSecondaryWidget(*copy,true); settle();
            } catch(const std::exception& e) { test.error=e.what(); }
              catch(const winrt::hresult_error& e) { test.error=stage + " secondary test HRESULT " + std::to_string(static_cast<unsigned>(e.code())); }
        };
        if(!RunFromWindowThread(context.window,check,&context)) context.error="secondary owner dispatch failed";
        if(!RemoveAllWidgetCopies() || !CopyWindows().empty()) context.error="cross-thread secondary cleanup failed";
        PostThreadMessageW(GetWindowThreadProcessId(context.window,nullptr),WM_APP+73,0,0);
    }
    owner.join();
    if(!context.error.empty()) throw std::runtime_error(context.error);

    // RenderTargetBitmap export is not supported by the secondary-thread
    // fixture on all Windows builds. Render the same instance on this thread.
    HWND shotWindow=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,1000,48,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    auto shotIsland=Windows::UI::Xaml::Hosting::DesktopWindowXamlSource();
    auto shotNative=shotIsland.as<IDesktopWindowXamlSourceNative>();
    check_hresult(shotNative->AttachToWindow(shotWindow));
    HWND shotChild=nullptr; check_hresult(shotNative->get_WindowHandle(&shotChild));
    SetWindowPos(shotChild,nullptr,0,0,1000,48,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    Grid shotRoot; shotRoot.Width(1000); shotRoot.Height(48); shotIsland.Content(shotRoot);
    auto* shot=AttachSecondaryWidget(shotWindow,shotRoot); shot->settings=settings; shot->model=model;
    for(bool light : {true,false}) {
        if (FindWidgetCopy(shotWindow) != shot)
            throw std::runtime_error("Synthetic secondary was removed during bitmap export");
        shotRoot.RequestedTheme(light ? ElementTheme::Light : ElementTheme::Dark);
        shotRoot.Background(SolidColorBrush(light ? Color{255,242,242,242} : Color{255,32,32,32}));
        shotRoot.Measure(Size{1000,48}); shotRoot.Arrange(Rect{0,0,1000,48}); shotRoot.UpdateLayout(); Pump();
        UpdateSecondaryWidget(*shot,true); shotRoot.UpdateLayout(); Pump();
        SaveImage(shotRoot,folder,light ? L"secondary-light.png" : L"secondary-dark.png");
    }
    DestroyWindow(shotWindow);
    if (!CopyWindows().empty()) throw std::runtime_error("Destroyed secondary window retained registry entry");
    shotIsland.Content(nullptr); shotIsland.Close();
    std::wcout << L"PASS: secondary XAML on another UI thread, local themes, event placement, stale data and teardown\n";
}

#include "coordinator-smoke.inc.cpp"
#include "composition-border.inc.cpp"
#include "styler-layout.inc.cpp"

int wmain(int argc, wchar_t** argv) {
    std::wcout << std::unitbuf;
    if (argc != 2) return 2;
    init_apartment(apartment_type::single_threaded);
    Windows::UI::Xaml::Hosting::WindowsXamlManager manager{nullptr};
    Windows::UI::Xaml::Hosting::DesktopWindowXamlSource island{nullptr};
    HWND window = nullptr;
    int result = 0;
    try {
        manager = Windows::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        TaskbarProjection missingRoot;
        ProbeTaskbarFrameGeometry(missingRoot, Grid{});
        if (missingRoot.geometry.ready)
            throw std::runtime_error("A taskbar frame without RootGrid must remain unavailable");
        Grid detachedFrame, detachedRoot;
        detachedRoot.Name(L"RootGrid");
        detachedFrame.Width(1920); detachedFrame.Height(48);
        detachedFrame.Children().Append(detachedRoot);
        detachedFrame.Measure(Size{1920, 48});
        detachedFrame.Arrange(Rect{0, 0, 1920, 48});
        if (!MeasureTaskbarGeometry(detachedRoot, nullptr, 0).ready)
            throw std::runtime_error("Detached-root fixture must expose measurable bounds");
        TaskbarProjection detached;
        ProbeTaskbarFrameGeometry(detached, detachedFrame);
        if (detached.geometry.ready)
            throw std::runtime_error("Measurable bounds without XamlRoot are not a ready screen projection");
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
        // A dock measures to its buttons, unlike the fixed-width fixtures below.
        // Reserving space must grow this auto-sized frame from a cold injection.
        {
            if (!RemoveWidget()) throw std::runtime_error("Dock setup cleanup failed");
            Grid scene, dockFrame, dockRoot, dockButtons;
            int reviewFailures = 0;
            scene.Width(1200); scene.Height(48);
            scene.Background(SolidColorBrush(Color{255, 20, 20, 20}));
            scene.RequestedTheme(ElementTheme::Dark);
            dockFrame.HorizontalAlignment(HorizontalAlignment::Center);
            dockRoot.Name(L"RootGrid");
            dockRoot.Background(SolidColorBrush(Color{255, 45, 45, 45}));
            dockButtons.Name(L"TaskbarFrameRepeater");
            dockButtons.HorizontalAlignment(HorizontalAlignment::Left);
            Button dockButton; dockButton.Width(300); dockButton.Height(38);
            dockButton.Content(box_value(L"Start / app buttons (test fixture)"));
            dockButtons.Children().Append(dockButton);
            dockRoot.Children().Append(dockButtons);
            dockFrame.Children().Append(dockRoot); scene.Children().Append(dockFrame);
            island.Content(scene);
            SetWindowPos(child, nullptr, 0, 0, 1200, 48, SWP_NOZORDER | SWP_NOACTIVATE);
            auto dockSettings = std::make_shared<ModSettings>(*initialSettings);
            dockSettings->reserveSpace = true; dockSettings->leftOffset = 10;
            g_settings = dockSettings;
            auto settleDock = [&] {
                for (int i = 0; i < 12; ++i) {
                    scene.Measure(Size{1200, 48}); scene.Arrange(Rect{0, 0, 1200, 48});
                    scene.UpdateLayout(); Pump(); ApplyTaskbarPlacement(*dockSettings);
                }
            };
            settleDock();
            if (!InjectWidget(dockFrame)) throw std::runtime_error("Dock injection failed");
            settleDock();
            std::wcout << L"DOCK root=" << dockRoot.ActualWidth() << L" widget="
                       << g_widgetHost.Width() << L" reserve=" << g_reservedMargin << L"\n";
            SaveImage(scene, argv[1], L"dock-reservation.png");
            if (g_widgetHost.Visibility() != Visibility::Visible || g_widgetHost.ActualWidth() <= 0 ||
                !PlacementFits(MeasureTaskbarGeometry(dockRoot, dockButtons),
                               {g_widgetHost.Margin().Left, g_widgetHost.Width(), 0}))
                throw std::runtime_error("Auto-sized dock cannot reserve space from a cold injection");
            if (!g_widgetHost.try_as<Grid>())
                throw std::runtime_error("Grid#WindhawkTaskbarSystemInfo theme selector no longer matches");
            // A saved right-side position must not select the trailing gap
            // created by our own reservation and repeatedly shrink the dock.
            auto oldProfiles = PlacementProfilesSnapshot();
            auto oldTaskbarWindow = g_taskbarWindow.load();
            g_taskbarWindow = window;
            auto dockKey = MonitorKey(window);
            if (dockKey.empty()) throw std::runtime_error("Missing fixture monitor key");
            dockButton.Width(600);
            for (double fraction : {.6, .75, .9}) {
                PlacementProfiles profiles;
                profiles.positions[dockKey] = fraction;
                SetPlacementProfiles(profiles);
                settleDock();
                auto revision = g_layoutRevision;
                double reserved = g_reservedMargin;
                settleDock();
                if (g_layoutRevision != revision || reserved <= 0 ||
                    std::abs(reserved - g_reservedMargin) > .1 ||
                    g_widgetHost.Visibility() != Visibility::Visible)
                    throw std::runtime_error("Saved right-side dock position oscillates after reservation");
                TaskbarProjection projection;
                projection.window = window; projection.key = dockKey;
                ProbeTaskbarFrameGeometry(projection, dockFrame);
                if (!projection.geometry.ready || !projection.reservationOnly)
                    throw std::runtime_error("Grown dock projection lost its reservation constraint");
                g_moveEditor = std::make_shared<MoveEditorState>();
                // Exercise the real candidate search without creating a native
                // preview window; closing suppresses only the final render.
                g_moveEditor->closing = true;
                g_moveEditor->visual.fontMetrics = MeasureWidgetFont(*dockSettings);
                UpdateMoveCandidate(projection, 900);
                if (g_moveEditor->candidate.width <= 0 ||
                    g_moveEditor->candidate.reserved <= 0)
                    throw std::runtime_error("Move editor offered synthetic trailing dock gap");
                g_moveEditor.reset();
            }
            for (bool fixedWidth : {false, true}) {
                if (!RemoveWidget()) throw std::runtime_error("Theme-switch setup failed");
                settleDock();
                if (!InjectWidget(dockFrame)) throw std::runtime_error("Theme-switch dock injection failed");
                settleDock();
                if (!g_dockReservationActive)
                    throw std::runtime_error("Theme-switch test requires an active dock reservation");
                if (fixedWidth) dockFrame.Width(1200);
                else dockFrame.HorizontalAlignment(HorizontalAlignment::Stretch);
                settleDock();
                auto expected = ResolveWidgetPlacement(*dockSettings,
                    MeasureTaskbarGeometry(dockRoot, dockButtons, g_reservedMargin),
                    {10, g_previousPlacementLeft.load(), .9}, MeasureWidgetFont(*dockSettings));
                if (g_dockReservationActive || g_reservedMargin != 0 ||
                    g_widgetHost.Visibility() != Visibility::Visible ||
                    std::abs(g_widgetHost.Margin().Left - expected.placement.left) > .25)
                    throw std::runtime_error("Dock-to-normal theme switch retains reservation-only placement");
                TaskbarProjection projection; projection.window = window;
                ProbeTaskbarFrameGeometry(projection, dockFrame);
                if (projection.reservationOnly)
                    throw std::runtime_error("Normal taskbar move projection retained dock restriction");
                dockFrame.Width(std::numeric_limits<double>::quiet_NaN());
                dockFrame.HorizontalAlignment(HorizontalAlignment::Center);
                // Allow the visible widget's adaptive width to converge before
                // asserting that subsequent passes no longer change layout.
                for (int pass = 0; pass < 4; ++pass) settleDock();
                auto revision = g_layoutRevision;
                settleDock();
                if (g_layoutRevision != revision ||
                    g_widgetHost.Visibility() != Visibility::Visible ||
                    !PlacementFits(MeasureTaskbarGeometry(dockRoot, dockButtons),
                        {g_widgetHost.Margin().Left, g_widgetHost.Width(), 0}))
                    throw std::runtime_error("Switching back to dock did not settle at a valid position");
            }
            // Return to a cold dock for the remaining reservation regressions.
            if (!RemoveWidget()) throw std::runtime_error("Theme-switch cleanup failed");
            SetPlacementProfiles(oldProfiles);
            g_taskbarWindow = oldTaskbarWindow;
            dockButton.Width(300); settleDock();
            if (!InjectWidget(dockFrame)) throw std::runtime_error("Theme-switch reinjection failed");
            settleDock();
            double dockWidth = dockRoot.ActualWidth(), dockMargin = dockButtons.Margin().Left;
            settleDock();
            if (std::abs(dockRoot.ActualWidth() - dockWidth) > .1 ||
                std::abs(dockButtons.Margin().Left - dockMargin) > .1)
                throw std::runtime_error("Auto-sized dock does not settle");
            scene.Width(400); settleDock();
            if (g_widgetHost.Visibility() != Visibility::Collapsed || g_reservedMargin != 0)
                throw std::runtime_error("Shrinking an already expanded dock retains clipped content");
            for (bool changeUnits : {false, true}) {
                g_reservationRejected = true;
                if (changeUnits) dockSettings->networkBits = !dockSettings->networkBits;
                else dockSettings->showNetwork = !dockSettings->showNetwork;
                ApplyTaskbarPlacement(*dockSettings);
                if (g_reservationRejected)
                    throw std::runtime_error("Network setting did not invalidate rejected reservation");
                settleDock();
            }
            dockSettings->showNetwork = false; dockSettings->networkBits = false;
            for (int setting = 0; setting < 4; ++setting) {
                g_reservationRejected = true;
                if (setting == 0) dockSettings->preferredLayout = WidgetLayoutMode::NoGraphs;
                if (setting == 1) dockSettings->temperatureSource = TemperatureSource::Disabled;
                if (setting == 2) dockSettings->showCpuGraph = false;
                if (setting == 3) dockSettings->showGpuGraph = false;
                ApplyTaskbarPlacement(*dockSettings);
                if (g_reservationRejected)
                    throw std::runtime_error("Content setting did not invalidate rejected reservation");
                settleDock();
            }
            dockSettings->preferredLayout = WidgetLayoutMode::Full;
            dockSettings->temperatureSource = TemperatureSource::Auto;
            dockSettings->showCpuGraph = dockSettings->showGpuGraph = true;
            scene.Width(1200); settleDock();
            if (g_widgetHost.Visibility() != Visibility::Visible)
                throw std::runtime_error("An expanded dock does not recover after shrink/grow");
            double fullReservation = g_reservedMargin;
            dockSettings->preferredLayout = WidgetLayoutMode::NoGraphs;
            dockSettings->temperatureSource = TemperatureSource::Disabled;
            settleDock();
            if (g_widgetHost.Visibility() != Visibility::Visible ||
                g_reservedMargin <= 0 || g_reservedMargin >= fullReservation - 1)
                throw std::runtime_error("Compact content does not release actual reserved dock space");
            dockSettings->preferredLayout = WidgetLayoutMode::Full;
            dockSettings->temperatureSource = TemperatureSource::Auto;
            settleDock();
            // DockLike uses padding; other presets add a border as well.
            dockRoot.Padding(Thickness{6, 2, 6, 2});
            dockRoot.BorderThickness(Thickness{1, 1, 1, 1});
            settleDock();
            auto widgetBounds = g_widgetHost.TransformToVisual(dockRoot).TransformBounds(
                {0, 0, static_cast<float>(g_widgetHost.ActualWidth()), static_cast<float>(g_widgetHost.ActualHeight())});
            auto buttonBounds = dockButton.TransformToVisual(dockRoot).TransformBounds(
                {0, 0, static_cast<float>(dockButton.ActualWidth()), static_cast<float>(dockButton.ActualHeight())});
            if (g_widgetHost.Visibility() != Visibility::Visible ||
                widgetBounds.X + widgetBounds.Width + kTaskbarClearance > buttonBounds.X + .25 ||
                widgetBounds.Y < 3 - .25 || widgetBounds.Y + widgetBounds.Height > 45 + .25)
                throw std::runtime_error("Dock padding/border breaks the actual widget clearance");
            dockRoot.Padding(Thickness{}); dockRoot.BorderThickness(Thickness{});
            dockSettings->reserveSpace = false; settleDock();
            if (dockButtons.Margin().Left != 0 || g_dockReservationActive)
                throw std::runtime_error("Disabling dock reservation retained its margin");
            dockSettings->reserveSpace = true; settleDock();
            if (g_widgetHost.Visibility() != Visibility::Visible)
                throw std::runtime_error("Re-enabling dock reservation did not restore the widget");
            if (!RemoveWidget()) throw std::runtime_error("Dock cleanup failed");
            settleDock();
            if (std::abs(dockRoot.ActualWidth() - 300) > .1 || dockButtons.Margin().Left != 0)
                throw std::runtime_error("Dock unload did not restore its original size and margin");
            // The parent's constraint is intentionally smaller than XamlRoot:
            // a proposed expansion must be rejected against arranged controls.
            scene.Width(400);
            if (!InjectWidget(dockFrame)) throw std::runtime_error("Constrained dock injection failed");
            settleDock();
            if (g_widgetHost.Visibility() != Visibility::Collapsed || g_reservedMargin != 0 ||
                dockButtons.Margin().Left != 0 || g_dockReservationTrial)
                throw std::runtime_error("Rejected dock growth did not roll back");
            auto stableRevision = g_layoutRevision;
            settleDock();
            if (g_layoutRevision != stableRevision)
                throw std::runtime_error("Rejected dock growth keeps retrying layout");
            scene.Width(1200); settleDock();
            if (g_widgetHost.Visibility() != Visibility::Visible || g_reservedMargin <= 0)
                throw std::runtime_error("Growing the parent did not retry a hidden dock widget");
            if (!RemoveWidget()) throw std::runtime_error("Constrained dock cleanup failed");
            settleDock();
            if (!InjectWidget(dockFrame) || !g_dockReservationTrial)
                throw std::runtime_error("Expected a pending cold dock reservation");
            // A theme may override the requested margin before arrangement.
            dockButtons.Margin(Thickness{}); settleDock();
            if (!g_reservationRejected || g_reservedMargin != 0 ||
                g_widgetHost.Visibility() != Visibility::Collapsed)
                throw std::runtime_error("Theme-rejected dock reservation was not rolled back");
            stableRevision = g_layoutRevision; settleDock();
            if (g_layoutRevision != stableRevision)
                throw std::runtime_error("Theme-rejected dock reservation entered a feedback loop");
            dockSettings->reserveGap = 6; settleDock();
            if (g_widgetHost.Visibility() != Visibility::Visible) {
                std::wcout << L"FAIL: changed placement settings do not retry a rejected dock\n";
                ++reviewFailures;
            }
            dockSettings->reserveGap = 8;
            if (!RemoveWidget()) throw std::runtime_error("Rejected dock cleanup failed");
            Grid dockTray;
            dockTray.Name(L"SystemTrayFrame"); dockTray.Width(120); dockTray.Height(48);
            dockTray.HorizontalAlignment(HorizontalAlignment::Right);
            scene.Children().Append(dockTray);
            dockRoot.Padding(Thickness{6, 2, 6, 2});
            dockRoot.BorderThickness(Thickness{1, 1, 1, 1});
            settleDock();
            if (!InjectWidget(dockFrame)) throw std::runtime_error("Padded dock injection failed");
            settleDock();
            auto dockGeometry = MeasureTaskbarGeometry(dockRoot, dockButtons);
            if (g_widgetHost.Visibility() != Visibility::Visible || !ReservedControlsFit(dockGeometry) ||
                !PlacementFits(dockGeometry, {g_widgetHost.Margin().Left, g_widgetHost.Width(), 0}))
                throw std::runtime_error("Cold padded dock with a separate tray failed");
            TaskbarProjection dockProjection; dockProjection.window = window;
            ProbeTaskbarFrameGeometry(dockProjection, dockFrame);
            POINT sceneOrigin{}; ClientToScreen(window, &sceneOrigin);
            auto actualOrigin = g_widgetHost.TransformToVisual(scene).TransformPoint({0, 0});
            double projectedX = dockProjection.origin.x + g_widgetHost.Margin().Left * dockProjection.scale;
            double actualX = sceneOrigin.x + actualOrigin.X * dockProjection.scale;
            if (!dockProjection.geometry.ready || std::abs(projectedX - actualX) > 1)
                throw std::runtime_error("Padded dock move projection differs from the rendered widget");
            SaveImage(scene, argv[1], L"dock-padding-and-tray.png");
            if (!RemoveWidget()) throw std::runtime_error("Padded dock cleanup failed");
            scene.Children().RemoveAtEnd();
            dockRoot.Padding(Thickness{}); dockRoot.BorderThickness(Thickness{});
            scene.Height(20); dockButton.Height(20);
            dockSettings->fontFamily = L"Verdana"; dockSettings->fontSize = 9; dockSettings->width = 330;
            settleDock();
            if (!InjectWidget(dockFrame)) throw std::runtime_error("Wide one-row dock injection failed");
            settleDock();
            if (g_widgetHost.Visibility() != Visibility::Visible || g_widgetHost.Width() <= 330) {
                std::wcout << L"FAIL: measured one-row dock width is capped by preference\n";
                ++reviewFailures;
            }
            if (!RemoveWidget()) throw std::runtime_error("Wide one-row dock cleanup failed");
            scene.Height(48); dockButton.Height(38);
            *dockSettings = *initialSettings; dockSettings->reserveSpace = true;
            dockFrame.MaxWidth(700); dockFrame.Padding(Thickness{20, 0, 20, 0});
            dockFrame.BorderThickness(Thickness{2, 0, 2, 0});
            dockRoot.Margin(Thickness{40, 0, 40, 0});
            dockRoot.Padding(Thickness{6, 0, 6, 0});
            dockRoot.BorderThickness(Thickness{1, 0, 1, 0});
            settleDock();
            if (!InjectWidget(dockFrame)) throw std::runtime_error("MaxWidth dock injection failed");
            settleDock();
            std::wcout << L"MAXWIDTH available=" << AutoSizedDockAvailableWidth()
                       << L" widget=" << g_widgetHost.Width() << L"\n";
            if (g_widgetHost.Visibility() != Visibility::Visible ||
                std::abs(AutoSizedDockAvailableWidth() - 562) > .1) {
                std::wcout << L"FAIL: frame MaxWidth is not expressed in content coordinates\n";
                ++reviewFailures;
            }
            if (!RemoveWidget()) throw std::runtime_error("MaxWidth dock cleanup failed");
            if (reviewFailures) throw std::runtime_error("Dock review regressions failed");
            std::wcout << L"PASS: cold dock reservation, Grid selector, padding, rollback, resize recovery and unload\n";
            island.Content(frame); g_settings = initialSettings;
            SetWindowPos(child, nullptr, 0, 0, 520, 64, SWP_NOZORDER | SWP_NOACTIVATE);
            if (!InjectWidget(frame)) throw std::runtime_error("Post-dock injection failed");
        }
        StylerLayoutChecks(island, child, argv[1], initialSettings);
        island.Content(frame);
        SetWindowPos(child, nullptr, 0, 0, 520, 64, SWP_NOZORDER | SWP_NOACTIVATE);
        if (!InjectWidget(frame)) throw std::runtime_error("Post-Styler injection failed");
        {
            auto multiple = std::make_shared<ModSettings>(*initialSettings);
            multiple->allMonitors = true; multiple->showNetwork = true; g_settings = multiple;
            ApplyWidgetSettings(); PublishDocumentationSamples();
            std::shared_ptr<const WidgetPresentation> model;
            { std::lock_guard lock(g_presentationMutex); model = g_presentation; }
            if (!model) throw std::runtime_error("Primary did not publish shared presentation");
            auto networkModel = std::make_shared<WidgetPresentation>(*model);
            networkModel->metrics.netAvailable = true;
            networkModel->metrics.netDownBytes = 125000000;
            networkModel->metrics.netUpBytes = 1536;
            model = networkModel;
            SecondaryUiChecks(argv[1], multiple, model);
            CoordinatorUiChecks(argv[1], multiple, model);
            g_settings = initialSettings; ApplyWidgetSettings();
        }
        // Exercise real XAML cells with the optional column in both units,
        // every layout, and after disabling it again.
        {
            auto network = std::make_shared<ModSettings>(*initialSettings);
            network->showNetwork = true; g_settings = network;
            frame.Width(1800); root.Width(1800); frame.Height(48); root.Height(48);
            frame.Measure(Size{1800,48}); frame.Arrange(Rect{0,0,1800,48});
            frame.UpdateLayout(); Pump();
            for (bool bits : {false, true}) {
                network->networkBits = bits; ApplyWidgetSettings();
                MetricsSnapshot sample; sample.capturedAt = SampleTime::clock::now();
                sample.netAvailable = true; sample.netDownBytes = 125000000;
                sample.netUpBytes = 1536; PublishMetrics(sample); UpdateWidgetText(true);
                if (g_netDownText.Text() != (bits ? L"1.0 Gbps" : L"119 MB/s") ||
                    g_netUpText.Text() != (bits ? L"12 Kbps" : L"1.5 KB/s"))
                    throw std::runtime_error("Primary network values or unit switching lost");
                for (auto mode : {WidgetLayoutMode::Full, WidgetLayoutMode::NoGraphs,
                                  WidgetLayoutMode::CompactTwoRows, WidgetLayoutMode::CompactOneRow}) {
                    auto layout = BuildWidgetLayout(*network, MeasureWidgetFont(*network), mode, 48);
                    ApplyWidgetGeometry(*network, layout);
                    g_widgetHost.Width(layout.width); g_widgetHost.Height(layout.height);
                    g_widgetHost.Visibility(Visibility::Visible);
                    g_widget.Measure(Size{static_cast<float>(layout.width), static_cast<float>(layout.height)});
                    g_widget.Arrange(Rect{0, 0, static_cast<float>(layout.width), static_cast<float>(layout.height)});
                    g_widget.UpdateLayout();
                    for (auto text : {g_netDownText, g_netUpText}) {
                        if (text.Text() == L"--" || text.ActualWidth() <= 0 ||
                            text.DesiredSize().Width > Controls::Primitives::LayoutInformation::GetLayoutSlot(text).Width + 1)
                        { std::wcout << L"NETWORK fail mode=" << int(mode) << L" text=" << text.Text().c_str() << L" actual=" << text.ActualWidth() << L" desired=" << text.DesiredSize().Width << L" slot=" << Controls::Primitives::LayoutInformation::GetLayoutSlot(text).Width << L"\n";
                            throw std::runtime_error("Network value is missing or clipped"); }
                    }
                    CaptureWidgetPreviewContext capture; CaptureWidgetPreview(&capture);
                    auto& preview = capture.frame;
                    ArrangeWidgetPreview(preview, layout, *network);
                    if (preview.texts.size() != 16 || preview.texts[13].bounds.Width <= 0 ||
                        preview.texts[15].bounds.Width <= 0 ||
                        preview.texts[13].text != (bits ? L"1.0 Gbps" : L"119 MB/s") ||
                        preview.texts[15].text != (bits ? L"12 Kbps" : L"1.5 KB/s"))
                        throw std::runtime_error("Network preview lost rate cells");
                }
            }
            network->showNetwork = false; ApplyWidgetSettings();
            if (g_netRows[0].Visibility() != Visibility::Collapsed || g_widgetLayout->net != 0)
                throw std::runtime_error("Disabled network column retains space");
            g_settings = initialSettings; ApplyWidgetSettings();
            std::wcout << L"PASS: optional network XAML cells, units, layouts, preview and disable\n";
        }
        PrimaryPreferenceChecks(frame, root, initialSettings, argv[1]);
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
                    auto applies = g_widgetVisualRevision;
                    for (int i = 0; i < 5; ++i) { ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump(); }
                    if (g_widgetVisualRevision != applies || settings->width != 410 || settings->leftOffset != 6)
                        throw std::runtime_error("Stable geometry reapplied its layout or rewrote user preferences");
                    ++adaptiveCases;
                    if (g_widgetHost.Visibility() == Visibility::Collapsed) continue;
                    const auto layout = *g_widgetLayout;
                    // Check real arranged XAML rows, not just the solver's
                    // abstract cells. Missing grid columns can otherwise clip
                    // RAM/VRAM while every calculated bound still fits.
                    // XAML rounds arranged grid tracks to device pixels.
                    constexpr double rowRoundingTolerance = 1.01;
                    for (size_t group = 0; group < g_metricRows.size(); ++group) {
                        auto row = g_metricRows[group];
                        auto bounds = row.TransformToVisual(g_widget).TransformBounds(
                            {0, 0, static_cast<float>(row.ActualWidth()),
                             static_cast<float>(row.ActualHeight())});
                        if (std::abs(row.ActualWidth() - layout.groups[group]) > rowRoundingTolerance ||
                            bounds.X < kWidgetSidePadding - rowRoundingTolerance || bounds.Y < -rowRoundingTolerance ||
                            bounds.X + bounds.Width > layout.width - kWidgetSidePadding + rowRoundingTolerance ||
                            bounds.Y + bounds.Height > layout.height + rowRoundingTolerance) {
                            std::wcerr << L"ROW group=" << group << L" font=" << fontSize
                                << L" gap=" << gap << L" height=" << height
                                << L" mode=" << static_cast<int>(layout.mode)
                                << L" actual=" << bounds.X << L"," << bounds.Y << L","
                                << bounds.Width << L"," << bounds.Height
                                << L" expected-width=" << layout.groups[group]
                                << L" layout=" << layout.width << L"," << layout.height << L"\n";
                            throw std::runtime_error("Arranged XAML metric row is clipped or has the wrong width");
                        }
                    }
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
            auto before = g_widgetVisualRevision;
            ApplyTaskbarPlacement(*wideCompact); frame.UpdateLayout(); Pump();
            if (before != g_widgetVisualRevision) throw std::runtime_error("Wide compact layout repeatedly rebuilds its geometry");
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
            auto before = g_widgetVisualRevision;
            ApplyTaskbarPlacement(*alternate); frame.UpdateLayout(); Pump();
            if (g_widgetVisualRevision != before || measured.widths != MeasureWidgetFont(*alternate).widths)
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
                       << L" available=" << available
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
        auto cachedElements = g_geometryCache->elements.data();
        for (int i = 0; i < 10; ++i) {
            g_cpuUsageText.Text(i % 2 ? L"25%" : L"26%");
            frame.UpdateLayout(); Pump(); ApplyTaskbarPlacement(*settings);
        }
        if (g_geometryCache->elements.data() != cachedElements)
            throw std::runtime_error("Metric text changes rediscovered the entire XAML tree");
        hidden.Height(38); hidden.Visibility(Visibility::Visible);
        frame.UpdateLayout(); Pump();
        if (g_widgetHost.Visibility() != Visibility::Collapsed || g_geometryCache->elements.data() != cachedElements)
            throw std::runtime_error("Cached hidden control did not become an obstacle");
        hidden.Visibility(Visibility::Collapsed); frame.UpdateLayout(); Pump();
        if (g_widgetHost.Visibility() != Visibility::Visible)
            throw std::runtime_error("Hiding a cached obstacle did not restore placement");
        // Replace a child at the same count: stale detached objects must cause
        // rediscovery, not remain in the occupied-area map.
        root.Children().RemoveAtEnd();
        Button replacement; replacement.Width(1920); replacement.Height(38);
        root.Children().Append(replacement); frame.UpdateLayout(); Pump();
        if (g_widgetHost.Visibility() != Visibility::Collapsed || std::none_of(g_geometryCache->elements.begin(), g_geometryCache->elements.end(), [&](const auto& item) { return item.element == replacement; }))
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
        if (!EnsurePlacementClass(editorClass, g_moveClassRegistered))
            throw std::runtime_error("Preview test class registration failed");
        g_moveEditor = std::make_shared<MoveEditorState>();
        HWND preview = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kMoveWindowClass, L"", WS_POPUP, 0, 0, 410, 38,
                                       window, nullptr, editorClass.hInstance, nullptr);
        if (!preview) throw std::runtime_error("Preview render window failed");
        g_moveEditor->window = preview; g_moveEditorWindow = preview; g_moveEditor->source = g_taskbarWindow.load();
        g_moveEditor->target.geometry = {1920, 48, {}, true};
        g_moveEditor->candidate = {500, 410, 0};
        RefreshMovePreviewVisual(); RenderMovePreview();
        if (!g_moveEditor->visual.ready || g_moveEditor->visual.texts.size() != 16 ||
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
                cpuEnd <= 0 || gpuEnd <= 0 || cpuEnd >= g_widgetLayout->graphs[0].Width * .75 ||
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
        auto measuredFamily = g_measuredFontFamily;
        auto measuredSize = g_measuredFontSize;
        auto capacityBudgets = g_capacityBudgets;
        auto fontWidths = g_widgetFontMetrics.widths;
        RemoveWidgetForMoveContext moveRemoval; RemoveWidgetForMove(&moveRemoval);
        if (!moveRemoval.succeeded || g_cpuHistory.size() != 2 || g_gpuHistory.size() != 2 ||
            g_cpuHistory.back().value != 26 || g_gpuHistory.back().value ||
            g_lastRenderedMetricsSequence != 42 || g_geometryCache.has_value())
            throw std::runtime_error("Move teardown lost metric history/sequence or retained XAML cache");
        if (g_measuredFontFamily != measuredFamily || g_measuredFontSize != measuredSize ||
            g_capacityBudgets != capacityBudgets || g_widgetFontMetrics.widths != fontWidths)
            throw std::runtime_error("Move teardown discarded reusable plain font measurements");
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
        g_rootLayoutUpdatedToken.value || g_systemTrayFrame ||
        g_borderSubscriptions.brush || g_borderSubscriptions.radius) {
        std::cerr << "XAML cleanup did not release all widget resources\n";
        result = 1;
    }
    if (island) { island.Content(nullptr); island.Close(); island = nullptr; }
    if (window) DestroyWindow(window);
    if (manager) { manager.Close(); manager = nullptr; Pump(); }
    return result;
}
