#include <catch2/catch_test_macros.hpp>

#include <string>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/RenderInterface.h>

#include "insys/backend/itime.h"
#include "insys/ui/rmlui_event_sink.h"
#include "insys/ui/rmlui_system_interface.h"

using namespace insys;
using namespace insys::ui;

namespace {

class FakeTime final : public ITime {
public:
    double now() const override { return m_now; }
    double m_now = 1.0;
};

class DummyRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,
                                                Rml::Span<const int>) override { return 0; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String&) override {
        dimensions = {};
        return 0;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>,
                                       Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

class ClickListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event&) override { ++clicks; }
    int clicks = 0;
};

struct RmlFixture {
    FakeTime time;
    RmlUiSystemInterface system{time};
    DummyRenderInterface renderer;
    Rml::Context* context = nullptr;

    RmlFixture() {
        Rml::SetSystemInterface(&system);
        Rml::SetRenderInterface(&renderer);
        REQUIRE(Rml::Initialise());
        context = Rml::CreateContext("ui-test", Rml::Vector2i(320, 200));
        REQUIRE(context != nullptr);
    }

    ~RmlFixture() {
        if (context != nullptr)
            Rml::RemoveContext("ui-test");
        Rml::Shutdown();
        Rml::SetRenderInterface(nullptr);
        Rml::SetSystemInterface(nullptr);
    }
};

} // namespace

TEST_CASE("RmlUiEventSink translates pointer, text and key events") {
    RmlFixture fixture;
    Rml::ElementDocument* document = fixture.context->LoadDocumentFromMemory(R"(
        <rml>
        <head></head>
        <body>
            <button id="button" style="display: block; width: 120px; height: 40px; margin: 10px;">Hit</button>
            <input id="text" type="text" style="display: block; width: 160px; height: 30px; margin: 10px;"/>
        </body>
        </rml>
    )");
    REQUIRE(document != nullptr);
    document->Show();
    fixture.context->Update();

    Rml::Element* button = document->GetElementById("button");
    Rml::Element* input = document->GetElementById("text");
    REQUIRE(button != nullptr);
    REQUIRE(input != nullptr);

    ClickListener listener;
    button->AddEventListener("click", &listener);

    RmlUiEventSink sink(*fixture.context);
    const Rml::Vector2f buttonPosition = button->GetAbsoluteOffset(Rml::BoxArea::Border);
    REQUIRE(sink.process_mouse_move(buttonPosition.x + 5.0f, buttonPosition.y + 5.0f, 0));
    REQUIRE(sink.pointer_over_ui());
    REQUIRE(sink.process_mouse_button_down(0, 0));
    REQUIRE(sink.process_mouse_button_up(0, 0));
    REQUIRE(listener.clicks == 1);

    const Rml::Vector2f inputPosition = input->GetAbsoluteOffset(Rml::BoxArea::Border);
    REQUIRE(sink.process_mouse_move(inputPosition.x + 5.0f, inputPosition.y + 5.0f, 0));
    REQUIRE(sink.process_mouse_button_down(0, 0));
    REQUIRE(sink.process_mouse_button_up(0, 0));
    REQUIRE(sink.text_input_focused());
    REQUIRE(sink.process_text_input(U'A'));
    REQUIRE(sink.process_text_input(U'B'));

    auto* form = static_cast<Rml::ElementFormControlInput*>(input);
    REQUIRE(form->GetValue() == Rml::String("AB"));

    REQUIRE(sink.process_key_down(Key::Backspace, 0));
    REQUIRE(form->GetValue() == Rml::String("A"));
    REQUIRE(sink.pointer_over_ui());

    sink.process_mouse_leave();
    REQUIRE_FALSE(sink.pointer_over_ui());
}
