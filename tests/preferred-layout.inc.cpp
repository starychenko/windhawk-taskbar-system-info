void PreferredLayouts() {
    ModSettings settings;
    WidgetFontMetrics font;
    auto resolve = [&](double width = 1200, double height = 48) {
        return ResolveWidgetPlacement(settings, {width, height, {}, true}, {6, 6}, font);
    };
    const auto full = resolve();
    Check(full.layout.legacy && Near(full.placement.width, 410),
          "defaults preserve the existing full widget");
    settings.temperatureSource = TemperatureSource::Disabled;
    auto noTemp = resolve();
    Check(Near(noTemp.placement.width, 370) && noTemp.layout.cells[2].Width == 0 &&
          noTemp.layout.cells[8].Width == 0 && !noTemp.layout.showTemperatures,
          "disabled temperatures release the full 40-DIP column, including the host");
    Check(Near(noTemp.layout.graphs[0].Width, full.layout.graphs[0].Width),
          "hiding temperature does not spend the released space on a longer graph");
    settings.showCpuGraph = settings.showGpuGraph = false;
    auto numbers = resolve();
    Check(numbers.placement.width < noTemp.placement.width &&
          numbers.layout.graphs[0].Width == 0 && numbers.layout.graphs[1].Width == 0,
          "disabling both graphs reduces actual placement width and removes both slots");
    settings.temperatureSource = TemperatureSource::Auto;
    Check(resolve().placement.width > numbers.placement.width,
          "enabling temperature restores its space without requiring sensor data");
    settings.showCpuGraph = true;
    auto cpuOnly = resolve();
    settings.showCpuGraph = false; settings.showGpuGraph = true;
    auto gpuOnly = resolve();
    Check(cpuOnly.layout.graphVisible == std::array<bool, 2>{true, false} &&
          gpuOnly.layout.graphVisible == std::array<bool, 2>{false, true} &&
          Near(cpuOnly.placement.width, gpuOnly.placement.width) &&
          !SameWidgetLayout(cpuOnly.layout, gpuOnly.layout),
          "swapping the only graph invalidates layout even at identical outer dimensions");
    settings = ModSettings{};
    for (auto preference : {WidgetLayoutMode::Full, WidgetLayoutMode::NoGraphs,
                            WidgetLayoutMode::CompactTwoRows, WidgetLayoutMode::CompactOneRow}) {
        settings.preferredLayout = preference;
        auto wide = resolve();
        Check(wide.layout.mode == preference && wide.layout.scale == 1,
              "ample space uses the selected layout without scaling");
        auto tight = resolve(240, 48);
        Check(tight.placement.width > 0 && !tight.layout.showGraphs &&
              (preference < WidgetLayoutMode::CompactTwoRows || !tight.layout.showMemoryDetails),
              "a narrow taskbar adapts without restoring manually removed details");
        Check(SameWidgetLayout(wide.layout, resolve().layout) && settings.preferredLayout == preference,
              "returning space restores the preference without rewriting it");
    }
    settings.preferredLayout = WidgetLayoutMode::CompactOneRow;
    Check(resolve(240).layout.mode == WidgetLayoutMode::CompactTwoRows &&
          resolve(800, 20).layout.mode == WidgetLayoutMode::CompactOneRow,
          "one-row preference can use two rows for width, and one row on a low taskbar");
    Check(ParsePreferredLayout(L"") == WidgetLayoutMode::Full &&
          ParsePreferredLayout(L"future") == WidgetLayoutMode::Full &&
          ParsePreferredLayout(L"noGraphs") == WidgetLayoutMode::NoGraphs &&
          ParsePreferredLayout(L"compactTwoRows") == WidgetLayoutMode::CompactTwoRows &&
          ParsePreferredLayout(L"compactOneRow") == WidgetLayoutMode::CompactOneRow,
          "missing/unknown layout preferences retain the existing default");

    auto originalSettings = g_settings;
    for (auto preference : {WidgetLayoutMode::Full, WidgetLayoutMode::NoGraphs,
                            WidgetLayoutMode::CompactTwoRows, WidgetLayoutMode::CompactOneRow}) {
        for (bool temperatures : {false, true}) for (int graphs = 0; graphs < 4; ++graphs)
        for (bool network : {false, true}) {
            settings = ModSettings{};
            settings.preferredLayout = preference;
            settings.temperatureSource = temperatures ? TemperatureSource::Auto : TemperatureSource::Disabled;
            settings.showCpuGraph = (graphs & 1) != 0;
            settings.showGpuGraph = (graphs & 2) != 0;
            settings.showNetwork = network;
            settings.reserveSpace = true;
            g_settings = std::make_shared<ModSettings>(settings);
            for (double height : {20., 30., 48.}) for (double width : {180., 260., 410., 1200.}) {
                TaskbarGeometry geometry{width + 12, height, {}, true};
                auto placed = ResolveWidgetPlacement(settings, geometry, {6, 6}, font, false);
                if (placed.placement.width <= 0) continue;
                const auto& layout = placed.layout;
                Check(PlacementFits(geometry, placed.placement) && layout.scale * settings.fontSize >= 9 - 1e-6,
                      "selected layouts respect the real free span and readability floor");
                Check(layout.showTemperatures == temperatures &&
                      (temperatures || (layout.cells[2].Width == 0 && layout.cells[8].Width == 0)) &&
                      (!layout.graphVisible[0] || settings.showCpuGraph) &&
                      (!layout.graphVisible[1] || settings.showGpuGraph),
                      "adaptation never enables a manually hidden field");
                Check(preference == WidgetLayoutMode::Full || !layout.showGraphs,
                      "non-full preferences never upgrade to graphs");
                Check(preference < WidgetLayoutMode::CompactTwoRows || !layout.showMemoryDetails,
                      "compact preferences never upgrade to memory capacities");
                for (double dpi : {1., 1.25, 1.5, 2.}) {
                    MoveEditorState editor; editor.visual.fontMetrics = font;
                    editor.target.geometry = geometry; editor.target.scale = dpi;
                    editor.target.origin = {-1000, 100}; editor.candidate = placed.placement;
                    auto preview = ResolveMovePreviewLayout(editor, {-2000, 0, 3000, 2000});
                    Check(SameWidgetLayout(preview.widget, layout),
                          "destination preview and actual widget use the same selected layout");
                    double fraction = .37;
                    double point = placed.placement.left + placed.placement.width * fraction;
                    auto dragged = ResolveWidgetPlacement(settings, geometry,
                        {0, placed.placement.left, std::nullopt, WidgetPointerAnchor{point, fraction}}, font, false);
                    Check(SameWidgetLayout(dragged.layout, layout) &&
                          Near(dragged.placement.left + dragged.placement.width * fraction, point),
                          "dragging keeps the grabbed fraction for every visibility combination");
                }
            }
        }
    }
    g_settings = originalSettings;
}
