#pragma once
#include "FactoryFxUiTests.h"
#include "FluidBackgroundTests.h"
#include "GuiTestDisplay.h"

namespace vocalstack::coordinatedPreviewTests {
namespace ui = toneUiTests;
inline void monitorCacheAndClipping() {
    auto owner = std::make_unique<Processor>();
    auto first = toneui::SharedFxSpectrum::acquire(*owner, 1);
    auto second = toneui::SharedFxSpectrum::acquire(*owner, 1);
    auto independent = toneui::SharedFxSpectrum::acquire(*owner, 2);
    ui::require(first == second && first != independent, "FFT cache is not shared per processor/slot");
    FxMonitorFrame frame; frame.sequence = 1; frame.epoch = 1; frame.nodeId = 100;
    frame.input[0][0] = frame.output[1][0] = .5f;
    first->update(frame); second->update(frame);
    ui::require(first->analyses == 1, "Duplicate monitor view recomputed the same FFT sequence");
    ++frame.sequence; second->update(frame);
    ui::require(first->analyses == 2, "New monitor publication skipped FFT analysis");
    ++frame.epoch; first->update(frame);
    ui::require(first->analyses == 3, "Monitor epoch did not invalidate FFT cache");
    toneui::SharedFxSpectrum stale;
    for (size_t i = 0; i < frame.sampleCount; ++i)
        frame.input[0][i] = frame.input[1][i] = float(std::sin(6.283185307179586 * 64 * double(i) / frame.sampleCount));
    stale.update(frame, 1);
    frame.input = {}; frame.output = {}; ++frame.sequence;
    stale.update(frame, 1001);
    for (float db : stale.input.levelsDbFS()) ui::near(db, -96, "Stalled shared FFT resurrected old peaks");
    juce::Component root, clipped, view;
    root.setSize(320, 240); root.addAndMakeVisible(clipped); clipped.setBounds(20, 20, 100, 100);
    clipped.addAndMakeVisible(view); view.setBounds(0, 0, 80, 80);
    guiTestDisplay::enumerate(); guiTestDisplay::attach(root);
    ui::require(toneui::monitorIntersectsAncestors(view), "Visible monitor intersection rejected");
    view.setTopLeftPosition(110, 0);
    ui::require(view.isShowing() && !toneui::monitorIntersectsAncestors(view), "Off-viewport showing monitor retained capture");
    view.setTopLeftPosition(90, 0);
    ui::require(toneui::monitorIntersectsAncestors(view), "Partially clipped monitor lost capture");
    clipped.setTopLeftPosition(400, 0);
    ui::require(!toneui::monitorIntersectsAncestors(view), "Ancestor viewport intersection ignored");
    root.removeFromDesktop();
}
inline void motionState() {
    DecorativeMotion clock;
    clock.advance(10, true, 3); clock.advance(13, true, 3);
    ui::near(float(clock.seconds()), 3, "3% must retain original speed");
    clock.advance(14, true, 0);
    ui::near(float(clock.seconds()), 3, "0% must freeze");
    clock.advance(17, true, 100);
    ui::near(float(clock.seconds()), 103, "100% must be exactly percent/3");
    clock.advance(18, false, 100); clock.advance(1000, true, 100);
    ui::near(float(clock.seconds()), 103, "Hidden time must pause");
    auto owner = std::make_unique<Processor>();
    ui::near(float(owner->getMotionPercent()), 3, "Motion default");
    owner->setMotionPercent(17.25);
    juce::MemoryBlock state; owner->getStateInformation(state);
    auto copy = std::make_unique<Processor>(); copy->setStateInformation(state.getData(), int(state.getSize()));
    ui::near(float(copy->getMotionPercent()), 17.25f, "Binary Motion recall");
    ui::require(copy->getParameters().size() == 2457, "Motion must not append a host parameter");
    auto legacy = juce::parseXML(owner->settingsText()); legacy->removeAttribute("motionPercent");
    ui::require(copy->pasteSettings(legacy->toString()), "Legacy state restore");
    ui::near(float(copy->getMotionPercent()), 3, "Missing Motion legacy default");
    std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
    auto& slider = ui::slider(*editor, "decorativeMotion");
    ui::require(slider.getSliderStyle() == juce::Slider::LinearHorizontal && bool(slider.getProperties()["vocalstackTexturedBar"]), "Header Motion must be fat square slider");
    slider.setValue(0, juce::sendNotificationSync);
    ui::near(float(owner->getMotionPercent()), 0, "Motion editor setting bridge");
    ui::passive(*editor);
    owner->setMotionPercent(42);
    PresetManager manager(*owner); juce::ValueTree patch;
    ui::require(manager.factoryPatch(0, patch).wasOk(), "Factory presentation isolation fixture");
    ui::require(manager.load({"Dark Lead", {}, 0}).wasOk(), "Factory presentation isolation apply");
    ui::near(float(owner->getMotionPercent()), 42, "Sound preset must preserve Motion preference");
}
inline void sparseGroups() {
    auto owner = std::make_unique<Processor>();
    for (auto kind : {CreativeKind::delay, CreativeKind::reverb, CreativeKind::compressor, CreativeKind::bitcrusher, CreativeKind::granular, CreativeKind::convolution, CreativeKind::saturator}) {
        CreativeEffectPanel panel(*owner, 1, kind); panel.setSize(760, 620);
        ui::require(ui::slider(panel, "fx1Mix").getSliderStyle() == juce::Slider::LinearHorizontal, "Blend singleton must remain thick");
        ui::visibleLayout(panel); ui::passive(panel);
    }
    ChoralePanel chorus(*owner); chorus.setSize(470, 64);
    ui::require(ui::slider(chorus, "choraleMotion").getSliderStyle() == juce::Slider::LinearHorizontal,
                "Chorale Motion + Mix must both be thick sliders");
    UtilityEffectPanel gain(*owner, 1, UtilityKind::gain); gain.setSize(400, 620);
    ui::require(ui::slider(gain, utilityParameterId(1, size_t(UtilityParameter::gainDb))).getSliderStyle() == juce::Slider::LinearHorizontal, "Gain + dry/wet two-control group must use thick sliders");
}
inline void bipolarCaption() {
    auto owner = std::make_unique<Processor>();
    ParameterKnob control(*owner, utilityParameterId(1, size_t(UtilityParameter::gainDb)), "Gain", "dB");
    control.setLinear(true); control.setSize(380, 44); control.setNeon(false);
    control.slider.setValue(0, juce::dontSendNotification);
    auto* label = ui::find<juce::Label>(control.slider, [](auto&) { return true; });
    ui::require(label != nullptr, "Bipolar fat caption missing");
    juce::Image actual(juce::Image::ARGB, label->getWidth(), label->getHeight(), true), expected(actual.createCopy());
    { juce::Graphics g(actual); static_cast<juce::Component*>(label)->paint(g); }
    { juce::Graphics g(expected); g.setFont(label->getFont()); g.setColour(themePalette(control.slider).text);
      g.drawText("Gain " + label->getText().trim(), label->getLocalBounds().reduced(2, 0), juce::Justification::centred); }
    ui::require(fluidBackgroundTests::identical(actual, expected), "Zero bipolar fill darkened its passive text over an unfilled rail");
}
inline void contentAnchoring() {
    auto owner = std::make_unique<Processor>();
    for (int theme = 0; theme < 3; ++theme) {
        CreativeEffectPanel creative(*owner, 1, CreativeKind::reverb);
        UtilityEffectPanel utility(*owner, 1, UtilityKind::gate);
        ToneEffectPanel tone(*owner, 1, NodeType::waveshaper);
        for (juce::Component* panel : {static_cast<juce::Component*>(&creative), static_cast<juce::Component*>(&utility), static_cast<juce::Component*>(&tone)}) {
            panel->getProperties().set(vocalstackThemeProperty, theme);
            panel->getProperties().set(vocalstackAnimationTimeOverrideProperty, 33.3);
            panel->setSize(250, 200);
            auto* viewport = ui::find<juce::Viewport>(*panel, [](auto&) { return true; });
            ui::require(viewport != nullptr, "Content anchoring viewport missing");
            const auto backgroundOnly = [&] {
                juce::Image image(juce::Image::RGB, 250, 200, true); juce::Graphics g(image);
                panel->paint(g); return image;
            };
            viewport->setViewPosition(0, 0); const auto before = backgroundOnly();
            viewport->setViewPosition(0, 37); const auto after = backgroundOnly();
            const int offset = viewport->getViewPositionY();
            ui::require(offset > 0, "Content anchoring fixture must scroll");
            for (int y = 0; y < 200 - offset; ++y) for (int x = 0; x < viewport->getViewWidth(); ++x)
                ui::require(after.getPixelAt(x, y) == before.getPixelAt(x, y + offset), "Inspector background reseeded or stayed viewport-fixed on scroll");
        }
    }
}
inline void reactiveAndRender(bool render) {
    for (int theme = 0; theme < (render ? 3 : 1); ++theme) for (auto kind : {CreativeKind::convolution, CreativeKind::saturator}) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme); owner->setMotionPercent(0);
        updateUiTests::install(*owner, kind == CreativeKind::convolution ? NodeType::convolution : NodeType::saturator);
        utilityIntegrationTests::prepare(*owner);
        ui::write(*owner, "fx1Mix", 100);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        editor->addToDesktop(0); editor->setVisible(true); ui::action(*editor, "Routing");
        CreativeEffectPanel panel(*owner, 1, kind);
        panel.setLookAndFeel(&editor->getLookAndFeel()); panel.getProperties().set(vocalstackThemeProperty, theme);
        panel.getProperties().set(vocalstackAnimationTimeOverrideProperty, 33.3);
        panel.setNeon(theme == 1); panel.setSize(840, 460); panel.addToDesktop(0); panel.setVisible(true);
        auto* visual = ui::find<CreativeEffectVisual>(panel, [](auto&) { return true; });
        visual->refreshAt(0);
        ui::require(!visual->hasMeasuredActivity(), "Idle graph must not invent live audio");
        const juce::String stem = juce::String(kind == CreativeKind::convolution ? "convolution-live-" : "saturator-live-") + updateUiTests::themeName(theme);
        juce::Image sheet(juce::Image::RGB, 840 * 3, 460 * 2, true);
        juce::Graphics montage(sheet);
        uint64_t previous = 0;
        juce::MidiBuffer events; juce::AudioBuffer<float> audio(2, 256);
        int position = 0;
        for (int frame = 0; frame < 12; ++frame) {
            for (int block = 0; block < 30; ++block) {
                for (int i = 0; i < 256; ++i, ++position) {
                    // A real gated two-tone burst through Processor::processBlock,
                    // not injected snapshots. Later frames capture the real IR tail.
                    const float envelope = position < 24000 ? (kind == CreativeKind::saturator ? .08f + .65f * float(position) / 24000.f : .35f) : 0.f;
                    audio.setSample(0, i, envelope * float(std::sin(position * .041) + .3 * std::sin(position * .077)));
                    audio.setSample(1, i, envelope * float(std::sin(position * .053)));
                }
                owner->processBlock(audio, events);
            }
            const double ms = 1000.0 * position / 48000;
            visual->refreshAt(ms);
            ui::require(visual->hasMeasuredActivity() && visual->measuredSequence() > previous, "Factory graph did not consume monotonically published real DSP activity");
            previous = visual->measuredSequence();
            if (frame < 3) ui::require(visual->measuredWetRms() > 0, "Real burst has no wet energy");
            if (render) {
                if (auto* actual = ui::find<CreativeEffectVisual>(*editor, [](auto& c) { return c.isShowing(); })) actual->refreshAt(ms);
                ui::screenshot(*editor, stem + "-full-frame-" + juce::String(frame).paddedLeft('0', 2) + ".png");
                const auto image = panel.createComponentSnapshot(panel.getLocalBounds());
                ui::screenshot(panel, stem + "-frame-" + juce::String(frame).paddedLeft('0', 2) + ".png");
                if (frame % 2 == 0) montage.drawImageAt(image, (frame / 2 % 3) * 840, (frame / 6) * 460);
            }
        }
        visual->refreshAt(5000); ui::require(!visual->hasMeasuredActivity(), "Stopped stream must clear, not hold stale audio");
        visual->refreshAt(6000); ui::require(!visual->hasMeasuredActivity(), "Stale stream reanimated without a new publication");
        owner->reset(); visual->refreshAt(6100);
        ui::require(!visual->hasMeasuredActivity(), "Reset must invalidate live activity");
        if (render) {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(stem + "-contact.png");
            juce::FileOutputStream stream(file); juce::PNGImageFormat().writeImageToStream(sheet, stream);
            ui::screenshot(*editor, stem + "-editor.png");
        }
        panel.setLookAndFeel(nullptr);
    }
}
inline void prototypeScreenshots() {
    auto owner = std::make_unique<Processor>();
    for (int theme = 0; theme < 3; ++theme) {
        juce::Component fixture; fixture.getProperties().set(vocalstackThemeProperty, theme);
        fixture.getProperties().set(vocalstackAnimationTimeOverrideProperty, 33.3);
        ParameterKnob square(*owner, "fx1saturator_drive", "Square prototype", "dB");
        ParameterKnob rotary(*owner, "fx1saturator_drive", "Existing rotary", "dB");
        ParameterKnob bar(*owner, "fx1saturator_drive", "Existing slider", "dB");
        ui::write(*owner, "fx1saturator_drive", 24);
        square.slider.getProperties().set("vocalstackSquarePrototype", true);
        bar.setLinear(true);
        fixture.addAndMakeVisible(square); fixture.addAndMakeVisible(rotary); fixture.addAndMakeVisible(bar);
        square.setBounds(10, 10, 150, 130); rotary.setBounds(170, 10, 150, 130); bar.setBounds(330, 45, 250, 44);
        square.setNeon(theme == 1); rotary.setNeon(theme == 1); bar.setNeon(theme == 1);
        fixture.setSize(590, 150);
        ui::screenshot(fixture, "square-prototype-" + updateUiTests::themeName(theme) + ".png");
    }
}
inline void run() { monitorCacheAndClipping(); motionState(); sparseGroups(); bipolarCaption(); contentAnchoring(); reactiveAndRender(false); }
inline void motionScreenshots() {
    for (int theme = 0; theme < 3; ++theme) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        editor->addToDesktop(0); editor->setVisible(true); ui::action(*editor, "Stack");
        auto& motion = ui::slider(*editor, "decorativeMotion");
        DecorativeMotion clock; clock.advance(0, true, 100);
        for (int phase = 0; phase < 4; ++phase) {
            clock.advance(double(phase), true, 100);
            motion.setValue(100, juce::sendNotificationSync);
            editor->getProperties().set(vocalstackAnimationTimeOverrideProperty, clock.seconds());
            ui::screenshot(*editor, "motion-100-" + updateUiTests::themeName(theme) + "-phase-" + juce::String(phase) + ".png");
        }
        UtilityEffectPanel gain(*owner, 1, UtilityKind::gain);
        gain.setLookAndFeel(&editor->getLookAndFeel()); gain.getProperties().set(vocalstackThemeProperty, theme);
        gain.getProperties().set(vocalstackAnimationTimeOverrideProperty, 33.3);
        gain.setNeon(theme == 1); gain.setSize(400, 240);
        ui::screenshot(gain, "two-sliders-scrollbar-" + updateUiTests::themeName(theme) + ".png");
        gain.setLookAndFeel(nullptr);
    }
}
inline void screenshots() { reactiveAndRender(true); prototypeScreenshots(); motionScreenshots(); updateUiTests::screenshots(); factoryFxUiTests::screenshots(); }
}
