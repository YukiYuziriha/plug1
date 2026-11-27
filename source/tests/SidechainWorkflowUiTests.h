#pragma once
#include "RoutingPanel.h"
#include "GuiTestDisplay.h"
#include <stdexcept>
namespace vocalstack::workflowUiTests {
inline void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
inline juce::Component* find(juce::Component& component, const juce::String& id) {
    if (component.getComponentID() == id) return &component;
    for (auto* child : component.getChildren()) if (auto* result = find(*child, id)) return result;
    return nullptr;
}
template <typename T, typename Predicate>
T* findMatching(juce::Component& component, const Predicate& matches) {
    if (auto* control = dynamic_cast<T*>(&component); control && matches(*control)) return control;
    for (auto* child : component.getChildren())
        if (auto* result = findMatching<T>(*child, matches)) return result;
    return nullptr;
}
inline void fitAndAttach(juce::Component& component, int width, int height) {
    const auto work = guiTestDisplay::selected().userArea;
    component.setSize(juce::jmin(width, work.getWidth()), juce::jmin(height, work.getHeight()));
    guiTestDisplay::attach(component);
}
template <typename Predicate>
void awaitUi(const Predicate& ready, const char* why) {
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
    while (!ready() && juce::Time::getMillisecondCounterHiRes() < deadline) {
        juce::Timer::callPendingTimersSynchronously();
        if (!ready()) juce::Thread::sleep(5);
    }
    require(ready(), why);
}
inline juce::MouseEvent mouse(juce::Component& target, juce::Point<float> point, juce::Point<float> down, bool dragged = false) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
        &target, &target, now, down, now, 1, dragged};
}
inline void gainRiderReadout() {
    guiTestDisplay::enumerate();
    for (int theme = 0; theme <= int(Theme::grass); ++theme) {
        auto owner = std::make_unique<Processor>();
        owner->setSkin(theme);
        auto layout = owner->getRoutingLayout();
        layout.setProperty("selectedVoiceSlot", -1, nullptr);
        owner->setRoutingLayout(layout);
        const auto parameter = [&](const char* id, float value) {
            auto* p = owner->parameters.getParameter(id);
            require(p != nullptr, "Gain Rider host parameter missing");
            p->setValueNotifyingHost(p->convertTo0to1(value));
        };
        parameter("level", 100.f);
        parameter("levelOn", 1.f);
        owner->prepareToPlay(48000, 128);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        fitAndAttach(*editor, 940, 700);
        auto* lead = find(*editor, "leadCard");
        auto* viewport = dynamic_cast<juce::Viewport*>(find(*editor, "stackViewport"));
        require(lead && viewport && viewport->getViewedComponent(), "Selected Lead card or Stack viewport missing");
        auto* strength = find(*lead, "level");
        auto* readout = dynamic_cast<juce::Label*>(find(*lead, "gainRiderReadout"));
        auto* ride = dynamic_cast<juce::Button*>(find(*lead, "levelOn"));
        require(strength && strength->getName() == "Gain Rider", "Lead card missing Gain Rider or its stable parameter ID");
        require(ride && ride->getButtonText() == "RIDE" && ride->getToggleState(), "Lead card Ride switch is not bound");
        require(readout && readout->isShowing() && readout->getY() >= strength->getBottom(),
            "Selected Lead card readout missing or overlapping the strength knob");
        auto& canvas = *viewport->getViewedComponent();
        const auto readoutBounds = canvas.getLocalArea(readout, readout->getLocalBounds());
        viewport->setViewPosition(0, juce::jmax(0, readoutBounds.getBottom() - viewport->getHeight() + 12));
        require(viewport->getScreenBounds().contains(readout->getScreenBounds()), "Lead readout clipped by Stack viewport");
        const auto await = [&](const juce::String& expected) {
            awaitUi([&] { return readout->getText() == expected; }, "Gain Rider readout did not reflect the live DSP correction");
        };
        juce::AudioBuffer<float> audio(2, 128);
        juce::MidiBuffer midi;
        const auto render = [&](float amplitude, int blocks) {
            for (int block = 0; block < blocks; ++block) {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < audio.getNumSamples(); ++i)
                        audio.setSample(c, i, amplitude * std::sin(juce::MathConstants<float>::twoPi * i / 128.f));
                owner->processBlock(audio, midi);
            }
            return owner->getMeters().correctionDb;
        };
        await("Ride 0.0 dB");
        render(.14f, 1000);
        const auto boost = render(.037f, 120);
        require(boost > .1f, "Gain Rider boost fixture did not produce positive correction");
        await("Ride +" + juce::String(boost, 1) + " dB");
        const auto cut = render(.5f, 120);
        require(cut < -.1f, "Gain Rider cut fixture did not produce negative correction");
        await("Ride " + juce::String(cut, 1) + " dB");
        if (theme == 1) {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("source/build/gain-rider-neon.png");
            if (auto stream = file.createOutputStream()) {
                stream->setPosition(0); stream->truncate();
                juce::PNGImageFormat{}.writeImageToStream(lead->createComponentSnapshot(lead->getLocalBounds()), *stream);
            }
        }
        ride->setToggleState(false, juce::sendNotificationSync);
        require(owner->parameters.getRawParameterValue("levelOn")->load() == 0.f, "Ride switch did not write the host parameter");
        render(.5f, 140);
        await("Ride 0.0 dB");
        editor->removeFromDesktop();
        editor.reset();
        owner->releaseResources();
    }
    std::cout << "Selected Lead Gain Rider switch and live readout passed in four themes\n";
}
inline void sidechainVoiceCard() {
    auto owner = std::make_unique<Processor>();
    owner->setSkin(int(Theme::device));
    while (!owner->settings().voiceSettings[4].present)
        require(owner->addVoice(), "Could not add voices for Stack scroll fixture");
    const auto configureBus = [&](juce::AudioChannelSet channels) {
        owner->releaseResources();
        auto buses = owner->getBusesLayout();
        buses.inputBuses.set(1, channels);
        require(owner->setBusesLayout(buses), "Could not change Stack sidechain bus layout");
        owner->prepareToPlay(48000, 128);
    };
    configureBus(juce::AudioChannelSet::disabled());
    std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
    fitAndAttach(*editor, 1000, 850);
    auto* lead = find(*editor, "leadCard");
    auto* sidechain = find(*editor, "sidechainCard");
    auto* firstVoice = find(*editor, "voice1Card");
    auto* fifthVoice = find(*editor, "voice5Card");
    auto* monitor = findMatching<VoiceMonitor>(*editor, [](auto&) { return true; });
    require(lead && sidechain && firstVoice && fifthVoice && monitor && monitor->onSelect, "Stack cards or voice monitor missing");
    auto* strip = sidechain->findParentComponentOfClass<juce::Viewport>();
    require(strip && !sidechain->isShowing(), "Stack Sidechain card was visible with a disabled bus");
    const int originalFirstVoiceX = firstVoice->getX();
    monitor->onSelect(4);
    require(strip->getViewArea().contains(fifthVoice->getBounds()), "Voice 5 was not revealed before bus change");
    const int originalViewX = strip->getViewPositionX();

    configureBus(juce::AudioChannelSet::mono());
    awaitUi([&] { return sidechain->isShowing(); }, "Stack Sidechain card did not appear on the open editor");
    const int gap = originalFirstVoiceX - lead->getRight();
    require(sidechain->getX() == originalFirstVoiceX && firstVoice->getX() == sidechain->getRight() + gap,
        "Sidechain did not insert immediately after Lead");
    require(strip->getViewPositionX() > originalViewX && strip->getViewArea().contains(fifthVoice->getBounds())
        && int(owner->getRoutingLayout().getProperty("selectedVoiceSlot")) == 4,
        "Showing Sidechain displaced the revealed voice or changed its selection");
    monitor->onSelect(VoiceMonitor::leadSlot);
    require(strip->getViewPositionX() == 0 && strip->getViewArea().contains(sidechain->getBounds()),
        "Revealing Lead did not show the adjacent Sidechain card");

    auto* process = dynamic_cast<juce::Button*>(find(*sidechain, sidechainParameterIds[0]));
    auto* on = owner->parameters.getParameter(sidechainParameterIds[0]);
    const auto* leadOn = owner->parameters.getRawParameterValue(leadParameterIds[0]);
    require(process && on && leadOn && process->getButtonText() == "Process" && !process->getWantsKeyboardFocus(),
        "Sidechain processing switch missing or claiming keyboard focus");
    const float originalLeadOn = leadOn->load();
    on->setValueNotifyingHost(1.f);
    require(process->getToggleState(), "Sidechain processing switch did not follow host automation");
    process->setToggleState(false, juce::sendNotificationSync);
    require(on->getValue() == 0.f && leadOn->load() == originalLeadOn && sidechain->isShowing(),
        "Sidechain bypass wrote the wrong parameter or hid the card");

    std::array<juce::Slider*, 8> sliders{};
    for (size_t field = 1; field < sidechainParameterIds.size(); ++field) {
        const auto id = sidechainParameterIds[field];
        auto*& slider = sliders[field - 1];
        slider = dynamic_cast<juce::Slider*>(find(*sidechain, id));
        auto* parameter = owner->parameters.getParameter(id);
        const auto* leadValue = owner->parameters.getRawParameterValue(leadParameterIds[field]);
        require(slider && parameter && leadValue && !slider->getWantsKeyboardFocus(), "Sidechain control or host parameter missing");
        const float originalLeadValue = leadValue->load();
        parameter->setValueNotifyingHost(0.f);
        require(std::abs(slider->getValue() - parameter->convertFrom0to1(0.f)) < 1.e-4,
            "Sidechain control did not follow its host parameter");
        slider->setValue(slider->getMaximum(), juce::sendNotificationSync);
        require(std::abs(parameter->getValue() - 1.f) < 1.e-4f && leadValue->load() == originalLeadValue,
            "Sidechain control did not write its own parameter independently of Lead");
    }
    for (const auto theme : {Theme::device, Theme::neon}) {
        owner->setSkin(int(theme));
        awaitUi([&] { return componentTheme(*sidechain) == theme; }, "Sidechain card did not follow the Stack skin");
        juce::RectangleList<int> used;
        used.add(process->getBounds());
        for (auto* slider : sliders) {
            const auto* control = slider->getParentComponent();
            const auto bounds = sidechain->getLocalArea(control, control->getLocalBounds());
            const auto lanes = slider->getLookAndFeel().getSliderLayout(*slider);
            require(sidechain->getLocalBounds().contains(bounds) && !used.intersectsRectangle(bounds)
                && lanes.sliderBounds.getHeight() >= 10 && !lanes.textBoxBounds.isEmpty(),
                "Sidechain controls overlap, clip or have no room for the slider rail and value");
            used.add(bounds);
        }
    }

    monitor->onSelect(4);
    configureBus(juce::AudioChannelSet::disabled());
    awaitUi([&] { return !sidechain->isShowing(); }, "Stack Sidechain card did not hide on the open editor");
    require(firstVoice->getX() == originalFirstVoiceX && strip->getViewArea().contains(fifthVoice->getBounds())
        && int(owner->getRoutingLayout().getProperty("selectedVoiceSlot")) == 4,
        "Hiding Sidechain left a layout gap or displaced the revealed voice");
    configureBus(juce::AudioChannelSet::stereo());
    awaitUi([&] { return sidechain->isShowing(); }, "Stack Sidechain card did not return for a stereo bus");
    require(!process->getToggleState() && sliders.back()->getValue() == sliders.back()->getMaximum(),
        "Sidechain bypass or Pitch edit was lost across bus changes");
    editor->removeFromDesktop();
    editor.reset();
    owner->releaseResources();
    std::cout << "Live Stack Sidechain card, independent bindings and layout passed\n";
}
inline void sidechainBlendControls() {
    guiTestDisplay::enumerate();
    sidechainVoiceCard();
    auto owner = std::make_unique<Processor>();
    owner->setSkin(int(Theme::device));
    juce::String error;
    require(owner->setRoutingGraph(defaultGraph(false), error), "Could not seed pre-insertion canonical routing");
    std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
    fitAndAttach(*editor, 1000, 850);
    const auto capture = [&](const juce::String& name) {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("source/build/" + name + ".png");
        if (auto stream = file.createOutputStream()) {
            stream->setPosition(0); stream->truncate();
            juce::PNGImageFormat{}.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), *stream);
        }
    };
    auto* version = dynamic_cast<juce::Label*>(find(*editor, "pluginVersion"));
    require(version && version->isShowing() && version->getText() == "v" JucePlugin_VersionString,
        "Loaded plugin version must be visible in the header");
    auto* blend = dynamic_cast<juce::Slider*>(find(*editor, "sidechainBlend"));
    auto* label = dynamic_cast<juce::Label*>(find(*editor, "sidechainBlendLabel"));
    require(blend && label && blend->isShowing() && label->isShowing(), "Sidechain Blend Stack controls missing");
    require(!blend->isEnabled() && !label->isEnabled() && label->getText() == "Sidechain?",
        "Sidechain? should be disabled without a configured host bus");
    auto buses = owner->getBusesLayout();
    buses.inputBuses.set(1, juce::AudioChannelSet::mono());
    require(owner->setBusesLayout(buses), "Could not configure GUI fixture sidechain bus");
    owner->prepareToPlay(48000, 128);
    awaitUi([&] { return blend->isEnabled() && label->isEnabled() && label->getText() == "Sidechain"; },
        "Sidechain Blend did not enable on bus configuration");
    require(!blend->getWantsKeyboardFocus(), "Sidechain control claims host keyboard focus");
    auto* parameter = owner->parameters.getParameter("sidechainBlend");
    require(parameter != nullptr, "Sidechain Blend host parameter missing");
    blend->setValue(43., juce::sendNotificationSync);
    require(owner->parameters.getRawParameterValue("sidechainBlend")->load() == 43.f,
        "Sidechain slider did not update the host parameter");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(72.f));
    require(blend->getValue() == 72., "Host automation did not update the Sidechain slider");

    auto* skin = findMatching<juce::TextButton>(*editor, [](auto& button) { return button.getButtonText().startsWith("SKIN"); });
    require(skin && skin->isShowing(), "Skin switch is missing");
    for (int index = 0; index <= int(Theme::grass); ++index) {
        const auto theme = static_cast<Theme>(index);
        require(owner->getSkin() == index && componentTheme(*label) == theme, "Skin switch did not update Sidechain controls");
        const auto font = label->getFont();
        const juce::Font expected(themeFontOptions(theme, 14.f));
        require(font.getTypefaceName() == expected.getTypefaceName() && font.getTypefaceStyle() == expected.getTypefaceStyle(),
            "Sidechain label did not use the selected theme font");
        if (theme == Theme::neon) require(font.getTypefaceName().startsWith("Oxanium"), "Neon label did not use Oxanium");
        if (theme == Theme::grass) {
            require(font.getTypefaceName() == "Pixelify Sans" && skin->getButtonText() == "SKIN / GRASS",
                "Grass label or font missing");
            auto copy = std::make_unique<Processor>();
            require(copy->pasteSettings(owner->settingsText()) && copy->getSkin() == int(Theme::grass), "Grass skin did not round-trip");
        }
        if (theme == Theme::grass) capture("grass-block-stack");
        else if (theme == Theme::neon) capture("oxanium-neon-stack");
        skin->onClick();
    }
    require(owner->getSkin() == int(Theme::device), "Grass did not cycle back to Device");

    auto* routing = findMatching<juce::TextButton>(*editor, [](auto& button) { return button.getButtonText() == "Routing"; });
    require(routing && routing->isShowing(), "Routing tab missing");
    routing->onClick();
    require(!blend->isShowing(), "Stack knob stayed visible in Routing");
    auto layout = owner->getRoutingLayout();
    layout.removeProperty("selectedVoiceSlot", nullptr);
    layout.setProperty("selectedNode", int(sidechainBlendNodeId), nullptr);
    layout.setProperty("inspectorVisible", true, nullptr);
    owner->setRoutingLayout(layout);
    awaitUi([&] {
        return findMatching<juce::Label>(*editor, [](auto& candidate) {
            return candidate.isShowing() && candidate.getText().endsWith("Sidechain Blend");
        }) != nullptr;
    }, "Sidechain Blend Routing inspector was not shown");
    owner->setSkin(int(Theme::grass));
    awaitUi([&] { return componentTheme(*editor) == Theme::grass; }, "Routing did not switch to Grass Block");
    capture("grass-block-routing");
    editor->removeFromDesktop();
    owner->releaseResources();
    std::cout << "Native Sidechain Blend controls, Routing node and four theme fonts passed\n";
}
inline void run() {
    guiTestDisplay::enumerate();
    const auto linkExists = [](const Graph& graph, uint32_t from, uint32_t to, int sourcePin = 0, int inputPin = 0) {
        return std::any_of(graph.links.begin(), graph.links.end(), [=](const auto& link) {
            return link.source == from && link.destination == to && link.sourcePin == sourcePin && link.inputPin == inputPin;
        });
    };
    const auto dragOntoCable = [](RoutingPanel& panel, uint32_t id, uint32_t from, uint32_t to, bool canSplice) {
        const auto links = panel.displayLinks();
        const auto cable = std::find_if(links.begin(), links.end(), [=](const auto& item) {
            return item.link.source == from && item.link.destination == to;
        });
        require(cable != links.end(), "Splice test cable is not visible");
        const auto path = panel.displayedCablePath(*cable);
        const auto end = path.getPointAlongPath(path.getLength() * .5f);
        const auto start = panel.toScreen(panel.nodeBounds(*panel.node(id)).getTopLeft().translated(25, 14));
        const auto bounds = panel.canvas.getLocalBounds().toFloat();
        require(bounds.contains(start) && bounds.contains(end), "Splice gesture fell outside the canvas");
        panel.canvas.mouseDown(mouse(panel.canvas, start, start));
        require(panel.draggingNode == id && panel.spliceHighlight < 0, "Click did not pick up the node or highlighted before a drag");
        panel.canvas.mouseDrag(mouse(panel.canvas, end, start, true));
        require(panel.spliceHighlight == (canSplice ? cable->index : -1), "Wrong cable splice highlight during drag");
        panel.canvas.mouseUp(mouse(panel.canvas, end, start, true));
        require(panel.spliceHighlight < 0, "Splice highlight persisted after release");
    };
    {
        auto owner = std::make_unique<Processor>();
        Graph graph{{{1, NodeType::lead}, {300, NodeType::splitter}, {301, NodeType::splitter},
                     {200, NodeType::mix}, {13, NodeType::output}}, {{1, 300, 0, 0}, {300, 200, 1, 1}, {200, 13, 0, 0}}};
        juce::String error;
        require(owner->setRoutingGraph(graph, error), "Main splice fixture rejected");
        RoutingPanel panel(*owner);
        fitAndAttach(panel, 1200, 950);
        panel.positions[1] = {10, 20}; panel.positions[300] = {230, 20}; panel.positions[200] = {570, 20};
        panel.positions[13] = {800, 20}; panel.positions[301] = {300, 160};
        panel.fit.onClick();
        dragOntoCable(panel, 1, 300, 200, false);
        require(linkExists(owner->getRoutingGraph(), 300, 200, 1, 1), "Dragging a connected node changed the cable");
        panel.undo.onClick();
        const auto before = panel.positions.at(301);
        dragOntoCable(panel, 301, 300, 200, true);
        const auto spliced = owner->getRoutingGraph();
        require(!linkExists(spliced, 300, 200, 1, 1) && linkExists(spliced, 300, 301, 1, 0)
            && linkExists(spliced, 301, 200, 0, 1), "Main splice did not preserve both endpoint pins");
        require(panel.positions.at(301) != before, "Main splice did not move the node");
        panel.undo.onClick();
        require(panel.positions.at(301) == before && linkExists(owner->getRoutingGraph(), 300, 200, 1, 1),
            "One undo did not restore both layout and original cable");
    }
    for (int theme = 0; theme <= int(Theme::grass); ++theme) {
        const bool legacy = theme % 2 == 0;
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        constexpr uint32_t parent = 100, first = 101, second = 102;
        const auto wetOut = wetBoundaryId(parent, false), wetReturn = wetBoundaryId(parent, true);
        Graph graph{{{1, NodeType::lead}, {parent, legacy ? NodeType::granular : NodeType::delay},
                     {13, NodeType::output}}, {{1, parent, 0, 0}, {parent, 13, 0, 0}}};
        if (legacy) {
            RoutingNode child{first, NodeType::reverb};
            child.wetOwner = parent; child.mix = 1.f;
            graph.nodes.push_back(child);
        }
        juce::String error;
        require(owner->setRoutingGraph(graph, error), "Wet-pin UI graph rejected");
        juce::ValueTree layout("RoutingLayout");
        layout.setProperty("modulationVisible", false, nullptr);
        owner->setRoutingLayout(layout);
        if (legacy) require(owner->pasteSettings(owner->settingsText()), "Legacy implicit wet path did not recall");
        RoutingPanel panel(*owner); panel.setNeon(theme == int(Theme::neon));
        fitAndAttach(panel, 1200, 950);
        panel.positions[1] = {10, 20}; panel.positions[parent] = {240, 20}; panel.positions[13] = {680, 20};
        const juce::Point<float> firstPosition{560, 200}, secondPosition{80, 260};
        const auto visible = [&](uint32_t id) {
            const auto* node = panel.node(id);
            return node && panel.nodeVisible(*node);
        };
        const auto shown = [&](uint32_t from, uint32_t to) {
            const auto links = panel.displayLinks();
            return std::any_of(links.begin(), links.end(), [=](const auto& item) {
                return item.link.source == from && item.link.destination == to;
            });
        };
        const auto checkInline = [&] {
            require(visible(1) && visible(parent) && visible(13) && shown(1, parent) && shown(parent, 13),
                "The wet path displaced the main routing canvas");
            require(!find(panel, "wetPocket") && !find(panel, "backToRouting"), "Wet routing still exposes mode navigation");
            require(!visible(wetOut) && !visible(wetReturn), "Wet boundaries appeared as standalone nodes");
        };
        checkInline();
        require(panel.wetPinCanvasPosition(parent, true) && panel.wetPinCanvasPosition(parent, false),
            "Owner did not expose both wet pins before an edit");
        if (legacy) {
            require(visible(first) && shown(wetOut, first) && shown(first, wetReturn),
                "Legacy implicit wet child and cables were not visible on load");
            require(!panel.undo.isEnabled(), "Displaying the legacy wet path created undo history");
            panel.positions[first] = firstPosition;
        } else {
            require(!panel.node(wetOut) && !panel.node(wetReturn), "Unwired wet pins created graph endpoints");
            panel.addNode(51, firstPosition); // Reverb
        }
        panel.addNode(61, secondPosition); // Stereo
        require(visible(first) && visible(second) && panel.node(second)->wetOwner == 0,
            "Adding a node on the main canvas did not leave it visible and unowned");
        panel.fit.onClick();
        const auto pan = panel.pan;
        const auto zoom = panel.zoom;
        const auto checkView = [&] {
            checkInline();
            require(panel.pan.getDistanceFrom(pan) < .01f && std::abs(panel.zoom - zoom) < 1.e-5f,
                "Wet edit changed the canvas camera");
        };
        const auto wire = [&](juce::Point<float> start, juce::Point<float> end) {
            const auto bounds = panel.canvas.getLocalBounds().toFloat();
            require(bounds.contains(start) && bounds.contains(end), "Wet cable pins fell outside the canvas");
            panel.canvas.mouseDown(mouse(panel.canvas, start, start));
            panel.canvas.mouseDrag(mouse(panel.canvas, end, start, true));
            panel.canvas.mouseUp(mouse(panel.canvas, end, start, true));
            checkView();
        };
        if (!legacy) {
            require(panel.node(first)->wetOwner == 0, "New Reverb was already wet-owned");
            wire(*panel.wetPinCanvasPosition(parent, true), panel.toScreen(panel.pinPoint(*panel.node(first), 0, false)));
            require(linkExists(owner->getRoutingGraph(), wetOut, first) && panel.node(first)->wetOwner == parent,
                "Dragging Wet Out to Reverb did not attach it to the wet path");
            panel.undo.onClick();
            require(panel.node(first) && panel.node(first)->wetOwner == 0 && !panel.node(wetOut) && !panel.node(wetReturn),
                "Undo of the first wet cable left ownership or graph endpoints behind");
            panel.redo.onClick();
            wire(panel.toScreen(panel.pinPoint(*panel.node(first), 0, true)), *panel.wetPinCanvasPosition(parent, false));
            require(linkExists(owner->getRoutingGraph(), first, wetReturn), "Dragging Reverb to Wet Return did not close the path");
        }
        const auto beforeSplice = panel.positions.at(second);
        dragOntoCable(panel, second, wetOut, first, true);
        const auto verifyChain = [&] {
            const auto current = owner->getRoutingGraph();
            for (const auto id : {wetOut, second, first, wetReturn}) {
                const auto* child = mainBusFx::find(current, id);
                require(child && child->wetOwner == parent, "Wet path has incorrect ownership");
            }
            require(linkExists(current, wetOut, second) && linkExists(current, second, first)
                && linkExists(current, first, wetReturn) && current.links.size() == graph.links.size() + 3,
                "Spliced wet path did not preserve the expected topology");
            require(visible(first) && visible(second) && shown(wetOut, second) && shown(second, first)
                && shown(first, wetReturn), "Wet nodes and cables were not displayed inline");
            checkView();
        };
        verifyChain();
        panel.undo.onClick();
        require(panel.positions.at(second) == beforeSplice && panel.node(second) && panel.node(second)->wetOwner == 0
            && shown(wetOut, first) && shown(first, wetReturn) && !shown(wetOut, second),
            "Splice undo did not restore the original cable, ownership and position");
        panel.redo.onClick();
        verifyChain();
        const auto splicedPosition = panel.positions.at(second);
        const auto saved = owner->settingsText();
        panel.undo.onClick();
        require(!shown(wetOut, second), "Recall fixture did not undo the saved chain");
        require(owner->pasteSettings(saved), "Wet-pin patch recall failed");
        panel.refreshGraphFromOwner();
        require(!panel.undo.isEnabled() && !panel.redo.isEnabled()
            && panel.positions.at(second).getDistanceFrom(splicedPosition) < .01f,
            "Recall did not restore layout or clear stale history");
        verifyChain();

        ParameterKnob mix(*owner, "fx1Mix", "Mix", "%"); mix.setLinear(true); mix.setSize(320, 64);
        auto& slider = mix.slider; slider.setValue(25, juce::dontSendNotification);
        const auto lanes = slider.getLookAndFeel().getSliderLayout(slider);
        const auto from = lanes.sliderBounds.toFloat().getCentre();
        const auto to = from.translated(60, 0);
        const auto originalSensitivity = slider.getMouseDragSensitivity();
        slider.mouseDown(mouse(slider, from, from));
        require(slider.getMouseDragSensitivity() == lanes.sliderBounds.getWidth(), "Fat Mix rail does not use 1:1 pointer travel");
        slider.mouseDrag(mouse(slider, to, from, true));
        const double expected = .25 + 60. / lanes.sliderBounds.getWidth();
        require(std::abs(slider.valueToProportionOfLength(slider.getValue()) - expected) < .015, "Fat Mix pointer and value movement differ");
        slider.mouseUp(mouse(slider, to, from, true));
        require(slider.getMouseDragSensitivity() == originalSensitivity, "Drag sensitivity leaked after gesture");
    }
    std::cout << "Main and inline wet cable splicing, wet-pin drags, legacy visibility, undo, recall and 1:1 fat Mix drag passed\n";
}
}
