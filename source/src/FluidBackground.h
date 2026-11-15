#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>

namespace vocalstack {

inline const juce::Identifier vocalstackAnimationTimeProperty{"vocalstackAnimationTime"};
inline const juce::Identifier vocalstackAnimationTimeOverrideProperty{"vocalstackAnimationTimeOverride"};

// Overrides anywhere above the caller win over the editor's ticking phase.
// Without an editor (or an explicit phase), snapshots always use time zero.
inline double fluidAnimationSeconds(const juce::Component& component) {
    const auto finiteSeconds = [](const juce::var& value) {
        const double seconds = static_cast<double>(value);
        return std::isfinite(seconds) ? juce::jmax(0.0, seconds) : 0.0;
    };
    for (auto* current = &component; current != nullptr; current = current->getParentComponent())
        if (current->getProperties().contains(vocalstackAnimationTimeOverrideProperty))
            return finiteSeconds(current->getProperties()[vocalstackAnimationTimeOverrideProperty]);
    for (auto* current = &component; current != nullptr; current = current->getParentComponent())
        if (current->getProperties().contains(vocalstackAnimationTimeProperty))
            return finiteSeconds(current->getProperties()[vocalstackAnimationTimeProperty]);
    return 0.0;
}

// Integrate elapsed visible time, not absolute time * speed: edits never jump phase.
class DecorativeMotion {
public:
    void advance(double now, bool visible, double percent) noexcept {
        if (visible && last >= 0 && now >= last)
            phase += (now - last) * std::clamp(std::isfinite(percent) ? percent : 3.0, 0.0, 100.0) / 3.0;
        last = visible ? now : -1.0;
    }
    double seconds() const noexcept { return phase; }
    void pause() noexcept { last = -1; }
private:
    double phase = 0, last = -1;
};

// GUI-thread only. Own one per surface; no timers or component dependencies.
inline constexpr double proceduralAnimationSpeed = 1.2;

class FluidBackground {
public:
    enum class Material { background, control, fatBar };
    struct WorkCounters { uint64_t paints = 0, cacheHits = 0, sharedHits = 0, rasterizations = 0, pixelsGenerated = 0, clipCulls = 0; };
    static WorkCounters workCounters() { return counters(); }
    static void resetWorkCounters() { counters() = {}; }
    static constexpr float backgroundVariation = .8f;
    explicit FluidBackground(Material material = Material::background) : material(material) {}
    void paint(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour base,
               juce::Colour accent, double seconds, float contrast = 1.f, int longest = 80) {
        if (bounds.isEmpty()) return;
        ++counters().paints;
        if (!g.clipRegionIntersects(bounds)) { ++counters().clipCulls; return; }
        // A fixed grid makes the physical pixels grow with the component, while
        // aspect adjustment keeps them approximately square in small FX cards.
        longest = juce::jmax(1, longest);
        const int width = bounds.getWidth() >= bounds.getHeight() ? longest
            : juce::jmax(1, juce::roundToInt(double(longest) * bounds.getWidth() / bounds.getHeight()));
        const int height = bounds.getHeight() >= bounds.getWidth() ? longest
            : juce::jmax(1, juce::roundToInt(double(longest) * bounds.getHeight() / bounds.getWidth()));
        const double frame = std::floor((std::isfinite(seconds) ? juce::jlimit(0.0, 1.0e9, seconds) : 0.0) * 10.0);
        if (image.isNull() || image.getWidth() != width || image.getHeight() != height
            || frame != cachedFrame || base != cachedBase || accent != cachedAccent || contrast != cachedContrast) {
            cachedFrame = frame;
            cachedBase = base;
            cachedAccent = accent;
            cachedContrast = contrast;
            if (!loadShared(width, height)) {
            // Shared tiles are immutable: never mutate an image retained by a
            // different surface or an earlier deterministic animation phase.
            image = juce::Image(juce::Image::RGB, width, height, false);
            ++counters().rasterizations;
            counters().pixelsGenerated += uint64_t(width) * uint64_t(height);
            const double phase = frame * 0.0025 * proceduralAnimationSpeed;
            juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
            // Keep the original dark surface dominant. A desaturated, brightness-
            // matched accent gives only a faint theme tint, never a vivid wash.
            const float brightness = base.getBrightness();
            const auto low = base.withBrightness(brightness * .78f);
            const auto tint = accent.withSaturation(juce::jmin(.18f, accent.getSaturation()))
                .withBrightness(juce::jmin(1.f, brightness * 1.55f + .012f));
            const auto high = base.withBrightness(juce::jmin(1.f, brightness * 1.22f + .012f))
                .interpolatedWith(tint, .12f);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    const double nx = double(x) / width, ny = double(y) / height;
                    // Shared low-frequency geology, not independent per-texel grain.
                    // Stepped cells form mineral patches; broad strata drift slowly
                    // beneath them. Five tones preserve chunky nearest-neighbour edges.
                    const double broad = noise(nx * 3.0 + phase * .35, ny * 3.0, phase * .25);
                    const double cell = noise(std::floor(nx * 16.0) / 3.0,
                                              std::floor(ny * 16.0) / 3.0, 7.0);
                    const double band = .5 + .5 * std::sin(ny * 9.0 + nx * 2.0 + broad * 3.0 + phase);
                    const double field = .55 * broad + .30 * cell + .15 * band;
                    const float blend = float(std::round(juce::jlimit(0.0, 1.0,
                        .5 + (field - .5) * 1.8) * 4.0) / 4.0);
                    auto tone = low.interpolatedWith(high, blend);
                    // Deep mineral shadows rather than a white lift of the whole bar.
                    if (material == Material::fatBar)
                        tone = tone.withMultipliedBrightness(.35f + .65f * blend);
                    auto colour = base.interpolatedWith(tone, contrast);
                    // Fixed rare seeds, with a slow continuous envelope: no frame-wise sparkle.
                    // Keep one seed even on the small 24-texel card grids.
                    if ((x == width * 2 / 3 && y == height * 3 / 4) || lattice(x, y, 73) > .9992) {
                        const float gleam = float(std::pow(.5 + .5 * std::sin(frame / 10.0 * .12 + lattice(x, y, 19) * 6.283), 2));
                        colour = colour.interpolatedWith(accent.interpolatedWith(juce::Colours::white, .20f), .62f * gleam * contrast);
                    }
                    // Attenuate the completed field and glints together, not their motion or seeds.
                    if (material == Material::background)
                        colour = base.interpolatedWith(colour, backgroundVariation);
                    pixels.setPixelColour(x, y, colour);
                }
            saveShared();
            }
        }
        else ++counters().cacheHits;
        juce::Graphics::ScopedSaveState state(g);
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImage(image, bounds.toFloat());
    }

private:
    static WorkCounters& counters() { static thread_local WorkCounters value; return value; }
    struct SharedTile {
        juce::Image image;
        Material material = Material::background;
        juce::Colour base, accent;
        double frame = -1;
        float contrast = 0;
        uint64_t age = 0;
    };
    struct SharedTiles { std::array<SharedTile, 128> tiles; uint64_t clock = 0; };
    static SharedTiles& sharedTiles() { static thread_local SharedTiles value; return value; }
    bool loadShared(int width, int height) {
        auto& cache = sharedTiles();
        for (auto& tile : cache.tiles)
            if (tile.age && tile.material == material && tile.frame == cachedFrame && tile.base == cachedBase
                && tile.accent == cachedAccent && tile.contrast == cachedContrast
                && tile.image.getWidth() == width && tile.image.getHeight() == height) {
                image = tile.image; tile.age = ++cache.clock; ++counters().sharedHits;
                return true;
            }
        return false;
    }
    void saveShared() {
        auto& cache = sharedTiles();
        auto* oldest = &cache.tiles.front();
        for (auto& tile : cache.tiles) if (tile.age < oldest->age) oldest = &tile;
        *oldest = {image, material, cachedBase, cachedAccent, cachedFrame, cachedContrast, ++cache.clock};
    }
    static double lattice(int x, int y, int z) noexcept {
        std::uint32_t h = std::uint32_t(x) * 374761393u + std::uint32_t(y) * 668265263u
                        + std::uint32_t(z) * 2246822519u + 1013904223u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return double(h & 0x00ffffffu) / 16777215.0;
    }
    static double noise(double x, double y, double z) noexcept {
        // Wrap extremely long/injected phases before integer lattice conversion.
        x = std::fmod(x, 4096.0); y = std::fmod(y, 4096.0); z = std::fmod(z, 4096.0);
        const int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
        const auto smooth = [](double v) { return v * v * v * (v * (v * 6.0 - 15.0) + 10.0); };
        const auto mix = [](double a, double b, double t) { return a + (b - a) * t; };
        const double u = smooth(x - ix), v = smooth(y - iy), w = smooth(z - iz);
        return mix(mix(mix(lattice(ix, iy, iz), lattice(ix + 1, iy, iz), u),
                       mix(lattice(ix, iy + 1, iz), lattice(ix + 1, iy + 1, iz), u), v),
                   mix(mix(lattice(ix, iy, iz + 1), lattice(ix + 1, iy, iz + 1), u),
                       mix(lattice(ix, iy + 1, iz + 1), lattice(ix + 1, iy + 1, iz + 1), u), v), w);
    }

    Material material;
    juce::Image image;
    double cachedFrame = -1.0;
    juce::Colour cachedBase, cachedAccent;
    float cachedContrast = -1.f;
};

// Look-and-feels are shared by differently sized buttons. Keep a cache per
// live surface rather than rebuilding one image on every button paint.
class ButtonFluidBackground {
public:
    void paint(juce::Graphics& g, juce::Component& button, juce::Rectangle<int> bounds,
               juce::Colour base, juce::Colour accent) {
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [](const Entry& entry) { return entry.button == nullptr; }), entries.end());
        auto found = std::find_if(entries.begin(), entries.end(),
            [&](const Entry& entry) { return entry.button.getComponent() == &button; });
        if (found == entries.end()) {
            entries.push_back({&button});
            found = std::prev(entries.end());
        }
        found->background.paint(g, bounds, base, accent, fluidAnimationSeconds(button), .55f);
    }
private:
    struct Entry {
        juce::Component::SafePointer<juce::Component> button;
        FluidBackground background{FluidBackground::Material::control};
    };
    std::vector<Entry> entries;
};

} // namespace vocalstack
