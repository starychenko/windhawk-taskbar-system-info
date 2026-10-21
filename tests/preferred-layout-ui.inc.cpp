void CheckSelectedContent(const WidgetElements& view, const WidgetLayout& layout) {
    for (int index : {2, 8}) {
        if (view.texts[index].Visibility() != (layout.showTemperatures ? Visibility::Visible : Visibility::Collapsed))
            throw std::runtime_error("Temperature visibility differs from selected content");
    }
    for (size_t i = 0; i < 2; ++i) {
        if (!layout.graphVisible[i] && (view.graphs[i].Visibility() != Visibility::Collapsed ||
                                       view.graphs[i].Width() != 0 ||
                                       view.rows[i].ColumnDefinitions().GetAt(3).Width().Value != 0))
            throw std::runtime_error("A disabled graph retains pixels or a column gap");
    }
    const int starts[] = {0, 6, 3, 9};
    for (size_t group = 0; group < 4; ++group) {
        auto row = view.rows[group];
        if (std::abs(row.ActualWidth() - layout.groups[group]) > 1.01)
            throw std::runtime_error("Selected content retains an obsolete row width");
        for (int field = 0; field < 3; ++field) {
            int index = starts[group] + field;
            auto text = view.texts[index];
            if (layout.cells[index].Width == 0) {
                if (row.ColumnDefinitions().GetAt(field).Width().Value != 0)
                    throw std::runtime_error("Hidden content retains its fixed grid column");
            } else if (text.DesiredSize().Width >
                       Controls::Primitives::LayoutInformation::GetLayoutSlot(text).Width + 1.01) {
                throw std::runtime_error("Selected content clips a visible XAML value");
            }
        }
    }
}

void SecondaryPreferenceChecks(SecondaryWidget& copy, Grid root,
                               std::shared_ptr<const ModSettings> original) {
    int cases = 0;
    for (auto mode : {WidgetLayoutMode::Full, WidgetLayoutMode::NoGraphs,
                      WidgetLayoutMode::CompactTwoRows, WidgetLayoutMode::CompactOneRow})
    for (bool temperatures : {false, true}) for (int graphs : {0, 1, 2, 3}) {
        auto settings = std::make_shared<ModSettings>(*original);
        settings->preferredLayout = mode;
        settings->temperatureSource = temperatures ? TemperatureSource::Auto : TemperatureSource::Disabled;
        settings->showCpuGraph = (graphs & 1) != 0; settings->showGpuGraph = (graphs & 2) != 0;
        copy.settings = settings;
        for (double width : {1800., 320., 1800.}) {
            root.Width(width);
            root.Measure(Size{static_cast<float>(width), 48});
            root.Arrange(Rect{0, 0, static_cast<float>(width), 48}); root.UpdateLayout();
            UpdateSecondaryWidget(copy, true); root.UpdateLayout();
            auto geometry = MeasureTaskbarGeometry(root, copy.repeater, 0, &copy.geometryCache);
            auto expected = ResolveWidgetPlacement(*settings, geometry,
                {static_cast<double>(settings->leftOffset), copy.previousLeft, SavedTaskbarFraction(copy.window)},
                copy.model->font, false);
            if (expected.placement.width <= 0) {
                if (copy.view.host.Visibility() != Visibility::Collapsed)
                    throw std::runtime_error("Copy remains visible without readable space");
                continue;
            }
            if (!copy.layout || !SameWidgetLayout(*copy.layout, expected.layout) ||
                std::abs(copy.view.host.Width() - expected.placement.width) > .01)
                throw std::runtime_error("Copy does not follow selected layout on its own taskbar");
            CheckSelectedContent(copy.view, *copy.layout);
            ++cases;
        }
    }
    root.Width(1800); root.Measure(Size{1800,48}); root.Arrange(Rect{0,0,1800,48});
    copy.settings = original; UpdateSecondaryWidget(copy, true); root.UpdateLayout();
    std::wcout << L"PASS: " << cases << L" secondary preference/visibility/shrink/restore cases on its owner thread\n";
}

