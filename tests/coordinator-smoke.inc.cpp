// Included by ui-smoke.cpp after the production implementation and XAML helpers.
// Only the private Explorer root lookup is substituted. Enumeration, owner-thread
// dispatch, reconciliation, subclasses, timers and XAML trees remain real.
void CoordinatorUiChecks(const std::wstring& folder, std::shared_ptr<const ModSettings> settings,
                         std::shared_ptr<const WidgetPresentation> model) {
    auto byteSettings = std::make_shared<ModSettings>(*settings);
    byteSettings->allMonitors = true; byteSettings->showNetwork = true;
    byteSettings->networkBits = false;
    settings = byteSettings;
    auto freshModel = std::make_shared<WidgetPresentation>(*model);
    freshModel->metrics.capturedAt = SampleTime::clock::now();
    freshModel->metrics.netAvailable = true;
    freshModel->metrics.netDownBytes = 125000;
    freshModel->metrics.netUpBytes = 1536;
    model = freshModel;
    struct StateGuard {
        DispatcherTimer timer = g_timer;
        HWND primary = g_taskbarWindow.load();
        Grid widget = g_widget, host = g_widgetHost, root = g_rootGrid;
        std::shared_ptr<const ModSettings> settings = CurrentSettings();
        std::shared_ptr<const WidgetPresentation> model;
        decltype(g_testFindTaskbarFrame) lookup = g_testFindTaskbarFrame;
        StateGuard() {
            if (timer) timer.Stop();
            std::lock_guard lock(g_presentationMutex);
            model = g_presentation;
        }
        ~StateGuard() {
            RemoveAllWidgetCopies();
            g_testFindTaskbarFrame = lookup;
            g_taskbarWindow = primary;
            g_widget = widget; g_widgetHost = host; g_rootGrid = root;
            { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
            { std::lock_guard lock(g_presentationMutex); g_presentation = model; }
            if (timer) timer.Start();
        }
    } saved;
    struct TaskbarFixture {
        HWND window = nullptr;
        Grid frame{nullptr}, root{nullptr};
        Windows::UI::Xaml::Hosting::DesktopWindowXamlSource island{nullptr};
        explicit TaskbarFixture(PCWSTR className) {
            WNDCLASSW cls{};
            cls.lpfnWndProc = DefWindowProcW;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpszClassName = className;
            if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                throw std::runtime_error("Coordinator fixture class registration failed");
            window = CreateWindowExW(0, className, L"", WS_POPUP,
                0, 0, 1800, 48, nullptr, nullptr, cls.hInstance, nullptr);
            if (!window) throw std::runtime_error("Coordinator fixture window failed");
            try {
                frame = Grid(); root = Grid(); root.Name(L"RootGrid");
                frame.Width(1800); frame.Height(48);
                frame.Children().Append(root);
                island = Windows::UI::Xaml::Hosting::DesktopWindowXamlSource();
                auto native = island.as<IDesktopWindowXamlSourceNative>();
                check_hresult(native->AttachToWindow(window));
                HWND child = nullptr;
                check_hresult(native->get_WindowHandle(&child));
                SetWindowPos(child, nullptr, 0, 0, 1800, 48,
                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                island.Content(frame);
                if (!SetPropW(window, L"TaskbarSystemInfo.CoordinatorFixture", &frame))
                    throw std::runtime_error("Coordinator fixture property failed");
                Settle();
            } catch (...) {
                Close();
                throw;
            }
        }
        void Settle() {
            frame.Measure(Size{1800, 48});
            frame.Arrange(Rect{0, 0, 1800, 48});
            frame.UpdateLayout();
        }
        void Close() noexcept {
            try {
                if (window) RemoveWidgetCopy(window);
                if (island) { island.Content(nullptr); island.Close(); island = nullptr; }
            } catch (...) {}
            if (window) {
                RemovePropW(window, L"TaskbarSystemInfo.CoordinatorFixture");
                DestroyWindow(window); window = nullptr;
            }
        }
        ~TaskbarFixture() { Close(); }
    } primary(L"Shell_TrayWnd"), secondary(L"Shell_SecondaryTrayWnd");
    auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    require(CopyWindows().empty(), "Coordinator fixture started with existing copies");
    g_testFindTaskbarFrame = [](HWND window) -> FrameworkElement {
        auto frame = reinterpret_cast<Grid*>(GetPropW(
            window, L"TaskbarSystemInfo.CoordinatorFixture"));
        return frame ? *frame : nullptr;
    };
    { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
    { std::lock_guard lock(g_presentationMutex); g_presentation = model; }
    auto primaryView = CreateWidgetElements(primary.root);
    g_widget = primaryView.widget; g_widgetHost = primaryView.host;
    g_rootGrid = primary.root;

    // Loaded-frame fallback has an attached primary without its native HWND.
    // The private lookup can recover before the placement retry backoff expires.
    g_taskbarWindow = nullptr;
    SyncWidgetCopies();
    require(primary.root.Children().Size() == 1 && secondary.root.Children().Size() == 0 &&
                CopyWindows().empty(),
            "Unknown primary ownership created a duplicate or premature copy");

    // Once native ownership is known, run the actual enumeration/reconciliation.
    g_taskbarWindow = primary.window;
    SyncWidgetCopies();
    auto* copy = FindWidgetCopy(secondary.window);
    require(copy && !FindWidgetCopy(primary.window) && primary.root.Children().Size() == 1 &&
                secondary.root.Children().Size() == 1,
            "Recovered primary did not create exactly one secondary");
    require(copy->view.texts[1].Text() == FormatPercent(model->metrics.cpu),
            "Coordinator did not render the published presentation");
    require(copy->view.texts[13].Text() == L"122 KB/s" &&
                copy->view.texts[15].Text() == L"1.5 KB/s",
            "Coordinator did not deliver byte-unit network settings");
    // Render the actual primary and secondary view with the same sample. The
    // network rates are illustrative; hardware readings use the documented sample.
    primary.root.RequestedTheme(ElementTheme::Dark);
    secondary.root.RequestedTheme(ElementTheme::Light);
    primaryView.host.Background(SolidColorBrush(Color{255, 32, 32, 32}));
    copy->view.host.Background(SolidColorBrush(Color{255, 242, 242, 242}));
    auto layout = BuildWidgetLayout(*settings, model->font, WidgetLayoutMode::Full, 48);
    ApplyElementGeometry(primaryView, layout);
    primaryView.host.Width(layout.width); primaryView.host.Height(layout.height);
    std::array<SolidColorBrush, 3> primaryPalette{
        SolidColorBrush(Color{255,120,168,255}), SolidColorBrush(Color{255,255,185,0}),
        SolidColorBrush(Color{255,255,107,107})};
    StyleWidgetElements(primaryView, *settings, primaryPalette, nullptr, false);
    RenderWidgetValues(primaryView, model->metrics, *settings, model->alerts, primaryPalette, nullptr);
    for (size_t i = 0; i < 2; ++i)
        UpdateSparkline(primaryView.graphs[i], model->histories[i], *settings,
                        SampleTime::clock::now(), layout.graphs[i].Width, layout.showGraphs);
    primary.Settle(); secondary.Settle();
    UpdateSecondaryWidget(*copy, true);
    SaveImage(primaryView.host, folder, L"documentation-network-primary.png");
    SaveImage(copy->view.host, folder, L"documentation-network-secondary.png");
    auto next = std::make_shared<WidgetPresentation>(*model);
    next->metrics.cpuAvailable = true; next->metrics.cpu = 42;
    next->metrics.capturedAt = SampleTime::clock::now();
    { std::lock_guard lock(g_presentationMutex); g_presentation = next; }
    auto lookups = g_testFrameLookups;
    SendMessageW(secondary.window, WM_TIMER, kCopySubclass, 0);
    require(g_testFrameLookups == lookups, "Copy timer rediscovered the taskbar");
    require(FindWidgetCopy(secondary.window) == copy && secondary.root.Children().Size() == 1 &&
                copy->view.texts[1].Text() == FormatPercent(42),
            "Coordinator update duplicated the instance or retained old values");
    auto bitSettings = std::make_shared<ModSettings>(*settings);
    bitSettings->networkBits = true;
    { std::lock_guard lock(g_settingsMutex); g_settings = bitSettings; }
    SyncWidgetCopies();
    require(FindWidgetCopy(secondary.window) == copy && copy->settings == bitSettings &&
                copy->view.texts[13].Text() == L"1.0 Mbps" &&
                copy->view.texts[15].Text() == L"12 Kbps",
            "Coordinator settings update retained byte-unit network values");

    RequestWidgetCopySync();
    RefreshWidgetCopies();
    lookups = g_testFrameLookups;
    for (int i = 0; i < 100; ++i) {
        RefreshWidgetCopies();
        SendMessageW(secondary.window, WM_TIMER, kCopySubclass, 0);
    }
    require(g_testFrameLookups == lookups,
            "Stable refreshes repeated secondary XAML discovery");

    // A temporarily missing root receives a bounded retry budget. A subsequent
    // lifecycle event re-arms it, even after that budget has been exhausted.
    RemovePropW(secondary.window, L"TaskbarSystemInfo.CoordinatorFixture");
    RequestWidgetCopySync();
    lookups = g_testFrameLookups;
    RefreshWidgetCopies();
    auto afterFirst = g_testFrameLookups;
    RefreshWidgetCopies();
    require(g_testFrameLookups == afterFirst, "Copy retry ignored its backoff");
    for (unsigned i = 0; i < kMaximumCopySyncAttempts + 5; ++i) {
        g_copySyncState.nextAttempt = {};
        RefreshWidgetCopies();
    }
    require(g_testFrameLookups - lookups == kMaximumCopySyncAttempts,
            "Missing XAML root caused unbounded or missing retries");
    SetPropW(secondary.window, L"TaskbarSystemInfo.CoordinatorFixture", &secondary.frame);
    SendMessageW(primary.window, WM_DISPLAYCHANGE, 0, 0);
    // The fixture uses DefWindowProc for its synthetic primary. Deliver the
    // production notification procedure explicitly, with the same parameters.
    TaskbarNotificationsProc(primary.window, WM_DISPLAYCHANGE, 0, 0, 1, 0);
    RefreshWidgetCopies();
    copy = FindWidgetCopy(secondary.window);
    require(copy && !copy->cleanupPending, "Display change did not re-arm discovery");
    lookups = g_testFrameLookups;
    RefreshWidgetCopies();
    require(g_testFrameLookups == lookups, "Successful recovery kept polling");
    std::wcout << L"PASS: stable copies avoid discovery; timer consumes samples; retries are bounded and re-armed by display changes\n";

    // A wrong HWND mapping must still never put another host on the primary root.
    g_taskbarWindow = secondary.window;
    SyncWidgetCopies();
    require(!FindWidgetCopy(primary.window) && primary.root.Children().Size() == 1 &&
                !FindWidgetCopy(secondary.window),
            "Misidentified primary HWND duplicated the occupied root");
    g_taskbarWindow = primary.window;
    SyncWidgetCopies();
    require(FindWidgetCopy(secondary.window) && secondary.root.Children().Size() == 1,
            "Coordinator did not recover after primary HWND correction");

    // A later fallback must also withdraw already registered copies until
    // primary ownership recovers; leaving them registered can duplicate a move.
    g_taskbarWindow = nullptr;
    SyncWidgetCopies();
    require(CopyWindows().empty() && secondary.root.Children().Size() == 0 &&
                primary.root.Children().Size() == 1,
            "Losing primary ownership retained an existing secondary");
    g_taskbarWindow = primary.window;
    SyncWidgetCopies();
    require(FindWidgetCopy(secondary.window) && secondary.root.Children().Size() == 1,
            "Coordinator did not recover from known-to-unknown primary transition");

    // The native HWND survives a XAML root rebuild; its old subscriptions/host
    // must be removed and the controller must switch to the replacement root.
    auto oldRoot = secondary.root;
    auto beforeUnload = g_copySyncRevision.load();
    secondary.frame.Children().Clear();
    secondary.root = Grid(); secondary.root.Name(L"RootGrid");
    secondary.frame.Children().Append(secondary.root); secondary.Settle();
    Pump();
    require(g_copySyncRevision.load() != beforeUnload,
            "Replacing the XAML root did not request rediscovery");
    RefreshWidgetCopies();
    copy = FindWidgetCopy(secondary.window);
    require(copy && copy->root == secondary.root && oldRoot.Children().Size() == 0 &&
                secondary.root.Children().Size() == 1,
            "Coordinator root replacement retained the old host or lost the new one");

    auto disabled = std::make_shared<ModSettings>(*settings);
    disabled->allMonitors = false;
    { std::lock_guard lock(g_settingsMutex); g_settings = disabled; }
    SyncWidgetCopies();
    require(CopyWindows().empty() && !FindWidgetCopy(secondary.window) &&
                secondary.root.Children().Size() == 0 && primary.root.Children().Size() == 1,
            "Coordinator disable retained a secondary or removed the primary");
    { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
    SyncWidgetCopies();
    require(FindWidgetCopy(secondary.window) && secondary.root.Children().Size() == 1,
            "Coordinator did not recreate the instance after re-enabling");
    HWND removedWindow = secondary.window;
    DestroyWindow(removedWindow);
    secondary.window = nullptr;
    SyncWidgetCopies();
    require(CopyWindows().empty() && !FindWidgetCopy(removedWindow) &&
                secondary.root.Children().Size() == 0 && primary.root.Children().Size() == 1,
            "Coordinator window destruction retained a host or native registration");
    {
        std::lock_guard lock(g_copyWindowsMutex);
        g_copyWindows.push_back(removedWindow);
        g_copyWindows.push_back(primary.window);
    }
    require(RemoveAllWidgetCopies() && CopyWindows().empty(),
            "Dead or propertyless HWND blocked copy cleanup");
    std::wcout << L"PASS: production coordinator unknown/recovered primary, ownership guard, "
                  L"model/settings updates, root replacement, disable/re-enable and window removal\n";
}
