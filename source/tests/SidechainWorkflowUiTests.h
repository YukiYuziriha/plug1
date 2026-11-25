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
    for (int theme = 0; theme < 3; ++theme) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        auto graph = mainBusFx::add(defaultGraph(), NodeType::granular).graph;
        juce::String error;
        require(owner->setRoutingGraph(graph, error), "Granular UI graph rejected");
        auto layout = owner->getRoutingLayout();
        layout.removeAllChildren(nullptr); layout.removeProperty("selectedVoiceSlot", nullptr);
        layout.setProperty("selectedNode", 100, nullptr); layout.setProperty("zoom", .8f, nullptr);
        layout.setProperty("inspectorVisible", true, nullptr);
        juce::ValueTree position("NODE"); position.setProperty("id", 100, nullptr);
        position.setProperty("x", 40, nullptr); position.setProperty("y", 40, nullptr);
        position.setProperty("expanded", true, nullptr); layout.addChild(position, -1, nullptr);
        owner->setRoutingLayout(layout);
        RoutingPanel panel(*owner); panel.setNeon(theme == 1);
        fitAndAttach(panel, 1200, 950);
        auto* pocket = dynamic_cast<juce::Button*>(find(panel, "granularWetPocket"));
        require(pocket && pocket->isShowing(), "Granular node has no visible wet-pocket action");
        pocket->onClick();
        const auto updated = owner->getRoutingGraph();
        const auto* child = mainBusFx::find(updated, 101);
        require(child && child->type == NodeType::delay && child->wetOwner == 100, "Node action did not create owned wet Delay");
        auto* delayMix = dynamic_cast<juce::Slider*>(find(panel, "fx2Mix"));
        require(delayMix && delayMix->isShowing(), "Pocket action did not reveal Delay controls");
        require(delayMix->getValue() == 100, "New wet insert must default fully processed");
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("source/build/wet-pocket-" + juce::String(theme) + ".png");
        if (auto stream = file.createOutputStream()) {
            stream->setPosition(0); stream->truncate();
            juce::PNGImageFormat{}.writeImageToStream(panel.createComponentSnapshot(panel.getLocalBounds()), *stream);
        }
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
    std::cout << "Node wet-pocket creation/navigation and 1:1 fat Mix drag passed in three themes\n";
}
}