void PrimaryPreferenceChecks(Grid frame, Grid root,
                             std::shared_ptr<const ModSettings> original,
                             const std::wstring& folder) {
    int cases = 0;
    double baselineWidth = 0;
    for (auto mode : {WidgetLayoutMode::Full, WidgetLayoutMode::NoGraphs,
                      WidgetLayoutMode::CompactTwoRows, WidgetLayoutMode::CompactOneRow})
    for (bool temperatures : {true, false}) for (int graphs : {3, 0, 1, 2})
    for (bool network : {false, true}) {
        auto settings = std::make_shared<ModSettings>(*original);
        settings->preferredLayout = mode;
        settings->temperatureSource = temperatures ? TemperatureSource::Auto : TemperatureSource::Disabled;
        settings->showCpuGraph = (graphs & 1) != 0; settings->showGpuGraph = (graphs & 2) != 0;
        settings->showNetwork = network; settings->leftOffset = 6;
        g_settings = settings;
        for (auto size : {Size{1600,48}, Size{280,48}, Size{800,20}, Size{1600,48}}) {
            frame.Width(size.Width); root.Width(size.Width);
            frame.Height(size.Height); root.Height(size.Height);
            frame.Measure(size); frame.Arrange(Rect{0,0,size.Width,size.Height}); frame.UpdateLayout();
            ApplyWidgetSettings(); PublishDocumentationSamples(); frame.UpdateLayout();
            if (g_widgetHost.Visibility() == Visibility::Collapsed) continue;
            const auto layout = *g_widgetLayout;
            CheckSelectedContent(PrimaryElements(), layout);
            if (size.Width == 1600 && layout.mode != mode)
                throw std::runtime_error("Wide taskbar does not restore the selected layout");
            auto revision = g_widgetVisualRevision;
            ApplyTaskbarPlacement(*settings);
            if (g_widgetVisualRevision != revision)
                throw std::runtime_error("Unchanged content preferences invalidate placement repeatedly");
            CaptureWidgetPreviewContext capture; CaptureWidgetPreview(&capture);
            for (double dpi : {1., 1.25, 2.}) {
                MoveEditorState editor; editor.visual = capture.frame;
                editor.target.geometry = {size.Width, size.Height, {}, true};
                editor.target.origin = {-1000, 100}; editor.target.scale = dpi;
                editor.candidate = {g_widgetHost.Margin().Left, g_widgetHost.Width(), g_reservedMargin};
                auto preview = ResolveMovePreviewLayout(editor, {-2000,0,3000,2000});
                if (!SameWidgetLayout(layout, preview.widget) || !PaintMovePreviewSurface(editor, preview))
                    throw std::runtime_error("Selected content differs between XAML and move preview");
                if ((!temperatures && (editor.visual.texts[2].bounds.Width != 0 || editor.visual.texts[8].bounds.Width != 0)) ||
                    (!layout.graphVisible[0] && !editor.visual.graphs[0].runs.empty()) ||
                    (!layout.graphVisible[1] && !editor.visual.graphs[1].runs.empty()))
                    throw std::runtime_error("Move preview revives manually hidden content");
                PreviewPlacementVerification confirmation{editor.candidate, preview.widget};
                VerifyPreviewedWidget(&confirmation);
                if (!confirmation.fits)
                    throw std::runtime_error("Confirmation rejects the selected destination layout");
            }
            ++cases;
        }
        if (!network && mode == WidgetLayoutMode::Full && temperatures && graphs == 3)
            baselineWidth = g_widgetHost.Width();
        if (!network && ((!temperatures && graphs == 3) || graphs == 0) && mode == WidgetLayoutMode::Full &&
            g_widgetHost.Width() >= baselineWidth - 1)
            throw std::runtime_error("Hiding temperatures or both graphs fails to shrink the actual XAML host");
        if (!network && (graphs == 3 || (!temperatures && graphs == 0) || (mode == WidgetLayoutMode::Full && temperatures))) {
            for (bool light : {false, true}) {
                root.RequestedTheme(light ? ElementTheme::Light : ElementTheme::Dark);
                frame.UpdateLayout(); ApplyWidgetSettings(); frame.UpdateLayout(); Pump();
                g_widget.Background(SolidColorBrush(light ? Color{255,242,242,242} : Color{255,32,32,32}));
                frame.UpdateLayout();
                std::wstring name = L"preferred-" + std::to_wstring(int(mode)) +
                    L"-temp" + std::to_wstring(temperatures) + L"-graphs" + std::to_wstring(graphs) +
                    (light ? L"-light.png" : L"-dark.png");
                SaveImage(g_widget, folder, name.c_str());
                g_widget.Background(nullptr);
            }
            std::wcout << L"PREFERENCE mode=" << int(mode) << L" temperatures=" << temperatures
                       << L" graphs=" << graphs << L" actual-width=" << g_widgetHost.Width() << L"\n";
        }
    }
    StopPreviewGraphics();
    g_settings = original; root.RequestedTheme(ElementTheme::Dark);
    frame.Width(520); root.Width(520); frame.Height(48); root.Height(48);
    frame.Measure(Size{520,48}); frame.Arrange(Rect{0,0,520,48}); frame.UpdateLayout();
    ApplyWidgetSettings(); frame.UpdateLayout(); Pump();
    std::wcout << L"PASS: " << cases << L" selected XAML layouts, three preview DPIs, confirmation and width savings\n";
}
