#include "PluginEditor.h"
#include "GuiTestDisplay.h"
#include "ProcessorRoutingState.h"
#include <iostream>
#include <stdexcept>
#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace {
using namespace vocalstack;
// Fixed-size rack fixtures deliberately exercise nested overflow independently of Stack.
constexpr int constrainedRackHeight = 388;
void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
juce::Component* find(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id) return &root;
    for (auto* child : root.getChildren()) if (auto* match = find(*child, id)) return match;
    return nullptr;
}
juce::Component* selectRackNode(juce::Component& root, uint32_t id) {
    auto* tab = find(root, "mainBusFxTab" + juce::String(id));
    expect(tab != nullptr, "routing node has a tab");
    auto* button = dynamic_cast<juce::TextButton*>(find(*tab, "mainBusFxDrag"));
    expect(button && button->onClick, "tab has a selection action");
    const auto focus = juce::Component::getCurrentlyFocusedComponent();
    button->onClick();
    auto* body = find(root, "mainBusFx" + juce::String(id));
    expect(body && body->isVisible() && button->getToggleState(), "selected tab shows its inspector");
    expect(juce::Component::getCurrentlyFocusedComponent() == focus, "tab selection is passive");
    return body;
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
            auto* card = selectRackNode(editor, uint32_t(100 + effect));
            expect(card != nullptr, "creative card exists in actual editor");
            auto* body = viewportIn(*card);
            auto* visual = descendant<CreativeEffectVisual>(*card);
            expect(body && visual, "creative card has its full visual and inner viewport");
            auto* tab = find(editor, "mainBusFxTab" + juce::String(100 + int(effect)));
            auto* header = find(*tab, "mainBusFxDrag");
            expect(header != nullptr, "creative node has a selectable drag tab");
            const auto prefix = CreativeEffectPanel::effectName(creativeKind(creativeTypes[effect])) + " / ";
            const auto reset = [&] {
                const int maximum = stack->getViewedComponent()->getHeight() - stack->getViewHeight();
                stack->setViewPosition(0, juce::jmin(maximum - 20, rack->getY() - 16));
                row->setViewPosition(juce::jmax(0, tab->getRight() - row->getViewWidth()), 0);
                body->setViewPosition(0, 0);
            };
            const int innerRange = body->getViewedComponent()->getHeight() - body->getViewHeight();
            reset();
            centre(*visual, .3f, stack, prefix + "visual at inner top scrolls Stack up");
            reset();
            centre(*visual, -.3f, innerRange > 0 ? body : stack, prefix + "visual scrolls only the viewport with range");
            reset();
            centre(*header, -.3f, stack, prefix + "tab scrolls Stack, not the row");
            reset();
            dispatch(*card, {2, 16}, -.3f, stack, prefix + "card border scrolls Stack");
            reset();
            dispatch(*rack, {2, 16}, .3f, stack, prefix + "rack background scrolls Stack");
            if (innerRange > 0) {
                reset();
                body->setViewPosition(0, 1);
                centre(*visual, .3f, body, prefix + "one pixel from inner top consumes wheel without moving Stack");
            }

        }
        parameters.check();
        std::cout << "Editor hit-tested wheel regressions: skin " << skin << ", " << cases << " cases passed" << std::endl;
    }
}
void stackRackGeometry() {
    const auto empty = defaultGraph(false);
    auto graph = defaultGraph();
    const std::array types{NodeType::waveshaper, NodeType::bitcrusher, NodeType::compressor,
        NodeType::delay, NodeType::reverb, NodeType::granular, NodeType::equalizer, NodeType::gain,
        NodeType::stereo, NodeType::gate, NodeType::xpFilter, NodeType::synthFilter,
        NodeType::convolution, NodeType::saturator, NodeType::spectralMorph};
    for (auto type : types) {
        auto edit = mainBusFx::add(graph, type);
        expect(edit.changed, "install every supported rack FX for geometry coverage");
        graph = std::move(edit.graph);
    }
    const auto effects = mainBusFx::discover(graph).effects;
    expect(effects.size() == 16, "geometry coverage includes all 16 FX types");
    std::cout << "OFFSCREEN Stack rack geometry: no physical monitor placement\n";
    for (int skin = 0; skin < 3; ++skin) {
        auto storage = std::make_unique<Processor>(); auto& owner = *storage;
        const auto publish = [&](const Graph& next) {
            juce::String error;
            expect(owner.setRoutingGraph(next, error), "publish Stack rack geometry fixture");
        };
        owner.setSkin(skin);
        Editor editor(owner);
        editor.setSize(940, 560);
        auto* stack = dynamic_cast<juce::Viewport*>(find(editor, "stackViewport"));
        auto* rack = dynamic_cast<MainBusFxRackPanel*>(find(editor, "mainBusFxRack"));
        auto* voices = descendant<DoublerPanel>(editor);
        auto* banner = find(editor, "stackAssignmentBanner");
        auto* done = find(editor, "stackAssignmentDone");
        auto* add = dynamic_cast<juce::TextButton*>(find(editor, "mainBusAddFx"));
        auto* row = find(editor, "mainBusFxRowViewport");
        expect(stack && rack && voices && banner && done && add && row, "Stack geometry components exist");
        auto* canvas = stack->getViewedComponent();
        const auto checkCanvas = [&] {
            expect(canvas->getHeight() == rack->getBottom() + editorLayout::stackBottomPadding,
                   "outer Stack content tracks rack height exactly");
            expect(rack->getY() > voices->getBottom() && canvas->getLocalBounds().contains(rack->getBounds()),
                   "rack is after the complete voice section and inside the outer canvas");
        };
        checkCanvas();
        const int emptyHeight = rack->getHeight();
        expect(emptyHeight == MainBusFxRackPanel::emptyHeight && emptyHeight <= 50
                   && !row->isVisible() && add->isEnabled() && add->getButtonText() == "Add FX"
                   && rack->getLocalBounds().contains(add->getBounds()),
               "empty rack is a single compact, usable Add FX row");

        ModulationState modulation; modulation.modulators.push_back({1});
        juce::String error;
        expect(owner.setModulationState(modulation, error), "install assignment fixture");
        owner.beginModulationAssignment(1);
        for (int i = 0; i < 4 && !banner->isVisible(); ++i) pump();
        expect(banner->isVisible() && done->isVisible() && banner->getRight() < done->getX()
                   && banner->getBottom() < voices->getY() && done->getBottom() < voices->getY(),
               "active Stack assignment row fits above voices without overlapping Done");
        for (auto* child : canvas->getChildren()) {
            if (!child->isVisible() || child == banner || child == done) continue;
            expect(!child->getBounds().intersects(banner->getBounds())
                       && !child->getBounds().intersects(done->getBounds()),
                   "visible Stack controls do not overlap assignment row");
        }
        owner.endModulationAssignment();

        publish(graph); rack->sync(); checkCanvas();
        const auto shortHeight = rack->getHeight();
        expect(shortHeight > emptyHeight, "first insertion grows the rack");
        int fit = 0, tallest = 0;
        for (auto id : effects) {
            auto* card = selectRackNode(editor, id);
            checkCanvas();
            expect(rack->getHeight() == rack->preferredHeightForWidth(rack->getWidth())
                       && card->getHeight() <= MainBusFxRackPanel::maxInspectorHeight,
                   "selected content controls Stack rack height within the outlier cap");
            tallest = juce::jmax(tallest, rack->getHeight());
            std::vector<juce::Viewport*> viewports; collectViewports(*card, viewports);
            expect(!viewports.empty(), "every FX has mounted controls");
            bool innerOverflow = false;
            for (auto* viewport : viewports) {
                auto* content = viewport->getViewedComponent();
                if (!content || content->getHeight() <= viewport->getViewHeight()) continue;
                innerOverflow = true;
                expect(viewport->getVerticalScrollBar().isVisible(), "capped content remains scrollable");
            }
            if (!innerOverflow) ++fit;
            else expect(card->getHeight() == MainBusFxRackPanel::maxInspectorHeight,
                        "only capped outliers may require inner vertical scrolling");
            stack->setViewPosition(0, canvas->getHeight());
            expect(stack->getViewArea().contains(rack->getBounds().getBottomLeft()),
                   "outer Stack scroll reaches the end of each FX");
        }
        std::cout << "Stack skin " << skin << ": " << fit << '/' << effects.size() << " FX fit without inner scrolling\n";
        expect(size_t(fit) * 10 >= effects.size() * 9, "at least 90% of FX fit without inner scrolling");
        expect(tallest > shortHeight, "different effect content produces different rack heights");
        const auto delayId = *std::find_if(effects.begin(), effects.end(), [&](auto id) {
            return mainBusFx::find(graph, id)->type == NodeType::delay;
        });
        selectRackNode(editor, delayId);
        const auto wideHeight = rack->getHeight();
        expect(rack->preferredHeightForWidth(560) > wideHeight && rack->getHeight() == wideHeight,
               "measuring the stacked narrow layout accounts for wrapping without resizing the live rack");
        selectRackNode(editor, effects.front());
        expect(rack->getHeight() == shortHeight, "returning to a short tab shrinks the rack again");
        publish(empty); rack->sync(); checkCanvas();
        expect(rack->getHeight() == emptyHeight && !row->isVisible()
                   && stack->getViewPositionY() + stack->getViewHeight() <= canvas->getHeight(),
               "removing the last FX restores the compact row and clamps outer scroll");
    }
}
void tabSelectionLifecycleRegressions(Processor& owner) {
    auto graph = mainBusFx::add(defaultGraph(), NodeType::granular).graph;
    graph = mainBusFx::add(graph, NodeType::granular).graph;
    juce::String error;
    expect(owner.setRoutingGraph(graph, error), "accept tab lifecycle fixture");
    RoutingPanel routing(owner);
    MainBusFxRackPanel rack(owner);
    rack.commitGraph = [&](const Graph& candidate, juce::String& rejection) {
        return routing.commitExternalGraphEdit(candidate, rejection);
    };
    rack.sync(); rack.setBounds(0, 0, 904, constrainedRackHeight);
    guiTestDisplay::attach(rack); pump();
    auto* first = selectRackNode(rack, 100);
    auto* second = find(rack, "mainBusFx101");
    auto* body = viewportIn(*first);
    body->setViewPosition(0, 80);
    const auto scroll = body->getViewPosition();
    const auto before = processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString();
    selectRackNode(rack, 101); selectRackNode(rack, 100);
    expect(body->getViewPosition() == scroll && find(rack, "mainBusFx101") == second,
           "duplicate tabs preserve independent inspector identity and scroll");
    expect(processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString() == before,
           "tab clicks never publish topology");
    auto* bypass = dynamic_cast<juce::TextButton*>(find(*find(rack, "mainBusFxTab101"), fxParameterId(2, "On")));
    expect(bypass != nullptr, "duplicate tab has a separate host-bound bypass button");
    bypass->setToggleState(!bypass->getToggleState(), juce::sendNotificationSync);
    expect(first->isVisible() && !second->isVisible(), "bypassing another instance does not select it");
    expect(processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString() == before,
           "bypass edits parameters without rewriting graph order");

    struct GestureProbe final : juce::AudioProcessorParameter::Listener {
        explicit GestureProbe(juce::RangedAudioParameter& p) : parameter(p) { parameter.addListener(this); }
        ~GestureProbe() override { parameter.removeListener(this); }
        void parameterValueChanged(int, float) override {}
        void parameterGestureChanged(int, bool starting) override { starting ? ++begins : ++ends; }
        juce::RangedAudioParameter& parameter;
        int begins = 0, ends = 0;
    } probe(*owner.parameters.getParameter(creativeParameterId(1, size_t(CreativeParameter::granularPitch))));
    auto* slider = dynamic_cast<juce::Slider*>(find(*first, creativeParameterId(1, size_t(CreativeParameter::granularPitch))));
    expect(slider != nullptr, "selected inspector has its host-bound pitch knob");
    const auto now = juce::Time::getCurrentTime();
    const auto point = slider->getLocalBounds().getCentre().toFloat();
    const juce::MouseEvent down{juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0, 0, 0, 0, 0,
        slider, slider, now, point, now, 1, false};
    slider->mouseDown(down);
    slider->mouseDrag(down.withNewPosition(point.translated(0, -20)));
    expect(probe.begins == 1 && probe.ends == 0, "fixture opens a real host parameter gesture");
    selectRackNode(rack, 101);
    expect(probe.ends == 1, "hiding an inspector closes its active host gesture");
    selectRackNode(rack, 100);
    expect(probe.ends == 1, "returning to an inspector does not emit an extra gesture end");

    auto reordered = mainBusFx::move(owner.getRoutingGraph(), 100, 0);
    expect(owner.setRoutingGraph(reordered.graph, error), "external tab reorder accepted"); rack.sync();
    expect(first->isVisible() && find(rack, "mainBusFx100") == first,
           "external reorder retains the selected node and inspector");
    auto* tab = find(rack, "mainBusFxTab100");
    auto* handle = find(*tab, "mainBusFxDrag");
    const auto start = handle->getLocalBounds().getCentre().toFloat();
    const juce::MouseEvent dragStart{juce::Desktop::getInstance().getMainMouseSource(), start,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0, 0, 0, 0, 0,
        handle, handle, now, start, now, 1, true};
    handle->mouseDown(dragStart);
    const auto dragEnd = dragStart.withNewPosition(start.translated(float(tab->getWidth() + MainBusFxRackPanel::tabGap), 0));
    handle->mouseDrag(dragEnd);
    auto external = mainBusFx::move(owner.getRoutingGraph(), 101, 0);
    expect(owner.setRoutingGraph(external.graph, error), "external edit during tab drag accepted");
    const auto externalState = processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString();
    handle->mouseUp(dragEnd); pump(); rack.sync();
    expect(processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString() == externalState,
           "stale tab drag never overwrites externally changed order");
    expect(first->isVisible(), "external order change keeps selection stable");

    const auto removeTab = [&](uint32_t id) {
        auto* nodeTab = find(rack, "mainBusFxTab" + juce::String(id));
        auto* button = dynamic_cast<juce::TextButton*>(find(*nodeTab, "mainBusFxRemove"));
        expect(button && button->isEnabled(), "tab has a separate enabled remove action");
        button->onClick(); pump(); rack.sync();
        expect(!find(rack, "mainBusFx" + juce::String(id)) && !find(rack, "mainBusFxTab" + juce::String(id)),
               "remove deletes both tab and inspector for that real node");
    };
    removeTab(101);
    expect(first->isVisible(), "removing an unselected tab leaves selection unchanged");
    auto inserted = mainBusFx::add(owner.getRoutingGraph(), NodeType::granular);
    expect(inserted.changed && owner.setRoutingGraph(inserted.graph, error), "external duplicate insertion accepted"); rack.sync();
    expect(find(rack, "mainBusFx101")->isVisible() && !first->isVisible(), "new effect automatically selects its own inspector");
    removeTab(101);
    expect(find(rack, "mainBusFx12")->isVisible(), "removing the last selected tab selects its previous neighbor");
    selectRackNode(rack, 100);
    slider->mouseDown(down); slider->mouseDrag(down.withNewPosition(point.translated(0, -20)));
    expect(probe.begins == 2 && probe.ends == 1, "fixture opens a gesture before node destruction");
    removeTab(100);
    expect(probe.ends == 2, "destroying an inspector closes its active host gesture before attachments die");
    expect(find(rack, "mainBusFx12")->isVisible(), "removing the first selected tab selects its next neighbor");
    removeTab(12);
    expect(mainBusFx::discover(owner.getRoutingGraph()).effects.empty(), "last tab can be removed safely");
    expect(find(rack, "mainBusAddFx")->isEnabled(), "empty tab strip still permits adding FX");
}
void tabbedInspectorsAndScreenshots(Processor& owner, const juce::File& output) {
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
    rack.sync(); rack.setBounds(0, 0, 904, constrainedRackHeight);
    guiTestDisplay::attach(rack); rack.toFront(false); pump();
    auto* row = dynamic_cast<juce::Viewport*>(find(rack, "mainBusFxRowViewport"));
    expect(row && row->getHorizontalScrollBar().isVisible(), "rack has horizontal overflow");
    expect(row->getHeight() == 42, "tab header includes its overflow scrollbar within 42px");
    const auto before = processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString();
    for (const auto& node : graph.nodes) if (mainBusFx::eligible(node.type)) {
        auto* card = selectRackNode(rack, node.id);
        expect(card->getWidth() == rack.getWidth() - 12, "selected inspector uses the full rack width");
        expect(card->getY() >= row->getBottom(), "selected inspector is below the tab strip");
        for (const auto id : mainBusFx::discover(graph).effects) {
            auto* other = find(rack, "mainBusFx" + juce::String(id));
            expect(other && other->isVisible() == (id == node.id), "only the selected body is rendered");
        }
        auto* tab = find(rack, "mainBusFxTab" + juce::String(node.id));
        expect(row->getViewArea().contains(tab->getBounds()), "selection reveals the entire tab in horizontal overflow");
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
        if (node.type == NodeType::gain) {
            auto* mix = find(*card, fxParameterId(slot, "Mix"));
            expect(mix && !mix->isShowing(), "default full-Wet Gain hides its compatibility blend");
            set(owner, fxParameterId(slot, "Mix"), 73); pump(); pump();
            expect(mix->isShowing(), "legacy Gain Wet automation reveals its original attachment");
        }
        if (node.type == NodeType::stereo) {
            const auto id = stereoMonoParameterId(slot);
            checkParameter(id);
            auto* mono = dynamic_cast<juce::Button*>(find(*card, id));
            expect(mono && mono->isEnabled(), "Stereo rack Mono Audition is persisted and enabled");
            mono->setToggleState(true, juce::sendNotificationSync);
            expect(owner.parameters.getRawParameterValue(id)->load() == 1, "rack Mono Audition writes its host parameter");
            mono->setToggleState(false, juce::sendNotificationSync);
        }
        checkParameter(fxParameterId(slot, "Mix"));
        auto* captionSlider = dynamic_cast<juce::Slider*>(find(*card, fxParameterId(slot, "Mix")));
        expect(captionSlider != nullptr, "rack Mix has a slider");
        if (bool(captionSlider->getProperties()["vocalstackTexturedBar"])) {
            auto* caption = descendant<juce::Label>(*captionSlider);
            expect(caption && !captionSlider->isTextBoxEditable() && !caption->isEditable(),
                   "fat rack Mix caption is display-only");
            set(owner, fxParameterId(slot, "Mix"), 30); pump();
            const auto point = rack.getLocalPoint(caption, caption->getLocalBounds().getCentre());
            auto* hit = rack.getComponentAt(point);
            expect(hit == captionSlider, "rack Mix caption hit passes through to the slider");
            const auto start = hit->getLocalPoint(&rack, point).toFloat();
            const auto layout = captionSlider->getLookAndFeel().getSliderLayout(*captionSlider);
            const auto end = start.translated(layout.sliderBounds.getWidth() * .2f, 0);
            const auto focus = juce::Component::getCurrentlyFocusedComponent();
            const auto now = juce::Time::getCurrentTime();
            const auto mouse = [&](juce::Point<float> position, int modifiers, bool dragged) {
                return juce::MouseEvent{juce::Desktop::getInstance().getMainMouseSource(), position,
                    juce::ModifierKeys(modifiers), 1, 0, 0, 0, 0, hit, hit, now, start, now, 1, dragged};
            };
            hit->mouseDown(mouse(start, juce::ModifierKeys::leftButtonModifier, false));
            expect(std::abs(captionSlider->getValue() - 30) < .01, "rack Mix caption mouseDown does not jump");
            hit->mouseUp(mouse(start, 0, false));
            expect(std::abs(captionSlider->getValue() - 30) < .01, "rack Mix caption click does not jump");
            hit->mouseDown(mouse(start, juce::ModifierKeys::leftButtonModifier, false));
            hit->mouseDrag(mouse(end, juce::ModifierKeys::leftButtonModifier, true));
            hit->mouseUp(mouse(end, 0, true));
            expect(std::abs(captionSlider->valueToProportionOfLength(captionSlider->getValue()) - .4) < .001,
                   "rack Mix caption drag moves at half cursor speed");
            expect(std::abs(owner.parameters.getRawParameterValue(fxParameterId(slot, "Mix"))->load()
                                - captionSlider->getValue()) < .01,
                   "rack Mix caption drag writes its host parameter");
            expect(caption->getCurrentTextEditor() == nullptr
                       && juce::Component::getCurrentlyFocusedComponent() == focus,
                   "rack Mix caption click/drag never opens numeric entry or captures focus");
        }
        if (node.type == NodeType::chorale || node.type == NodeType::equalizer) {
            auto* mixSlider = dynamic_cast<juce::Slider*>(find(*card, fxParameterId(slot, "Mix")));
            expect(mixSlider && mixSlider->getSliderStyle() == juce::Slider::LinearHorizontal,
                   "rack-owned Mix uses a standalone linear slider");
            auto* mixControl = dynamic_cast<ParameterKnob*>(mixSlider->getParentComponent());
            expect(mixControl && mixControl->getHeight() == 44 && (node.type == NodeType::chorale
                                ? mixControl->getWidth() == body->getViewedComponent()->getWidth() - 16
                                : mixControl->getWidth() <= 240),
                   "standalone rack Mix uses a 44px textured row");
            for (auto* child : mixControl->getChildren())
                if (auto* title = dynamic_cast<juce::Label*>(child))
                    expect(!title->isVisible(), "standalone rack Mix hides its external title");
            expect(bool(mixSlider->getProperties()["vocalstackTexturedBar"]), "standalone rack Mix opts into textured bar styling");
            const auto mixLayout = mixSlider->getLookAndFeel().getSliderLayout(*mixSlider);
            const auto mixBar = ParameterKnob::Style::texturedBarBounds(mixLayout.sliderBounds.toFloat()).toNearestInt();
            auto* value = descendant<juce::Label>(*mixSlider);
            expect(mixLayout.sliderBounds.getHeight() == mixSlider->getHeight() && mixBar.getHeight() == 25
                       && mixBar.getY() >= 8 && mixBar.contains(mixLayout.textBoxBounds)
                       && value && value->getBounds() == mixLayout.textBoxBounds
                       && !mixSlider->isTextBoxEditable() && !value->isEditable(),
                   "standalone rack Mix retains its lane and display-only caption inside the 25px bar");
            if (node.type == NodeType::chorale) {
                auto* motion = dynamic_cast<juce::Slider*>(find(*card, fxParameterId(slot, "Motion")));
                expect(motion && motion->getSliderStyle() == juce::Slider::LinearHorizontal
                           && motion->getTextBoxPosition() == juce::Slider::TextBoxAbove,
                       "Sparse Chorale Motion uses a thick slider");
                auto* motionControl = motion->getParentComponent();
                expect(motionControl->getWidth() == mixControl->getWidth() && motionControl->getHeight() == 44
                           && motion->getWidth() >= 64 && bool(motion->getProperties()["vocalstackTexturedBar"]),
                       "Chorale Motion and Mix retain equal full-width fat rows");
                expect(mixControl->getX() == motionControl->getX()
                           && mixControl->getY() - motionControl->getBottom() == 2,
                       "Chorale stacks its two fat sliders with a 2px gap");
            } else {
                auto* tone = find(*card, "mainBusFxFullPanel");
                expect(tone && tone->getY() == 0 && tone->getWidth() == body->getViewedComponent()->getWidth(),
                       "Tone inspector retains full width and starts at the top without a Mix strip");
                expect(mixControl->getY() >= tone->getBottom() && mixControl->getX() > tone->getWidth() / 2,
                       "Tone extra Mix occupies a compact footer on the control side");
                auto* eqGraph = descendant<toneui::EqGraph>(*tone);
                expect(eqGraph && eqGraph->getWidth() > 200, "full-width Tone retains its usable EQ graph");
            }
        }
        if (isCreativeEffect(node.type)) for (size_t i = 0; i < creativeParameterCount; ++i)
            if (creativeParameters[i].kind == creativeKind(node.type)) checkParameter(creativeParameterId(slot, i));
        if (isUtilityEffect(node.type)) for (size_t i = 0; i < utilityParameterCount; ++i)
            if (utilityParameters[i].kind == utilityKind(node.type)) checkParameter(utilityParameterId(slot, i));
        body->setViewPosition(0, 0);
    }
    auto* granular = selectRackNode(rack, 100);
    row->setViewPosition(0, 0);
    auto* body = viewportIn(*granular);
    auto* mix = dynamic_cast<juce::Slider*>(find(*granular, fxParameterId(1, "Mix")));
    expect(mix != nullptr, "granular Mix slider exists");
    auto* mixValue = descendant<juce::Label>(*mix);
    expect(mixValue && !mix->isTextBoxEditable() && !mixValue->isEditable(),
               "granular Mix retains its display-only in-bar caption");
    const auto mixBounds = body->getViewedComponent()->getLocalArea(mix, mix->getLocalBounds());
    body->setViewPosition(0, mixBounds.getY());
    const int previousY = body->getViewPositionY(); const int previousX = row->getViewPositionX();
    const int maximumY = body->getViewedComponent()->getHeight() - body->getViewHeight();
    auto* focus = juce::Component::getCurrentlyFocusedComponent();
    expect(rack.getComponentAt(rack.getLocalPoint(mixValue, mixValue->getLocalBounds().getCentre())) == mix,
           "granular Mix caption wheel hits the slider, not its display-only label");
    wheelAt(rack, *mixValue, 0, -.5f);
    expect(mixValue->getCurrentTextEditor() == nullptr, "hovering the in-bar Mix value never opens numeric entry");
    expect(previousY < maximumY ? body->getViewPositionY() > previousY : body->getViewPositionY() == previousY,
           "hovered in-bar Mix value scrolls the inspector, or stays clamped at its bottom");
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
    wheelAt(rack, *find(*find(rack, "mainBusFxTab100"), "mainBusFxDrag"), -.5f, 0);
    expect(row->getViewPositionX() > previousX, "hovered card header scrolls outer row horizontally");
    body->setViewPosition(0, 80);
    const auto rememberedPosition = body->getViewPosition();
    auto* fullPanel = find(*granular, "mainBusFxFullPanel");
    selectRackNode(rack, 101);
    expect(!granular->isVisible(), "previous inspector is hidden, not destroyed");
    expect(selectRackNode(rack, 100) == granular && find(*granular, "mainBusFxFullPanel") == fullPanel,
           "switching tabs preserves inspector components and attachments");
    expect(body->getViewPosition() == rememberedPosition, "switching tabs preserves per-instance scroll");
    int previousRight = -1;
    for (const auto id : mainBusFx::discover(graph).effects) {
        auto* tab = find(rack, "mainBusFxTab" + juce::String(id));
        expect(tab && tab->getX() > previousRight, "tab order follows actual graph order");
        previousRight = tab->getRight();
    }
    const auto addBounds = find(rack, "mainBusAddFx")->getBounds();
    row->setViewPosition(row->getViewedComponent()->getWidth(), 0);
    expect(find(rack, "mainBusAddFx")->getBounds() == addBounds && addBounds.getX() >= row->getRight(),
           "+ FX stays fixed outside horizontal overflow");
    const std::array<const char*, 3> skins{"device", "neon", "spectral"};
    for (int skin = 0; skin < 3; ++skin) {
        owner.setSkin(skin); rack.getProperties().set(vocalstackThemeProperty, skin); rack.setNeon(skin == 1);
        row->setViewPosition(0, 0); body->setViewPosition(0, 0);
        writeSnapshot(rack, output, juce::String("rack-row-") + skins[size_t(skin)]);
        writeSnapshot(*granular, output, juce::String("rack-granular-") + skins[size_t(skin)]);
        selectRackNode(rack, 12);
        writeSnapshot(rack, output, juce::String("rack-chorale-tabs-") + skins[size_t(skin)]);
        selectRackNode(rack, 106);
        viewportIn(*find(rack, "mainBusFx106"))->setViewPosition(0, 0);
        writeSnapshot(rack, output, juce::String("rack-eq-tabs-") + skins[size_t(skin)]);
        auto* reverb = selectRackNode(rack, 101);
        viewportIn(*reverb)->setViewPosition(0, 0);
        auto* reverbTab = find(rack, "mainBusFxTab101");
        auto* title = dynamic_cast<juce::TextButton*>(find(*reverbTab, "mainBusFxDrag"));
        const auto titleFont = title->getLookAndFeel().getTextButtonFont(*title, title->getHeight());
                const float textWidth = juce::GlyphArrangement::getStringWidth(titleFont, title->getButtonText());
                expect(title->getWidth() >= textWidth + 8 && title->getWidth() <= textWidth + 9,
                       "FX title has only four pixels of padding per side, without clipping");
                auto* crusherTab = find(rack, "mainBusFxTab104");
                expect(crusherTab && crusherTab->getWidth() > reverbTab->getWidth(),
                       "tab widths follow their titles rather than reserving fixed empty space");
                const auto onId = fxParameterId(2, "On");
        const float savedOn = owner.parameters.getRawParameterValue(onId)->load();
        const auto tick = [&] { pump(); pump(); pump(); };
        set(owner, onId, 1); tick();
        const auto activeTitle = title->findColour(juce::TextButton::textColourOffId);
        const auto activeSelectedTitle = title->findColour(juce::TextButton::textColourOnId);
        const auto palette = themePalette(rack, skin == 1);
        expect(activeSelectedTitle == palette.accent.contrasting(),
               "selected tab text contrasts against its accent background in each theme");
        expect(row->getViewArea().contains(reverbTab->getBounds()), "selected Reverb tab is accessible among multiple FX");
        writeSnapshot(rack, output, juce::String("rack-reverb-tabs-") + skins[size_t(skin)]);
        set(owner, onId, 0); tick();
        expect(title->findColour(juce::TextButton::textColourOffId) != activeTitle
                   && title->findColour(juce::TextButton::textColourOnId) != activeSelectedTitle,
               "rack timer dims both ordinary and selected tab titles after host bypass changes");
        expect(title->isEnabled() && title->getToggleState() && reverb->isVisible(),
               "bypassed tab stays selectable with its selected inspector visible");
        writeSnapshot(rack, output, juce::String("rack-reverb-bypassed-tabs-") + skins[size_t(skin)]);
        set(owner, onId, savedOn); tick();
        selectRackNode(rack, 100);
        row->setViewPosition(row->getViewedComponent()->getWidth(), 0);
        writeSnapshot(rack, output, juce::String("rack-row-end-") + skins[size_t(skin)]);
        // All-tabs overview can exceed the physical work area; no native window.
        rack.removeFromDesktop();
        std::cout << "OFFSCREEN all-tabs overview: no physical monitor placement\n";
        rack.setSize(row->getViewedComponent()->getWidth() + 92, constrainedRackHeight);
        row->setViewPosition(0, 0);
        writeSnapshot(rack, output, juce::String("rack-row-all-") + skins[size_t(skin)]);
        rack.setSize(904, constrainedRackHeight);
        guiTestDisplay::attach(rack);
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
    auto* card = selectRackNode(rack, 100);
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
    auto* handle = find(*find(rack, "mainBusFxTab100"), "mainBusFxDrag");
    const auto now = juce::Time::getCurrentTime();
    const auto start = handle->getLocalBounds().getCentre().toFloat();
    const auto finish = start.translated(-float(handle->getParentComponent()->getWidth() + MainBusFxRackPanel::tabGap), 0);
    const auto event = [&](juce::Point<float> point) {
        return juce::MouseEvent{juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0, 0, 0, 0, 0,
            handle, handle, now, start, now, 1, true};
    };
    handle->mouseDown(event(start)); handle->mouseDrag(event(finish)); handle->mouseUp(event(finish)); pump();
    expect(mainBusFx::discover(owner.getRoutingGraph()).effects.front() == 100, "horizontal handle drag reorders actual routing nodes");
    expect(find(rack, "mainBusFx100") == card && card->isVisible(), "drag retains selected inspector identity and attachments");
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
    expect(find(rack, "mainBusFx100") == card && card->isVisible(), "external reorder retains selection and gesture bindings");
    expect(owner.parameters.getRawParameterValue(creativeParameterId(1, size_t(CreativeParameter::granularPitch)))->load() == 5, "topology does not rewind automation");
    auto branched = owner.getRoutingGraph(); branched.links.push_back({11, 13, 0, 0});
    expect(owner.setRoutingGraph(branched, error), "custom routing accepted"); rack.sync();
    expect(!find(rack, "mainBusAddFx")->isEnabled(), "ambiguous insertion disabled");
    expect(pitch->isEnabled(), "branch FX parameters remain editable");
    expect(selectRackNode(rack, 101)->isVisible(), "ambiguous graphs still permit tab selection");
    selectRackNode(rack, 100);
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
    tabSelectionLifecycleRegressions(owner);
    stackRackGeometry();
    editorHitTestedWheelRegressions();
    tabbedInspectorsAndScreenshots(owner, output);
    std::cout << "Main Bus FX rack UI/bindings/recall tests passed\n";
}
}
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    try {
        guiTestDisplay::enumerate();
        std::cout << "Rack PNGs use offscreen component rendering, not a physical framebuffer capture\n";
        if (argc == 2 && juce::String(argv[1]) == "--geometry") { stackRackGeometry(); return 0; }
        const auto output = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
            : juce::File::getCurrentWorkingDirectory().getChildFile("mainbus-rack-screenshots");
        expect(output.createDirectory().wasOk(), "create rack screenshot directory");
        run(output);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
