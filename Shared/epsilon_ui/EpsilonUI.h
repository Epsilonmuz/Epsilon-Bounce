#pragma once
// epsilon_ui — the shared Epsilon Sounds look (design system v1).
// One file, header-only, copied unchanged into every plugin repo under Shared/epsilon_ui/.
// Two lines, one system: Tier::Muzi (premium, near-black + brushed gold) and Tier::Epsilon
// (main line, graphite + platinum). Everything else — layout, controls, the "live" champagne,
// meters — is shared.
//
// Fonts: Barlow Semi Condensed 500/600/700 from the plugin's BinaryData
// (BarlowSemiCondensedMedium_ttf, BarlowSemiCondensedSemiBold_ttf, BarlowSemiCondensedBold_ttf).
// Brand art (hallmark + wordmarks) lives in EpsilonBrandArt.h as SVG paths.
//
// Performance (Intel Mac rule): static chrome is painted into a cached image once per size;
// only knobs, meters and the plugin's hero repaint.

#include <JuceHeader.h>
#include "EpsilonPalette.h"
#include "EpsilonBrandArt.h"

namespace epsilon_ui
{
//==============================================================================
enum class Weight { Medium, SemiBold, Bold };

inline juce::Font font(Weight w, float height)
{
    static juce::Typeface::Ptr faces[3] = {
        juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiCondensedMedium_ttf, BinaryData::BarlowSemiCondensedMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiCondensedSemiBold_ttf, BinaryData::BarlowSemiCondensedSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiCondensedBold_ttf, BinaryData::BarlowSemiCondensedBold_ttfSize),
    };
    return juce::Font(juce::FontOptions(faces[(int) w]).withHeight(height));
}
// Type styles (tokens.json): section-label, control-label, value, value-hero, body, caption
inline juce::Font sectionLabel() { return font(Weight::Bold, 11.0f).withExtraKerningFactor(0.14f); }
inline juce::Font controlLabel() { return font(Weight::SemiBold, 12.5f).withExtraKerningFactor(0.04f); }
inline juce::Font valueFont()    { return font(Weight::SemiBold, 13.0f); }
inline juce::Font valueHero()    { return font(Weight::SemiBold, 24.0f); }
inline juce::Font body()         { return font(Weight::Medium, 14.0f); }
inline juce::Font caption()      { return font(Weight::Medium, 11.5f); }

