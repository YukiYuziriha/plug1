#include "UtilityIntegrationTests.h"

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        if (!vocalstack::utilityIntegrationTests::run()) return 1;
        std::cout << "PASS utility panel scheduling, redraw, narrow layout and wheel\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL utility panel: " << error.what() << '\n';
        return 1;
    }
}
