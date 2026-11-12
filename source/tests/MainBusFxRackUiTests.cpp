#include "PluginEditor.h"
#include "ProcessorRoutingState.h"
#include <iostream>
#include <stdexcept>
#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace {
using namespace vocalstack;
void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
juce::Component* find(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id) return &root;
    for (auto* child : root.getChildren()) if (auto* match = find(*child, id)) return match;
    return nullptr;
}
void set(Processor& owner, const juce::String& id, float value) {
    auto* parameter = owner.parameters.getParameter(id); expect(parameter != nullptr, "host parameter exists");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
void writeSnapshot(juce::Component& component, const juce::File& directory, const juce::String& name) {
    const auto image = component.createComponentSnapshot(component.getLocalBounds());
    expect(image.isValid(), "rack card snapshot is valid");
    const auto file = directory.getChildFile(name + ".png");
    auto stream = file.createOutputStream();
    expect(stream != nullptr && stream->openedOk(), "open rack screenshot output");
    stream->setPosition(0); stream->truncate();
    expect(juce::PNGImageFormat().writeImageToStream(image, *stream), "write rack screenshot PNG");
}
void pump() {
#if JUCE_WINDOWS
    const auto until = juce::Time::getMillisecondCounter() + 40;
    do {
        MSG message;
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessage(&message);
        }
        juce::Thread::sleep(1);
    } while (juce::Time::getMillisecondCounter() < until);
#endif
}
void wheelAt(juce::Component& root, juce::Component& target, float dx, float dy) {
    const auto point = root.getLocalPoint(&target, target.getLocalBounds().getCentre());
    auto* hovered = root.getComponentAt(point);
    expect(hovered != nullptr, "wheel position hits a visible component");
    const auto local = hovered->getLocalPoint(&root, point).toFloat();
    const auto now = juce::Time::getCurrentTime();
    juce::MouseEvent event{juce::Desktop::getInstance().getMainMouseSource(), local, {}, 0, 0, 0, 0, 0,
        hovered, hovered, now, local, now, 0, false};
    juce::MouseWheelDetails wheel; wheel.deltaX = dx; wheel.deltaY = dy;
    hovered->mouseWheelMove(event, wheel);
    pump();
}
juce::Viewport* viewportIn(juce::Component& root) {
    if (auto* viewport = dynamic_cast<juce::Viewport*>(&root)) return viewport;
    for (auto* child : root.getChildren()) if (auto* viewport = viewportIn(*child)) return viewport;
    return nullptr;
}
template <typename T>
T* descendant(juce::Component& root) {
    if (auto* match = dynamic_cast<T*>(&root)) return match;
    for (auto* child : root.getChildren()) if (auto* match = descendant<T>(*child)) return match;
    return nullptr;
}
struct WheelParameterGuard final : juce::AudioProcessorParameter::Listener {
    explicit WheelParameterGuard(Processor& processor) : owner(processor) {
        for (auto* parameter : owner.getParameters()) {
            values.push_back(parameter->getValue());
            parameter->addListener(this);
        }
    }
    ~WheelParameterGuard() override {
        for (auto* parameter : owner.getParameters()) parameter->removeListener(this);
    }
    void parameterValueChanged(int, float) override { ++changes; }
    void parameterGestureChanged(int, bool) override { ++gestures; }
    void check() const {
        expect(changes == 0 && gestures == 0, "editor hover wheel must not emit host values or gestures");
        size_t index = 0;
        for (auto* parameter : owner.getParameters())
            expect(parameter->getValue() == values[index++], "editor hover wheel must preserve every parameter");
    }
    Processor& owner;
    std::vector<float> values;
    int changes = 0, gestures = 0;
};
void collectViewports(juce::Component& root, std::vector<juce::Viewport*>& result) {
    if (auto* viewport = dynamic_cast<juce::Viewport*>(&root)) result.push_back(viewport);
    for (auto* child : root.getChildren()) collectViewports(*child, result);
}
void editorHitTestedWheelRegressions() {
    auto storage = std::make_unique<Processor>(); auto& owner = *storage;
    auto graph = defaultGraph();
    const std::array creativeTypes{NodeType::granular, NodeType::reverb, NodeType::delay,
        NodeType::compressor, NodeType::bitcrusher};
    for (auto type : {NodeType::granular, NodeType::reverb, NodeType::delay, NodeType::compressor,
                      NodeType::bitcrusher, NodeType::waveshaper, NodeType::gain, NodeType::stereo}) {
        auto edit = mainBusFx::add(graph, type);
        expect(edit.changed, "add effects for real-editor nested wheel fixture"); graph = std::move(edit.graph);
    }
    juce::String error; expect(owner.setRoutingGraph(graph, error), "accept real-editor wheel graph");
    for (int skin = 0; skin < 3; ++skin) {
        owner.setSkin(skin);
        Editor editor(owner);
        editor.setSize(940, 560);
        editor.setVisible(true);
        auto* stack = dynamic_cast<juce::Viewport*>(find(editor, "stackViewport"));
        auto* row = dynamic_cast<juce::Viewport*>(find(editor, "mainBusFxRowViewport"));
        auto* rack = find(editor, "mainBusFxRack");
        expect(stack && row && rack, "real editor contains Stack and nested FX row");
        expect(stack->getViewedComponent()->getHeight() > stack->getViewHeight(), "Stack ancestor must have vertical overflow");
        expect(row->getViewedComponent()->getWidth() > row->getViewWidth(), "FX row must have horizontal overflow");
        std::vector<juce::Viewport*> viewports; collectViewports(editor, viewports);
        WheelParameterGuard parameters(owner);
        const auto routingBefore = processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString();
        int cases = 0;
        const auto dispatch = [&](juce::Component& target, juce::Point<int> targetPoint, float deltaY,
                                  juce::Viewport* expectedMover, const juce::String& description) {
            const auto require = [&](bool value, const juce::String& reason) {
                if (!value) throw std::runtime_error(("Editor skin " + juce::String(skin) + " / " + description + ": " + reason).toStdString());
            };
            const auto editorPoint = editor.getLocalPoint(&target, targetPoint);
            require(editor.getLocalBounds().contains(editorPoint), "wheel point lies inside editor");
            auto* hit = editor.getComponentAt(editorPoint);
            require(hit == &target, "wheel must hit the intended rendered surface, not a clipped child or parent");
            require(dynamic_cast<juce::Slider*>(hit) == nullptr, "regression must not dispatch directly to a slider");
            std::vector<juce::Point<int>> positions;
            for (auto* viewport : viewports) positions.push_back(viewport->getViewPosition());
            const auto focus = juce::Component::getCurrentlyFocusedComponent();
            const auto layout = owner.getRoutingLayout().toXmlString();
            const auto local = hit->getLocalPoint(&editor, editorPoint).toFloat();
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent event{juce::Desktop::getInstance().getMainMouseSource(), local, {}, 0, 0, 0, 0, 0,
                hit, hit, now, local, now, 0, false};
            juce::MouseWheelDetails wheel; wheel.deltaY = deltaY;
            hit->mouseWheelMove(event, wheel);
            int moved = 0;
            for (size_t i = 0; i < viewports.size(); ++i) {
                const auto after = viewports[i]->getViewPosition();
                require(after.x == positions[i].x, "vertical wheel must not move ANY horizontal axis, including FX rack");
                if (viewports[i] == expectedMover) {
                    require(deltaY < 0 ? after.y > positions[i].y : after.y < positions[i].y,
                            "expected viewport must move in the wheel direction");
                } else require(after == positions[i], "a different viewport moved or one event scrolled two viewports");
                if (after != positions[i]) ++moved;
            }
            require(moved == (expectedMover ? 1 : 0), "exactly the appropriate viewport moves once (none at absolute edge)");
            require(juce::Component::getCurrentlyFocusedComponent() == focus, "hover wheel must not claim focus");
            require(owner.getRoutingLayout().toXmlString() == layout, "hover must not select, reorder or edit layout state");
            require(processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString() == routingBefore,
                    "hover must not edit routing");
            parameters.check();
            ++cases;
        };
        const auto centre = [&](juce::Component& target, float deltaY, juce::Viewport* expectedMover, const juce::String& description) {
            dispatch(target, target.getLocalBounds().getCentre(), deltaY, expectedMover, description);
        };
        // Exercise Editor::Style's actual numeric labels, not a substitute look-and-feel
        // or Slider::mouseWheelMove. Every point is hit-tested through the whole editor.
        expect(knobParameterIds.size() == 6, "cover all six main knobs");
        for (const auto* id : knobParameterIds) {
            auto* slider = dynamic_cast<juce::Slider*>(find(editor, id));
            expect(slider != nullptr, "main knob exists in real editor");
            auto* label = descendant<juce::Label>(*slider);
            expect(label && label->isVisible() && !label->getText().isEmpty(), "main knob has rendered numeric value label");
            expect(label->getCurrentTextEditor() == nullptr, "numeric wheel test starts without text entry");
            stack->setViewPosition(0, 0);
            centre(*label, -.3f, stack, juce::String(id) + " numeric label scrolls Stack down without click");
            stack->setViewPosition(0, 80);
            centre(*label, .3f, stack, juce::String(id) + " numeric label scrolls Stack up without click");
            stack->setViewPosition(0, 0);
            centre(*label, .3f, nullptr, juce::String(id) + " numeric label at Stack top stays clamped");
            expect(label->getCurrentTextEditor() == nullptr, "wheel never opens numeric text entry");
        }
        for (size_t effect = 0; effect < creativeTypes.size(); ++effect) {
            auto* card = find(editor, "mainBusFx" + juce::String(100 + int(effect)));
            expect(card != nullptr, "creative card exists in actual editor");
            auto* body = viewportIn(*card);
            auto* visual = descendant<CreativeEffectVisual>(*card);
            expect(body && visual, "creative card has its full visual and inner viewport");
            auto* title = descendant<juce::Label>(*card);
            auto* header = find(*card, "mainBusFxDrag");
            expect(title && header, "creative card title and drag header exist");
            const auto prefix = CreativeEffectPanel::effectName(creativeKind(creativeTypes[effect])) + " / ";
            const auto reset = [&](int innerY) {
                stack->setViewPosition(0, 250);
                row->setViewPosition(card->getX() - 60, 0);
                body->setViewPosition(0, innerY);
                expect(stack->getViewPositionY() > 0
                           && stack->getViewPositionY() + stack->getViewHeight() < stack->getViewedComponent()->getHeight(),
                       "Stack ancestor can move in both directions while FX card is on screen");
                expect(row->getViewPositionX() > 0, "horizontal row starts away from left clamp");
            };
            reset(0);
            expect(body->getViewedComponent()->getHeight() > body->getViewHeight(), "creative inner body must overflow");
            centre(*visual, -.3f, body, prefix + "visual at inner top scrolls only inner body");
            reset(0);
            centre(*visual, .3f, stack, prefix + "visual at inner top bubbles up only to Stack");
            reset(1);
            centre(*visual, .3f, body, prefix + "visual one pixel from top consumes event without residual ancestor scroll");
            reset(0);
            const int maximum = body->getViewedComponent()->getHeight() - body->getViewHeight();
            const auto background = [&](float deltaY, juce::Viewport* expectedMover, const juce::String& name) {
                const auto point = editor.getLocalPoint(body, juce::Point<int>(2, body->getHeight() / 2));
                auto* hit = editor.getComponentAt(point);
                expect(hit == body->getViewedComponent(), "inner background point hits actual content margin, not knob or scrollbar");
                dispatch(*hit, hit->getLocalPoint(&editor, point), deltaY, expectedMover, prefix + name);
            };
            for (bool bottom : {false, true}) {
                const int edge = bottom ? maximum : 0;
                const float outward = bottom ? -.3f : .3f;
                const float inward = -outward;
                const auto edgeName = bottom ? "bottom" : "top";
                reset(edge); background(outward, stack, juce::String("background at ") + edgeName + " bubbles outward to Stack");
                reset(edge); background(inward, body, juce::String("background at ") + edgeName + " scrolls inward only in body");
                for (auto* surface : {header, static_cast<juce::Component*>(title)}) {
                    reset(edge); centre(*surface, outward, stack, prefix + "header/title at inner " + edgeName + " scrolls Stack only");
                    reset(edge); centre(*surface, inward, stack, prefix + "header/title reverse at inner " + edgeName + " scrolls Stack only");
                }
                reset(edge);
                dispatch(*card, {2, 16}, outward, stack, prefix + "card border/background at inner " + edgeName);
                reset(edge);
                dispatch(*rack, {2, rack->getHeight() / 2}, outward, stack, prefix + "rack background at inner " + edgeName);
            }
            reset(maximum - 1);
            background(-.3f, body, "background one pixel from bottom consumes event without residual ancestor scroll");

        }
        parameters.check();
        std::cout << "Editor hit-tested wheel regressions: skin " << skin << ", " << cases << " cases passed" << std::endl;
    }
}
void compactCardsAndScreenshots(Processor& owner, const juce::File& output) {
    auto graph = defaultGraph();
    const std::array types{NodeType::granular, NodeType::reverb, NodeType::delay,
        NodeType::compressor, NodeType::bitcrusher, NodeType::waveshaper, NodeType::equalizer,
        NodeType::gain, NodeType::stereo, NodeType::gate, NodeType::xpFilter, NodeType::synthFilter};
    for (const auto type : types) {
        auto edit = mainBusFx::add(graph, type); expect(edit.changed, "add screenshot/test effect"); graph = std::move(edit.graph);
    }
    juce::String error; expect(owner.setRoutingGraph(graph, error), "all ordinary effects in same rack");
    RoutingPanel routing(owner);
    MainBusFxRackPanel rack(owner);
    rack.commitGraph = [&](const Graph& candidate, juce::String& rejection) { return routing.commitExternalGraphEdit(candidate, rejection); };
    rack.sync(); rack.setBounds(0, 0, 904, MainBusFxRackPanel::preferredHeight);
    rack.addToDesktop(0); rack.setVisible(true); rack.toFront(false); pump();
    auto* row = dynamic_cast<juce::Viewport*>(find(rack, "mainBusFxRowViewport"));
    expect(row && row->getHorizontalScrollBar().isVisible(), "rack has horizontal overflow");
    const auto before = processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString();
    for (const auto& node : graph.nodes) if (mainBusFx::eligible(node.type)) {
        auto* card = find(rack, "mainBusFx" + juce::String(node.id)); expect(card != nullptr, "effect card exists");
        expect(card->getWidth() == 204 && card->getHeight() <= 310, "FX cards stay close to 172px voice cards");
        expect(find(*card, "mainBusFxDetails") == nullptr, "no Details gate or duplicate primary controls");
        auto* body = viewportIn(*card); expect(body != nullptr, "full card body scrolls vertically");
        const auto checkParameter = [&](const juce::String& id) {
            auto* control = find(*card, id);
            if (!control) throw std::runtime_error((card->getName() + ": missing " + id).toStdString());
            const auto count = [&](auto&& self, juce::Component& parent) -> int {
                int total = parent.getComponentID() == id ? 1 : 0;
                for (auto* child : parent.getChildren()) total += self(self, *child);
                return total;
            };
            expect(count(count, *card) == 1, "each parameter has one card control, not a duplicate primary strip");
            const auto bounds = body->getViewedComponent()->getLocalArea(control, control->getLocalBounds());
            body->setViewPosition(0, bounds.getY());
            expect(body->getViewArea().intersects(bounds), "every full-panel parameter is reachable by vertical scrolling");
        };
        const auto slot = size_t(routingFxSlot(node.id));
        checkParameter(fxParameterId(slot, "Mix"));
        if (isCreativeEffect(node.type)) for (size_t i = 0; i < creativeParameterCount; ++i)
            if (creativeParameters[i].kind == creativeKind(node.type)) checkParameter(creativeParameterId(slot, i));
        if (isUtilityEffect(node.type)) for (size_t i = 0; i < utilityParameterCount; ++i)
            if (utilityParameters[i].kind == utilityKind(node.type)) checkParameter(utilityParameterId(slot, i));
        body->setViewPosition(0, 0);
    }
    auto* granular = find(rack, "mainBusFx100");
    row->setViewPosition(granular->getX(), 0);
    auto* body = viewportIn(*granular);
    auto* mix = find(*granular, fxParameterId(1, "Mix"));
    const auto mixBounds = body->getViewedComponent()->getLocalArea(mix, mix->getLocalBounds());
    body->setViewPosition(0, mixBounds.getY());
    const int previousY = body->getViewPositionY(); const int previousX = row->getViewPositionX();
    auto* focus = juce::Component::getCurrentlyFocusedComponent();
    wheelAt(rack, *mix, 0, -.5f);
    expect(body->getViewPositionY() > previousY, "hovered nested knob scrolls card without a click");
    expect(row->getViewPositionX() == previousX, "vertical body wheel leaves horizontal rack alone");
    expect(juce::Component::getCurrentlyFocusedComponent() == focus, "hover wheel does not steal keyboard focus");
    body->setViewPosition(0, 0);
    auto* visual = body->getViewedComponent()->getComponentAt(20, 20);
    expect(visual != nullptr, "card visual is hit-testable without focus");
    wheelAt(rack, *visual, 0, -.5f);
    expect(body->getViewPositionY() > 0, "hovered visual scrolls nested card without clicking");
    body->setViewPosition(0, body->getViewedComponent()->getHeight());
    wheelAt(rack, *body, 0, -.5f);
    expect(row->getViewPositionX() == previousX, "vertical edge wheel never turns into horizontal row scrolling");
    wheelAt(rack, *find(*granular, "mainBusFxDrag"), -.5f, 0);
    expect(row->getViewPositionX() > previousX, "hovered card header scrolls outer row horizontally");
    const std::array<const char*, 3> skins{"device", "neon", "spectral"};
    for (int skin = 0; skin < 3; ++skin) {
        owner.setSkin(skin); rack.getProperties().set(vocalstackThemeProperty, skin); rack.setNeon(skin == 1);
        row->setViewPosition(0, 0); body->setViewPosition(0, 0);
        writeSnapshot(rack, output, juce::String("rack-row-") + skins[size_t(skin)]);
        writeSnapshot(*granular, output, juce::String("rack-granular-") + skins[size_t(skin)]);
        row->setViewPosition(row->getViewedComponent()->getWidth(), 0);
        writeSnapshot(rack, output, juce::String("rack-row-end-") + skins[size_t(skin)]);
        rack.setSize(row->getViewedComponent()->getWidth() + 12, MainBusFxRackPanel::preferredHeight);
        row->setViewPosition(0, 0);
        writeSnapshot(rack, output, juce::String("rack-row-all-") + skins[size_t(skin)]);
        rack.setSize(904, MainBusFxRackPanel::preferredHeight);
    }
    expect(processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString() == before, "layout and scrolling never publish topology");
    std::cout << "Rack row PNGs: " << output.getFullPathName() << std::endl;
}
void run(const juce::File& output) {
    auto storage = std::make_unique<Processor>(); auto& owner = *storage;
    juce::String error;
    auto graph = mainBusFx::add(defaultGraph(), NodeType::granular).graph;
    graph = mainBusFx::add(graph, NodeType::granular).graph;
    expect(owner.setRoutingGraph(graph, error), "processor accepts rack topology");
    MainBusFxRackPanel rack(owner); rack.setBounds(0, 0, 904, 244);
    expect(find(rack, "mainBusFx12") && find(rack, "mainBusFx100") && find(rack, "mainBusFx101"), "same routing node cards");
    auto* card = find(rack, "mainBusFx100");
    auto* pitch = dynamic_cast<juce::Slider*>(find(*card, creativeParameterId(1, size_t(CreativeParameter::granularPitch))));
    expect(pitch != nullptr, "Granular uses real modulation-capable host control");
    pitch->setValue(5, juce::sendNotificationSync);
    expect(owner.parameters.getRawParameterValue(creativeParameterId(1, size_t(CreativeParameter::granularPitch)))->load() == 5, "card updates shared parameter");
    expect(owner.parameters.getRawParameterValue(creativeParameterId(2, size_t(CreativeParameter::granularPitch)))->load() == -7, "duplicate remains independent");
    RoutingPanel routing(owner);
    rack.commitGraph = [&](const Graph& candidate, juce::String& rejection) {
        return routing.commitExternalGraphEdit(candidate, rejection);
    };
    rack.sync();
    auto* handle = find(*card, "mainBusFxDrag");
    const auto now = juce::Time::getCurrentTime();
    const auto start = handle->getLocalBounds().getCentre().toFloat();
    const auto finish = start.translated(-float(MainBusFxRackPanel::cardWidth + MainBusFxRackPanel::cardGap), 0);
    const auto event = [&](juce::Point<float> point) {
        return juce::MouseEvent{juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0, 0, 0, 0, 0,
            handle, handle, now, start, now, 1, true};
    };
    handle->mouseDown(event(start)); handle->mouseDrag(event(finish)); handle->mouseUp(event(finish)); pump();
    expect(mainBusFx::discover(owner.getRoutingGraph()).effects.front() == 100, "horizontal handle drag reorders actual routing nodes");
    expect(find(rack, "mainBusFx100") == card, "drag retains card identity and attachments");
    const auto history = [&](const juce::String& text) {
        for (auto* child : routing.getChildren()) if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (button->getButtonText() == text) { expect(button->isEnabled(), "shared history action enabled"); button->onClick(); rack.sync(); return; }
        expect(false, "shared history button exists");
    };
    history("Undo"); expect(mainBusFx::discover(owner.getRoutingGraph()).effects.front() == 12, "Routing Undo reverses rack drag");
    history("Redo"); expect(mainBusFx::discover(owner.getRoutingGraph()).effects.front() == 100, "Routing Redo reapplies rack drag");
    history("Undo");
    expect(owner.parameters.getRawParameterValue(creativeParameterId(1, size_t(CreativeParameter::granularPitch)))->load() == 5
        && owner.parameters.getRawParameterValue(creativeParameterId(2, size_t(CreativeParameter::granularPitch)))->load() == -7,
        "drag and shared history preserve independent duplicate parameter states");
    const auto before = processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString();
    rack.sync(); rack.resized();
    expect(processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString() == before, "refresh and layout do not publish topology");
    auto reordered = mainBusFx::move(owner.getRoutingGraph(), 100, 0);
    expect(owner.setRoutingGraph(reordered.graph, error), "external Routing edit accepted"); rack.sync();
    expect(find(rack, "mainBusFx100") == card, "external reorder retains card and gesture bindings");
    expect(owner.parameters.getRawParameterValue(creativeParameterId(1, size_t(CreativeParameter::granularPitch)))->load() == 5, "topology does not rewind automation");
    auto branched = owner.getRoutingGraph(); branched.links.push_back({11, 13, 0, 0});
    expect(owner.setRoutingGraph(branched, error), "custom routing accepted"); rack.sync();
    expect(!find(rack, "mainBusAddFx")->isEnabled(), "ambiguous insertion disabled");
    expect(pitch->isEnabled(), "branch FX parameters remain editable");
    juce::MemoryBlock state; owner.getStateInformation(state);
    expect(owner.setRoutingGraph(defaultGraph(), error), "change graph before recall"); rack.sync();
    owner.setStateInformation(state.getData(), int(state.getSize())); rack.sync();
    expect(find(rack, "mainBusFx100") && find(rack, "mainBusFx101"), "session recall restores same rack identities");
    for (int skin = 0; skin < 3; ++skin) {
        owner.setSkin(skin); rack.getProperties().set(vocalstackThemeProperty, skin); rack.setNeon(skin == 1);
        const auto image = rack.createComponentSnapshot(rack.getLocalBounds());
        expect(image.isValid() && image.getWidth() == 904, "rack renders in all three skins");
    }
    auto editor = std::make_unique<Editor>(owner);
    expect(find(*editor, "mainBusFxRack") != nullptr, "Stack replaces fixed Chorale with rack");
    editor.reset();
    editorHitTestedWheelRegressions();
    compactCardsAndScreenshots(owner, output);
    std::cout << "Main Bus FX rack UI/bindings/recall tests passed\n";
}
}
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    try {
        const auto output = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
            : juce::File::getCurrentWorkingDirectory().getChildFile("mainbus-rack-screenshots");
        expect(output.createDirectory().wasOk(), "create rack screenshot directory");
        run(output);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