//==============================================================================
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel(Tier t) : tier(t), pal(palette(t))
    {
        setColour(juce::ResizableWindow::backgroundColourId, pal.bg);
        setColour(juce::Label::textColourId, pal.ink);
        setColour(juce::TextButton::buttonColourId, pal.raised);
        setColour(juce::TextButton::buttonOnColourId, pal.live);
        setColour(juce::TextButton::textColourOffId, pal.inkMuted);
        setColour(juce::TextButton::textColourOnId, pal.liveInk);
        setColour(juce::ComboBox::backgroundColourId, pal.bg);
        setColour(juce::ComboBox::outlineColourId, pal.line);
        setColour(juce::ComboBox::textColourId, pal.ink);
        setColour(juce::ComboBox::arrowColourId, pal.inkMuted);
        setColour(juce::PopupMenu::backgroundColourId, pal.raised);
        setColour(juce::PopupMenu::textColourId, pal.ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, pal.live);
        setColour(juce::PopupMenu::highlightedTextColourId, pal.liveInk);
        setColour(juce::Slider::textBoxTextColourId, pal.ink);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TooltipWindow::backgroundColourId, pal.raised);
        setColour(juce::TooltipWindow::textColourId, pal.ink);
        setColour(juce::TooltipWindow::outlineColourId, pal.line);
    }

    const Palette& colours() const { return pal; }
    Tier getTier() const { return tier; }

    // Knob: raised body, trim ring, ink pointer, live value arc from the default position.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto box = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(2.0f);
        const auto d = juce::jmin(box.getWidth(), box.getHeight());
        const auto c = box.getCentre();
        const auto rArc = d * 0.5f - size::arc;
        const auto rBody = rArc * 0.76f;
        const auto ang = a0 + pos * (a1 - a0);

        juce::Path track;
        track.addCentredArc(c.x, c.y, rArc, rArc, 0.0f, a0, a1, true);
        g.setColour(pal.line);
        g.strokePath(track, juce::PathStrokeType(size::arc, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto range = s.getRange();
        // one-sided knobs fill from the bottom of their range; bipolar ones (range crosses 0) from 0
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        const auto origin = bipolar ? 0.0 : range.getStart();
        const auto defPos = (float) s.valueToProportionOfLength(origin);
        const auto aDef = a0 + defPos * (a1 - a0);
        if (std::abs(ang - aDef) > 0.01f && s.isEnabled())
        {
            juce::Path arc;
            arc.addCentredArc(c.x, c.y, rArc, rArc, 0.0f, juce::jmin(aDef, ang), juce::jmax(aDef, ang), true);
            g.setColour(pal.live);
            g.strokePath(arc, juce::PathStrokeType(size::arc, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // body + shadow
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillEllipse(juce::Rectangle<float>(rBody * 2.0f, rBody * 2.0f).withCentre(c.translated(0.0f, 2.0f)));
        g.setColour(pal.raised);
        g.fillEllipse(juce::Rectangle<float>(rBody * 2.0f, rBody * 2.0f).withCentre(c));
        g.setColour(pal.trim.withAlpha(s.isEnabled() ? 1.0f : 0.4f));
        g.drawEllipse(juce::Rectangle<float>(rBody * 2.0f, rBody * 2.0f).withCentre(c).reduced(0.75f), 1.5f);

        const auto tip = c.getPointOnCircumference(rBody * 0.78f, ang);
        const auto tail = c.getPointOnCircumference(rBody * 0.18f, ang);
        g.setColour(pal.ink);
        g.drawLine({ tail, tip }, d > 70.0f ? 3.0f : 2.5f);

        if (s.hasKeyboardFocus(true))
        {
            g.setColour(pal.focus);
            g.drawEllipse(juce::Rectangle<float>(rArc * 2.0f + 4.0f, rArc * 2.0f + 4.0f).withCentre(c), 1.5f);
        }
    }

    // Toggles and segmented switches: off = raised + muted text, on = live fill.
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = b.getToggleState();
        const bool pill = b.getProperties().getWithDefault("pill", false);
        const auto rad = pill ? r.getHeight() * 0.5f : size::radiusMd;
        auto fill = on ? pal.live : pal.raised;
        if (!on && over) fill = fill.brighter(0.08f);
        if (down) fill = fill.darker(0.1f);
        g.setColour(fill);
        g.fillRoundedRectangle(r, rad);
        g.setColour(on ? pal.live : pal.line);
        g.drawRoundedRectangle(r, rad, 1.0f);
        if (b.hasKeyboardFocus(false)) { g.setColour(pal.focus); g.drawRoundedRectangle(r.expanded(1.5f), rad + 1.5f, 1.5f); }
    }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return sectionLabel(); }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont(sectionLabel());
        g.setColour(b.getToggleState() ? pal.liveInk : (b.isEnabled() ? pal.inkMuted : pal.inkMuted.withAlpha(0.4f)));
        g.drawText(b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred, false);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return body(); }
    juce::Font getPopupMenuFont() override { return body(); }
    juce::Font getLabelFont(juce::Label&) override { return controlLabel(); }
    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(0.5f);
        g.setColour(pal.bg); g.fillRoundedRectangle(r, size::radiusMd);
        g.setColour(pal.line); g.drawRoundedRectangle(r, size::radiusMd, 1.0f);
        juce::Path arrow;
        const auto ax = (float) w - 14.0f, ay = h * 0.5f;
        arrow.startNewSubPath(ax - 4.0f, ay - 2.0f); arrow.lineTo(ax, ay + 2.0f); arrow.lineTo(ax + 4.0f, ay - 2.0f);
        g.setColour(pal.inkMuted); g.strokePath(arrow, juce::PathStrokeType(1.5f));
    }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(8, 0, box.getWidth() - 28, box.getHeight());
        label.setFont(getComboBoxFont(box));
    }

private:
    Tier tier;
    Palette pal;
};

