// Included by ui-smoke.cpp after the production implementation and XAML helpers.
// Only the private Explorer root lookup is substituted. Enumeration, owner-thread
// dispatch, reconciliation, subclasses, timers and XAML trees remain real.
void CoordinatorUiChecks(std::shared_ptr<const ModSettings> settings,
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
        HWND primary = g_taskbarWindow.load();
        Grid widget = g_widget, host = g_widgetHost, root = g_rootGrid;
        std::shared_ptr<const ModSettings> settings = CurrentSettings();
        std::shared_ptr<const WidgetPresentation> model;
        decltype(g_testFindTaskbarFrame) lookup = g_testFindTaskbarFrame;
        StateGuard() {
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
    auto next = std::make_shared<WidgetPresentation>(*model);
    next->metrics.cpuAvailable = true; next->metrics.cpu = 42;
    next->metrics.capturedAt = SampleTime::clock::now();
    { std::lock_guard lock(g_presentationMutex); g_presentation = next; }
    SyncWidgetCopies();
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
    secondary.frame.Children().Clear();
    secondary.root = Grid(); secondary.root.Name(L"RootGrid");
    secondary.frame.Children().Append(secondary.root); secondary.Settle();
    SyncWidgetCopies();
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
    std::wcout << L"PASS: production coordinator unknown/recovered primary, ownership guard, "
                  L"model/settings updates, root replacement, disable/re-enable and window removal\n";
}
