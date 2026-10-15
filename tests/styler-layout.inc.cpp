// TaskbarTweaks-style host placement, with the production widget and solver.
void StylerLayoutChecks(Hosting::DesktopWindowXamlSource island, HWND child,
                       const std::wstring& folder,
                       std::shared_ptr<ModSettings> initialSettings) {
    if (!RemoveWidget()) throw std::runtime_error("Styler fixture cleanup failed");
    Grid scene, frame, root, buttons;
    scene.Width(1200); scene.Height(48);
    scene.Background(SolidColorBrush(Color{255, 20, 20, 20}));
    frame.HorizontalAlignment(HorizontalAlignment::Center); frame.MaxWidth(800);
    root.Name(L"RootGrid");
    for (int index = 0; index < 4; ++index) {
        ColumnDefinition column;
        column.Width({1, index == 1 ? GridUnitType::Star : GridUnitType::Auto});
        root.ColumnDefinitions().Append(column);
    }
    buttons.Name(L"TaskbarFrameRepeater");
    Button button; button.Width(300); button.Height(38);
    button.Content(box_value(L"Start / app buttons"));
    buttons.Children().Append(button); Grid::SetColumn(buttons, 2);
    root.Children().Append(buttons); frame.Children().Append(root);
    scene.Children().Append(frame); island.Content(scene);
    SetWindowPos(child, nullptr, 0, 0, 1200, 48, SWP_NOZORDER | SWP_NOACTIVATE);
    auto settings = std::make_shared<ModSettings>(*initialSettings);
    settings->reserveSpace = true; settings->leftOffset = 10; g_settings = settings;
    auto settle = [&] {
        for (int pass = 0; pass < 16; ++pass) {
            scene.Measure({1200, 48}); scene.Arrange({0, 0, 1200, 48});
            scene.UpdateLayout(); Pump();
            ApplyTaskbarPlacement(*settings);
        }
    };
    scene.Measure({1200, 48}); scene.Arrange({0, 0, 1200, 48});
    scene.UpdateLayout();
    if (!InjectWidget(frame)) throw std::runtime_error("Styler fixture injection failed");
    settle();
    double originalWidth = g_widgetHost.Width(), originalHeight = g_widgetHost.Height();
    auto originalMargin = g_widgetHost.Margin();
    // Changing only Grid.Column must release our old overlay width and margin.
    Grid::SetColumn(g_widgetHost, 3); Grid::SetColumnSpan(g_widgetHost, 1);
    settle();
    if (!std::isnan(g_widgetHost.Width()) || g_reservedMargin != 0 ||
        g_widgetHost.Visibility() != Visibility::Visible)
        throw std::runtime_error("A themed column retains overlay size or reservation");
    g_widgetHost.HorizontalAlignment(HorizontalAlignment::Center);
    g_widgetHost.VerticalAlignment(VerticalAlignment::Stretch);
    g_widgetHost.Width(NAN); g_widgetHost.Height(NAN);
    g_widgetHost.Padding({8, 2, 6, 0}); g_widgetHost.Margin({0, 0, 11, 0});
    g_widgetHost.BorderThickness({1, 1, 1, 1});
    g_widgetHost.BorderBrush(SolidColorBrush(Color{255, 255, 0, 255}));
    g_widgetHost.Background(SolidColorBrush(Color{255, 35, 35, 35}));
    int widthWrites = 0;
    auto token = g_widgetHost.RegisterPropertyChangedCallback(FrameworkElement::WidthProperty(),
        [&](auto const&, auto const&) { ++widthWrites; });
    for (int column : {0, 1, 3}) {
        Grid::SetColumn(g_widgetHost, column); settle();
        TaskbarProjection projection;
        projection.window = child;
        ProbeTaskbarFrameGeometry(projection, frame);
        if (!projection.themePlaced)
            throw std::runtime_error("Move preview lost the theme's placement ownership");
        auto bounds = g_widgetHost.TransformToVisual(frame).TransformBounds(
            {0, 0, float(g_widgetHost.ActualWidth()), float(g_widgetHost.ActualHeight())});
        std::wcout << L"STYLER column=" << column << L" frame=" << frame.ActualWidth()
                   << L" host=" << bounds.X << L"+" << bounds.Width
                   << L" reserved=" << g_reservedMargin << L"\n";
        if (g_widgetHost.Visibility() != Visibility::Visible || bounds.X < -.25 ||
            bounds.X + bounds.Width > frame.ActualWidth() + .25 ||
            frame.ActualWidth() > 800.25 || g_reservedMargin != 0 ||
            !std::isnan(g_widgetHost.Width()) || !std::isnan(g_widgetHost.Height()) ||
            g_widgetHost.Margin() != Thickness{0, 0, 11, 0})
            throw std::runtime_error("Themed column clips or overwrites host geometry");
        auto oldWidth = g_widgetHost.ActualWidth();
        auto revision = g_layoutRevision; settle();
        if (std::abs(g_widgetHost.ActualWidth() - oldWidth) > .1 ||
            g_layoutRevision != revision || widthWrites != 0)
            throw std::runtime_error("Themed column did not settle without property contention");
        SaveImage(scene, folder, (L"styler-column-" + std::to_wstring(column) + L".png").c_str());
    }
    // Explorer can keep arranged children while every public column reports
    // ActualWidth=0 after Styler replaces ColumnDefinitions. Recreate that
    // metadata state before the next layout pass and constrain the real root.
    root.MaxWidth(550);
    root.ColumnDefinitions().Clear();
    for (int index = 0; index < 4; ++index) {
        ColumnDefinition column;
        column.Width({1, index == 1 ? GridUnitType::Star : GridUnitType::Auto});
        root.ColumnDefinitions().Append(column);
    }
    for (auto column : root.ColumnDefinitions())
        if (column.ActualWidth() != 0)
            throw std::runtime_error("Zero-column-width fixture is not exercising Explorer state");
    auto constrained = ResolveThemedWidget(root, buttons, g_widgetHost,
        *settings, MeasureWidgetFont(*settings));
    auto hostInsets = TaskbarContentInsets(g_widgetHost);
    double contentBudget = 550 - buttons.ActualWidth() - 11 -
                           hostInsets.Left - hostInsets.Right;
    if (constrained.placement.width > contentBudget + .25)
        throw std::runtime_error("Zero column metadata ignores arranged siblings");
    settle();
    auto constrainedBounds = g_widgetHost.TransformToVisual(frame).TransformBounds(
        {0, 0, float(g_widgetHost.ActualWidth()), float(g_widgetHost.ActualHeight())});
    if (g_widgetHost.Visibility() == Visibility::Visible &&
        constrainedBounds.X + constrainedBounds.Width > frame.ActualWidth() + .25)
        throw std::runtime_error("Root MaxWidth clips the themed widget");
    root.MaxWidth(INFINITY); settle();
    frame.MaxWidth(550); settle();
    if (g_widgetHost.Visibility() == Visibility::Visible) {
        auto bounds = g_widgetHost.TransformToVisual(frame).TransformBounds(
            {0, 0, float(g_widgetHost.ActualWidth()), float(g_widgetHost.ActualHeight())});
        if (bounds.X + bounds.Width > frame.ActualWidth() + .25)
            throw std::runtime_error("Constrained themed column clips instead of adapting");
    }
    frame.MaxWidth(800); settle();
    if (g_widgetHost.Visibility() != Visibility::Visible)
        throw std::runtime_error("Themed column does not recover when space returns");
    // A stretched frame can still be content-sized by its parent's Auto
    // column. TaskbarTweaks uses this in its original dynamic-dock mode.
    ColumnDefinition contentColumn, remainingColumn;
    contentColumn.Width({1, GridUnitType::Auto});
    remainingColumn.Width({1, GridUnitType::Star});
    scene.ColumnDefinitions().Append(contentColumn);
    scene.ColumnDefinitions().Append(remainingColumn);
    frame.HorizontalAlignment(HorizontalAlignment::Stretch);
    g_widgetHost.Margin({0, 0, -1, 0});
    auto scaler = g_widgetHost.Children().GetAt(0).as<Viewbox>();
    scaler.Width(0); g_widgetHost.Visibility(Visibility::Collapsed);
    scene.UpdateLayout(); settle();
    if (g_widgetHost.Visibility() != Visibility::Visible || scaler.Width() < 300)
        throw std::runtime_error("Stretched Auto-column dock cannot grow from collapsed widget");
    double recoveredWidth = scaler.Width(); settle();
    if (std::abs(scaler.Width() - recoveredWidth) > .1)
        throw std::runtime_error("Dynamic dock shrinks its widget on each layout pass");
    frame.HorizontalAlignment(HorizontalAlignment::Center);
    scene.ColumnDefinitions().Clear();
    g_widgetHost.Margin({0, 0, 11, 0}); settle();
    g_widgetHost.UnregisterPropertyChangedCallback(FrameworkElement::WidthProperty(), token);
    bool regressionsPassed = CompositionBorderChecks(g_widgetHost);
    Grid::SetColumn(g_widgetHost, 0); Grid::SetColumnSpan(g_widgetHost, 4);
    g_widgetHost.HorizontalAlignment(HorizontalAlignment::Left);
    g_widgetHost.VerticalAlignment(VerticalAlignment::Center);
    g_widgetHost.Margin({}); g_widgetHost.Padding({}); g_widgetHost.BorderThickness({});
    // Styler restores the numeric local values captured before its Auto styles.
    g_widgetHost.Width(originalWidth); g_widgetHost.Height(originalHeight);
    g_widgetHost.Margin(originalMargin);
    settle();
    TaskbarProjection projection;
    projection.window = child;
    ProbeTaskbarFrameGeometry(projection, frame);
    if (g_hostLayoutState.themed || g_widgetHost.Visibility() != Visibility::Visible ||
        !std::isfinite(g_widgetHost.Width()) || g_reservedMargin <= 0 || projection.themePlaced)
        throw std::runtime_error("Removing themed placement does not restore automatic layout");
    Grid::SetColumn(g_widgetHost, 3); Grid::SetColumnSpan(g_widgetHost, 1); settle();
    frame.MaxWidth(550); settle();
    auto reapplied = g_widgetHost.TransformToVisual(frame).TransformBounds(
        {0, 0, float(g_widgetHost.ActualWidth()), float(g_widgetHost.ActualHeight())});
    if (!std::isnan(g_widgetHost.Width()) || !std::isnan(g_widgetHost.Height()) ||
        g_widgetHost.Margin() != Thickness{} ||
        g_widgetHost.Visibility() != Visibility::Visible ||
        reapplied.X < -.25 || reapplied.X + reapplied.Width > frame.ActualWidth() + .25) {
        std::wcout << L"FAIL: theme reapply retains restored automatic dimensions/margin\n";
        regressionsPassed = false;
    }
    SaveImage(scene, folder, L"styler-reapply.png");

    // A finite Grid allocates weighted slots even when a sibling's content is
    // smaller. Check the inner Viewbox, since the host itself can be clipped.
    frame.MaxWidth(800); root.Width(600); root.MaxWidth(600);
    root.ColumnDefinitions().Clear();
    ColumnDefinition firstStar, secondStar;
    firstStar.Width({1, GridUnitType::Star}); secondStar.Width({1, GridUnitType::Star});
    root.ColumnDefinitions().Append(firstStar); root.ColumnDefinitions().Append(secondStar);
    buttons.HorizontalAlignment(HorizontalAlignment::Left);
    buttons.Width(100); button.Width(100); Grid::SetColumn(buttons, 0);
    Grid::SetColumn(g_widgetHost, 1); Grid::SetColumnSpan(g_widgetHost, 1);
    g_widgetHost.Width(NAN); g_widgetHost.Height(NAN); g_widgetHost.Margin({});
    g_widgetHost.HorizontalAlignment(HorizontalAlignment::Left);
    for (double weight : {1., 2.}) {
        secondStar.Width({weight, GridUnitType::Star}); settle();
        auto inner = scaler.TransformToVisual(root).TransformBounds(
            {0, 0, float(scaler.ActualWidth()), float(scaler.ActualHeight())});
        double slotLeft = firstStar.ActualWidth(), slotWidth = secondStar.ActualWidth();
        if (g_widgetHost.Visibility() != Visibility::Visible ||
            inner.X < slotLeft - .25 || inner.X + inner.Width > slotLeft + slotWidth + .25 ||
            scaler.Width() > slotWidth + .25) {
            std::wcout << L"FAIL: weighted Star slot clips inner content, weight=" << weight << L"\n";
            regressionsPassed = false;
        }
        SaveImage(scene, folder, weight == 1 ? L"styler-star-equal.png" : L"styler-star-weighted.png");
    }
    // The same proportions must work before newly replaced definitions have
    // ActualWidth, including a neighbor minimum and a cap on the widget slot.
    root.ColumnDefinitions().Clear();
    firstStar = ColumnDefinition(); secondStar = ColumnDefinition();
    firstStar.Width({1, GridUnitType::Star}); firstStar.MinWidth(250);
    secondStar.Width({2, GridUnitType::Star}); secondStar.MaxWidth(330);
    root.ColumnDefinitions().Append(firstStar); root.ColumnDefinitions().Append(secondStar);
    if (firstStar.ActualWidth() != 0 || secondStar.ActualWidth() != 0)
        throw std::runtime_error("Star fallback fixture unexpectedly has arranged columns");
    auto coldStar = ResolveThemedWidget(root, buttons, g_widgetHost,
        *settings, MeasureWidgetFont(*settings));
    if (coldStar.placement.width <= 0 || coldStar.placement.width > 330.25)
        throw std::runtime_error("Zero-width Star metadata ignores weights/minimum/maximum");
    settle();
    if (scaler.Width() > secondStar.ActualWidth() + .25)
        throw std::runtime_error("Constrained Star slot clips inner content");
    // Pixel columns can also cap a themed slot below its requested width.
    for (bool capNeighbor : {false, true}) {
        root.ColumnDefinitions().Clear();
        ColumnDefinition firstPixel, secondPixel;
        firstPixel.Width({capNeighbor ? 600. : 200., GridUnitType::Pixel});
        secondPixel.Width({capNeighbor ? 400. : 600., GridUnitType::Pixel});
        (capNeighbor ? firstPixel : secondPixel).MaxWidth(200);
        root.ColumnDefinitions().Append(firstPixel);
        root.ColumnDefinitions().Append(secondPixel);
        settle();
        // Commit a rendered frame before reading transforms after replacing
        // definitions; XAML can retain the preceding frame's visual offset.
        SaveImage(scene, folder, capNeighbor ? L"styler-pixel-neighbor-cap.png" : L"styler-pixel-host-cap.png");
        auto inner = scaler.TransformToVisual(root).TransformBounds(
            {0, 0, float(scaler.ActualWidth()), float(scaler.ActualHeight())});
        double slotLeft = firstPixel.ActualWidth(), slotWidth = secondPixel.ActualWidth();
        std::wcout << L"PIXEL capNeighbor=" << capNeighbor << L" native=" << slotLeft
                   << L"+" << slotWidth << L" inner=" << inner.X << L"+" << inner.Width
                   << L" requested=" << scaler.Width() << L" host=" << g_widgetHost.ActualWidth()
                   << L" hostWidth=" << g_widgetHost.Width() << L" root=" << root.ActualWidth()
                   << L" innerActual=" << scaler.ActualWidth() << L" margin=" << scaler.Margin().Left
                   << L" hostMargin=" << g_widgetHost.Margin().Left << L" desired=" << g_widgetHost.DesiredSize().Width
                   << L"\n";
        if (g_widgetHost.Visibility() != Visibility::Visible ||
            inner.X < slotLeft - .25 || inner.X + inner.Width > slotLeft + slotWidth + .25 ||
            scaler.Width() > slotWidth + .25) {
            std::wcout << L"FAIL: constrained Pixel column clips or hides inner content\n";
            regressionsPassed = false;
        }
    }
    auto removedHost = g_widgetHost;
    if (!RemoveWidget()) throw std::runtime_error("Styler fixture teardown failed");
    auto removedLayer = removedHost.Children().GetAt(1).as<Canvas>();
    auto removedBorder = removedLayer.Children().GetAt(0).as<Border>();
    SolidColorBrush sentinel(Color{255, 1, 2, 3});
    removedBorder.BorderBrush(sentinel); removedLayer.Visibility(Visibility::Visible);
    removedHost.CornerRadius({3, 3, 3, 3}); Pump();
    if (removedBorder.BorderBrush() != sentinel || removedLayer.Visibility() != Visibility::Visible)
        throw std::runtime_error("Removed primary host retained a border callback");
    g_settings = initialSettings;
    if (!regressionsPassed) throw std::runtime_error("Styler review regressions failed");
    std::wcout << L"PASS: themed columns, Auto dimensions, bounded dock and theme removal\n";
}