//==============================================================================
// Panel helper: surface, radius-lg, hairline top highlight, section title.
inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, const Palette& p, const juce::String& title = {})
{
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(r.translated(0.0f, 1.0f), size::radiusLg);
    g.setColour(p.surface);
    g.fillRoundedRectangle(r, size::radiusLg);
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    g.drawHorizontalLine((int) r.getY() + 1, r.getX() + size::radiusLg, r.getRight() - size::radiusLg);
    g.setColour(p.line);
    g.drawRoundedRectangle(r.reduced(0.5f), size::radiusLg, 1.0f);
    if (title.isNotEmpty())
    {
        g.setFont(sectionLabel());
        g.setColour(p.inkMuted);
        g.drawText(title.toUpperCase(), r.reduced((float) size::space4, 0.0f).withHeight(34.0f), juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// Header: hallmark + wordmark left, the plugin's own controls in getControlArea().
class HeaderBar : public juce::Component
{
public:
    HeaderBar(Tier t, const juce::String& productWord) : tier(t), pal(palette(t)), word(productWord)
    {
        hallmark = brand::hallmark(pal.trim, true);          // 26 px: the solid small-size seal
        if (t == Tier::Muzi) muziWord = brand::muziWordmark(pal.trim);
        setOpaque(true);
    }
    juce::Rectangle<int> getControlArea() const { return getLocalBounds().withTrimmedLeft(wordEnd + size::space6).reduced(size::space3, 8); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(pal.raised);
        g.setColour(pal.line);
        g.drawHorizontalLine(getHeight() - 1, 0.0f, (float) getWidth());
        auto r = getLocalBounds().reduced(size::space4, 0);
        const auto mark = r.removeFromLeft(26).withSizeKeepingCentre(26, 26).toFloat();
        if (hallmark) hallmark->drawWithin(g, mark, juce::RectanglePlacement::centred, 1.0f);
        r.removeFromLeft(size::space3);
        if (tier == Tier::Muzi)
        {
            // Muzi: the drawn serif MUZI + product word in spaced caps, endorsed "by Epsilon Sounds"
            const auto wm = juce::Rectangle<float>((float) r.getX(), 9.0f, 61.0f, 17.5f);
            if (muziWord) muziWord->drawWithin(g, wm, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yTop, 1.0f);
            g.setColour(pal.trim);
            g.setFont(font(Weight::SemiBold, 17.0f).withExtraKerningFactor(0.28f));
            const auto product = word.toUpperCase();
            const auto pw = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), product);
            const int px = (int) wm.getRight() + 10;
            g.drawText(product, juce::Rectangle<int>(px, 7, pw + 8, 22), juce::Justification::centredLeft, false);
            g.setColour(pal.inkMuted);
            g.setFont(caption().withExtraKerningFactor(0.12f));
            g.drawText("BY EPSILON SOUNDS", juce::Rectangle<int>(r.getX(), 30, 200, 12), juce::Justification::centredLeft, false);
            wordEnd = juce::jmax(px + pw + 8, r.getX() + 120);
        }
        else
        {
            g.setColour(pal.trim);
            g.setFont(font(Weight::Bold, 18.0f).withExtraKerningFactor(0.22f));
            const auto text = "EPSILON " + word.toUpperCase();
            const auto lineW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text);
            g.drawText(text, r.withWidth(lineW + 8), juce::Justification::centredLeft, false);
            wordEnd = r.getX() + lineW + 8;
        }
    }

private:
    Tier tier;
    Palette pal;
    juce::String word;
    std::unique_ptr<juce::Drawable> hallmark, muziWord;
    mutable int wordEnd = 260;
};

//==============================================================================
// Footer: version, mode, CPU; warns when the build isn't optimised.
class FooterBar : public juce::Component, private juce::Timer
{
public:
    FooterBar(Tier t, juce::String versionText) : pal(palette(t)), version(std::move(versionText)) { setOpaque(true); startTimerHz(2); }
    std::function<juce::String()> statusText;   // e.g. "Live" / "Preview 140 BPM"
    std::function<float()> cpuPercent;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(pal.bg);
        g.setColour(pal.line);
        g.drawHorizontalLine(0, 0.0f, (float) getWidth());
        auto r = getLocalBounds().reduced(size::space4, 0);
        juce::String s = "v" + version;
        if (statusText) s << "  \xc2\xb7  " << statusText();
        if (cpuPercent) s << "  \xc2\xb7  CPU " << juce::String(cpuPercent(), 1) << "%";
        g.setFont(caption());
        g.setColour(pal.inkMuted);
        g.drawText(juce::String::fromUTF8(s.toRawUTF8()), r, juce::Justification::centredLeft, false);
       #if ! defined(__OPTIMIZE__) && ! defined(NDEBUG) && ! defined(_MSC_VER)
        g.setColour(pal.meterClip);
        g.setFont(font(Weight::Bold, 11.5f));
        g.drawText("SLOW UNOPTIMISED BUILD", r, juce::Justification::centredRight, false);
       #endif
    }

private:
    Palette pal;
    juce::String version;
    void timerCallback() override { repaint(); }
};

