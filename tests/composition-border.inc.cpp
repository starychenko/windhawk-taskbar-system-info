// A real backdrop effect exercises the same CompositionBrush category as
// WindhawkBlur without bringing Taskbar Styler or its implementation into tests.
// The Windhawk compiler does not ship windows.graphics.effects.interop.h.
// Declare only the public IGraphicsEffectD2D1Interop ABI needed by this fixture.
struct BorderEffectInterop : ::IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetEffectId(GUID*) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR, UINT*, UINT*) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT*) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(UINT, winrt::impl::abi_t<IPropertyValue>**) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSource(UINT, ABI::Windows::Graphics::Effects::IGraphicsEffectSource**) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSourceCount(UINT*) noexcept = 0;
};
template <> inline constexpr winrt::guid winrt::impl::guid_v<BorderEffectInterop>{
    0x2FC57384, 0xA068, 0x44D7, {0xA3, 0x31, 0x30, 0x98, 0x2F, 0xCF, 0x71, 0x77}};
namespace borderTest {
namespace effects = winrt::Windows::Graphics::Effects;
namespace native = ABI::Windows::Graphics::Effects;
namespace composition = winrt::Windows::UI::Composition;
struct BlurEffect : winrt::implements<BlurEffect, effects::IGraphicsEffect,
                                    effects::IGraphicsEffectSource, BorderEffectInterop> {
    effects::IGraphicsEffectSource source = composition::CompositionEffectSourceParameter(L"backdrop");
    hstring Name() { return L"BorderTestBlur"; }
    void Name(hstring const&) {}
    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* value) noexcept override {
        if (!value) return E_POINTER;
        *value = CLSID_D2D1GaussianBlur; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR, UINT*,
        UINT*) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 3; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetProperty(UINT index,
        winrt::impl::abi_t<IPropertyValue>** value) noexcept override try {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (index > 2) return E_BOUNDS;
        auto property = (index == 0 ? PropertyValue::CreateSingle(20.f)
                                   : PropertyValue::CreateUInt32(index == 2 ? 1 : 0))
                            .as<IPropertyValue>();
        *value = static_cast<winrt::impl::abi_t<IPropertyValue>*>(winrt::detach_abi(property));
        return S_OK;
    } catch (...) { return winrt::to_hresult(); }
    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSource(UINT index, native::IGraphicsEffectSource** value) noexcept override try {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (index != 0) return E_BOUNDS;
        winrt::copy_to_abi(source, *reinterpret_cast<void**>(value));
        return S_OK;
    } catch (...) { return winrt::to_hresult(); }
};
struct Brush : Media::XamlCompositionBrushBaseT<Brush> {
    composition::Compositor compositor;
    Brush(UIElement element)
        : compositor(Hosting::ElementCompositionPreview::GetElementVisual(element).Compositor()) {}
    void OnConnected() {
        if (CompositionBrush()) return;
        auto brush = compositor.CreateEffectFactory(winrt::make<BlurEffect>()).CreateBrush();
        brush.SetSourceParameter(L"backdrop", compositor.CreateBackdropBrush());
        CompositionBrush(brush);
    }
    void OnDisconnected() { CompositionBrush(nullptr); }
};
}

bool CompositionBorderChecks(Grid host) {
    bool passed = true;
    auto drain = [] {
        // Render-only changes must work through production subscriptions,
        // without an explicit layout or border update from the fixture.
        for (int pass = 0; pass < 20; ++pass) { Pump(); Sleep(20); }
    };
    auto brush = winrt::make_self<borderTest::Brush>(host);
    brush->OnConnected();
    host.BorderBrush(*brush); host.CornerRadius({});
    host.BorderThickness({0, 1, 0, 0}); host.Padding({8, 2, 6, 0});
    host.UpdateLayout(); UpdateWidgetCompositionBorder(host);
    auto layer = host.Children().GetAt(1).as<Canvas>();
    auto border = layer.Children().GetAt(0).as<Border>();
    auto clip = border.Clip().as<RectangleGeometry>().Rect();
    if (layer.Visibility() != Visibility::Visible || border.BorderBrush() != host.BorderBrush() ||
        host.CornerRadius() != CornerRadius{} || clip.X != 1 || clip.Y != 1 ||
        clip.Width != host.ActualWidth() || clip.Height != host.ActualHeight() ||
        border.Width() != clip.Width + 2 || border.Height() != clip.Height + 2 ||
        border.BorderThickness() != Thickness{1, 2, 1, 1} ||
        border.CornerRadius() != CornerRadius{1, 1, 1, 1} ||
        Canvas::GetLeft(border) != -9 || Canvas::GetTop(border) != -4)
        throw std::runtime_error("Square backdrop border clip does not match the styled Grid");
    drain();
    host.CornerRadius({1, 1, 1, 1}); drain();
    if (layer.Visibility() != Visibility::Collapsed || border.BorderBrush()) {
        std::wcout << L"FAIL: render-only radius change retained the square overlay\n";
        passed = false;
    }
    // Establish a known state even on a failing-before build, so the brush
    // transition is tested independently of the radius failure.
    UpdateWidgetCompositionBorder(host);
    host.CornerRadius({}); drain();
    if (layer.Visibility() != Visibility::Visible || border.BorderBrush() != host.BorderBrush()) {
        std::wcout << L"FAIL: render-only radius reset did not restore the overlay\n";
        passed = false;
    }
    UpdateWidgetCompositionBorder(host);
    host.BorderBrush(SolidColorBrush(Color{255, 255, 0, 255})); drain();
    if (layer.Visibility() != Visibility::Collapsed || border.BorderBrush()) {
        std::wcout << L"FAIL: render-only solid brush change retained the backdrop overlay\n";
        passed = false;
    }
    UpdateWidgetCompositionBorder(host);
    host.BorderBrush(*brush); drain();
    if (layer.Visibility() != Visibility::Visible || border.BorderBrush() != host.BorderBrush()) {
        std::wcout << L"FAIL: render-only backdrop brush change did not restore the overlay\n";
        passed = false;
    }
    host.BorderBrush(nullptr); host.BorderThickness({}); host.Padding({});
    UpdateWidgetCompositionBorder(host);
    if (passed)
        std::wcout << L"PASS: square backdrop border clip and render-only radius/brush transitions\n";
    return passed;
}
