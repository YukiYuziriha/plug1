#include "PluginEditor.h"
#include "ProcessorRoutingState.h"
#include <iostream>
#include <stdexcept>

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
void checkPrimaryGrid(juce::Component& card, bool creative) {
    auto* controls = find(card, "mainBusPrimaryControls");
    expect(controls != nullptr, "primary control grid exists");
    expect(dynamic_cast<juce::Viewport*>(controls) == nullptr, "primary grid does not scroll sideways");
    const auto count = controls->getNumChildComponents();
    expect(count > 0 && count <= 8, "compact primary control count");
    if (creative) expect(count >= 5, "creative card exposes useful primary controls");
    for (int i = 0; i < count; ++i) {
        auto* knob = dynamic_cast<ParameterKnob*>(controls->getChildComponent(i));
        expect(knob != nullptr, "every primary control is a shared rotary knob");
        expect(knob->slider.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag, "rack controls remain rotary");
        expect(knob->slider.getTextBoxPosition() == juce::Slider::TextBoxBelow, "numeric value is below knob");
        expect(knob->getWidth() >= 80 && knob->getWidth() <= 96, "compact knob width");
        expect(controls->getLocalBounds().contains(knob->getBounds()), "responsive grid never clips a control");
        const auto layout = knob->slider.getLookAndFeel().getSliderLayout(knob->slider);
        expect(layout.sliderBounds.getWidth() >= 60 && layout.sliderBounds.getHeight() >= 50, "rotary retains usable drawing area");
        expect(std::abs(layout.sliderBounds.getCentreX() - layout.textBoxBounds.getCentreX()) <= 1, "knob and numeric value share centre");
        for (int j = 0; j < i; ++j)
            expect(!knob->getBounds().intersects(controls->getChildComponent(j)->getBounds()), "grid controls do not overlap");
        if (i > 0) {
            const auto* previous = controls->getChildComponent(i - 1);
            if (previous->getY() == knob->getY()) expect(knob->getX() - previous->getX() == 88, "no excessive horizontal gaps");
        }
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
    MainBusFxRackPanel rack(owner); rack.setBounds(0, 0, 904, 800);
    for (const auto& node : graph.nodes) if (mainBusFx::eligible(node.type)) {
        auto* card = find(rack, "mainBusFx" + juce::String(node.id)); expect(card != nullptr, "effect card exists");
        checkPrimaryGrid(*card, isCreativeEffect(node.type));
    }
    auto* granular = find(rack, "mainBusFx100"); auto* reverb = find(rack, "mainBusFx101");
    auto* details = dynamic_cast<juce::Button*>(find(*reverb, "mainBusFxDetails"));
    expect(details && details->isEnabled(), "full shared visual/details available");
    const auto before = processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString();
    const std::array<const char*, 3> skins{"device", "neon", "spectral"};
    for (int skin = 0; skin < 3; ++skin) {
        owner.setSkin(skin); rack.getProperties().set(vocalstackThemeProperty, skin); rack.setNeon(skin == 1);
        rack.setSize(904, 800);
        writeSnapshot(*granular, output, juce::String("rack-granular-") + skins[size_t(skin)]);
        writeSnapshot(*reverb, output, juce::String("rack-reverb-") + skins[size_t(skin)]);
        details->onClick();
        CreativeEffectPanel* full = nullptr;
        for (auto* child : reverb->getChildren()) if (auto* panel = dynamic_cast<CreativeEffectPanel*>(child)) full = panel;
        expect(full && full->isVisible(), "Details shows the existing full visual panel");
        expect(find(*full, creativeParameterId(2, size_t(CreativeParameter::reverbDiffusion))) != nullptr, "secondary controls remain editable in Details");
        writeSnapshot(*reverb, output, juce::String("rack-reverb-details-") + skins[size_t(skin)]);
        details->onClick(); expect(!full->isVisible(), "Details collapses without destroying controls");
        details->onClick();
        expect(full->isVisible() && full->getParentComponent() == reverb, "Details reuses existing panel and attachments");
        details->onClick();
        rack.setSize(360, 800);
        checkPrimaryGrid(*granular, true); checkPrimaryGrid(*reverb, true);
        expect(find(rack, "mainBusFx100") == granular, "responsive layout preserves card objects");
        writeSnapshot(*granular, output, juce::String("rack-granular-narrow-") + skins[size_t(skin)]);
    }
    expect(processorRouting::encode(owner.getRoutingGraph(), "Graph").toXmlString() == before, "layout and Details do not publish topology");
    std::cout << "Rack card PNGs: " << output.getFullPathName() << '\n';
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