//==============================================================================
// Vertical segment meter, 18 segments. Feed it with setLevel() from a timer (<= 30 fps).
class SegmentMeter : public juce::Component
{
public:
    SegmentMeter(Tier t, bool gainReduction = false) : pal(palette(t)), gr(gainReduction) {}
    void setLevel(float dbfs)
    {
        const auto lit = gr ? juce::jlimit(0, segments, (int) std::round(-dbfs / 1.0f))    // 1 dB per segment
                            : juce::jlimit(0, segments, (int) std::round((dbfs + 54.0f) / 3.0f));
        if (lit != litSegments) { litSegments = lit; repaint(); }
        if (! gr && dbfs > -0.1f) { clipUntil = juce::Time::getMillisecondCounter() + 1500; }
    }
    void paint(juce::Graphics& g) override
    {
        const auto h = (float) getHeight(), w = (float) getWidth();
        const auto seg = (h - (segments - 1) * 2.0f) / segments;
        for (int i = 0; i < segments; ++i)
        {
            const auto y = gr ? i * (seg + 2.0f) : h - (i + 1) * (seg + 2.0f) + 2.0f;
            auto c = pal.raised;
            if (i < litSegments) c = gr ? pal.live : (i >= 16 ? pal.meterClip : i >= 12 ? pal.meterHot : pal.meterOk);
            g.setColour(c);
            g.fillRoundedRectangle(0.0f, y, w, seg, size::radiusSm);
        }
    }
    bool clipped() const { return juce::Time::getMillisecondCounter() < clipUntil; }

private:
    static constexpr int segments = 18;
    Palette pal;
    bool gr;
    int litSegments = 0;
    juce::uint32 clipUntil = 0;
};

//==============================================================================
// Knob with label and value underneath, attached to a parameter.
class Knob : public juce::Component
{
public:
    Knob(juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& name, bool hero = false)
        : label(name), isHero(hero)
    {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setMouseDragSensitivity(220);
        slider.setVelocityModeParameters(1.0, 1, 0.08, false, juce::ModifierKeys::shiftModifier);
        slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible(slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, paramID, slider);
        if (auto* p = state.getParameter(paramID))
        {
            slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
            param = p;
        }
        slider.onValueChange = [this] { repaint(getLocalBounds().removeFromBottom(isHero ? 32 : 18)); };
    }
    juce::Slider& getSlider() { return slider; }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromBottom(isHero ? 32 : 18);
        r.removeFromBottom(16);
        slider.setBounds(r);
    }
    void paint(juce::Graphics& g) override
    {
        auto* lf = dynamic_cast<LookAndFeel*>(&getLookAndFeel());
        const auto pal = lf ? lf->colours() : palette(Tier::Epsilon);
        auto r = getLocalBounds();
        auto valueArea = r.removeFromBottom(isHero ? 32 : 18);
        auto labelArea = r.removeFromBottom(16);
        g.setFont(controlLabel());
        g.setColour(pal.inkMuted);
        g.drawText(label, labelArea, juce::Justification::centred, false);
        g.setFont(isHero ? valueHero() : valueFont());
        g.setColour(pal.ink);
        g.drawText(param ? param->getCurrentValueAsText() : juce::String(slider.getValue(), 1), valueArea, juce::Justification::centredTop, false);
    }

private:
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
    juce::String label;
    bool isHero;
};

//==============================================================================
// Segmented switch bound to a choice parameter.
class ModeSwitch : public juce::Component, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    ModeSwitch(juce::AudioProcessorValueTreeState& s, const juce::String& id) : state(s), paramID(id)
    {
        auto* p = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(id));
        jassert(p != nullptr);
        choice = p;
        for (int i = 0; i < p->choices.size(); ++i)
        {
            auto* b = buttons.add(new juce::TextButton(p->choices[i]));
            b->setClickingTogglesState(false);
            b->onClick = [this, i] { choice->beginChangeGesture(); *choice = i; choice->endChangeGesture(); };
            addAndMakeVisible(b);
        }
        state.addParameterListener(id, this);
        refresh();
    }
    ~ModeSwitch() override { state.removeParameterListener(paramID, this); cancelPendingUpdate(); }
    void resized() override
    {
        auto r = getLocalBounds().reduced(2);
        const int w = r.getWidth() / juce::jmax(1, buttons.size());
        for (auto* b : buttons) b->setBounds(r.removeFromLeft(w).reduced(1, 0));
    }
    void paint(juce::Graphics& g) override
    {
        auto* lf = dynamic_cast<LookAndFeel*>(&getLookAndFeel());
        const auto pal = lf ? lf->colours() : palette(Tier::Epsilon);
        g.setColour(pal.bg); g.fillRoundedRectangle(getLocalBounds().toFloat(), size::radiusMd);
        g.setColour(pal.line); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), size::radiusMd, 1.0f);
    }

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String paramID;
    juce::AudioParameterChoice* choice = nullptr;
    juce::OwnedArray<juce::TextButton> buttons;
    void parameterChanged(const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { refresh(); }
    void refresh() { for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState(choice->getIndex() == i, juce::dontSendNotification); }
};

} // namespace epsilon_ui
