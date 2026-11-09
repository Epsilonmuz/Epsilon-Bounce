#pragma once
// epsilon_ui — the shared Epsilon Sounds look (design system v1).
// One file, header-only, copied unchanged into every plugin repo under Shared/epsilon_ui/.
// Two lines, one system: Tier::Muzi (premium, near-black + brushed gold) and Tier::Epsilon
// (main line, graphite + platinum). Everything else — layout, controls, the "live" champagne,
// meters — is shared.
//
// Fonts: Epsilon Dial Regular/Bold (the house typeface) from the plugin's BinaryData
// (EpsilonDialRegular_ttf, EpsilonDialBold_ttf). Medium maps to Regular; SemiBold and Bold to Bold.
// Brand art (hallmark + wordmarks) lives in EpsilonBrandArt.h as SVG paths.
//
// Performance (Intel Mac rule): static chrome is painted into a cached image once per size;
// only knobs, meters and the plugin's hero repaint.

#include <JuceHeader.h>
#include "EpsilonPalette.h"
#include "EpsilonBrandArt.h"
#include "EpsilonHeader.h"
#include "EpsilonControls.h"

namespace epsilon_ui
{
//==============================================================================
enum class Weight { Medium, SemiBold, Bold };

inline juce::Font font(Weight w, float height)
{
    static juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor(BinaryData::EpsilonDialRegular_ttf, BinaryData::EpsilonDialRegular_ttfSize);
    static juce::Typeface::Ptr bold    = juce::Typeface::createSystemTypefaceFor(BinaryData::EpsilonDialBold_ttf, BinaryData::EpsilonDialBold_ttfSize);
    return juce::Font(juce::FontOptions(w == Weight::Medium ? regular : bold).withHeight(height));
}
// Type styles (tokens.json): section-label, control-label, value, value-hero, body, caption
inline juce::Font sectionLabel() { return font(Weight::Bold, 11.0f).withExtraKerningFactor(0.14f); }
inline juce::Font controlLabel() { return font(Weight::SemiBold, 12.5f).withExtraKerningFactor(0.04f); }
inline juce::Font valueFont()    { return font(Weight::SemiBold, 13.0f); }
inline juce::Font valueHero()    { return font(Weight::SemiBold, 24.0f); }
inline juce::Font body()         { return font(Weight::Medium, 14.0f); }
inline juce::Font caption()      { return font(Weight::Medium, 11.5f); }
inline juce::Font buttonFont()   { return font(Weight::SemiBold, 10.5f).withExtraKerningFactor(1.5f / 10.5f); }
inline juce::Font knobLabel(float size) { return font(Weight::SemiBold, size).withExtraKerningFactor(1.4f / size); }

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

    // Signature knob (controls::drawKnob): coin-edge bezel, machined face, watch-hand pointer, floating lume tip.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s) override
    {
        const auto range = s.getRange();
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        const float origin = bipolar ? (float) s.valueToProportionOfLength(0.0) : 0.0f;
        controls::State st;
        st.pressed = s.isMouseButtonDown();
        st.hover = s.isMouseOverOrDragging() && ! st.pressed;
        st.disabled = ! s.isEnabled();
        st.focus = s.hasKeyboardFocus(false) && keyboardUsed;
        controls::drawKnob(g, juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(4.0f), pos, origin, pal, st, tier == Tier::Epsilon);
    }

    // Buttons: property "segment" = a segment of a ModeSwitch, "pill" = pill toggle with LED,
    // otherwise a machined keycap (a toggle when it toggles or has "toggleKey").
    static controls::State buttonState(juce::Button& b, bool over, bool down)
    {
        controls::State st;
        st.pressed = down;
        st.hover = over && ! down;
        st.on = b.getToggleState();
        st.disabled = ! b.isEnabled();
        return st;
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto st = buttonState(b, over, down);
        const auto r = b.getLocalBounds().toFloat();
        juce::Colour label;
        if (b.getProperties().getWithDefault("segment", false))
            label = controls::drawSegment(g, r, pal, st);
        else if (b.getProperties().getWithDefault("pill", false))
        {
            juce::Rectangle<float> area;
            label = controls::drawPill(g, r.reduced(0.0f, juce::jmax(0.0f, (r.getHeight() - 26.0f) * 0.5f)), pal, st, area);
            b.getProperties().set("labelX", area.getX());
        }
        else
        {
            const bool toggle = b.getClickingTogglesState() || (bool) b.getProperties().getWithDefault("toggleKey", false);
            label = controls::drawKeycap(g, r, pal, st, toggle);
        }
        if (st.disabled) label = label.withAlpha(0.4f);
        b.getProperties().set("labelColour", (juce::int64) label.getARGB());
    }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return buttonFont(); }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        auto r = b.getLocalBounds().toFloat();
        if (b.getProperties().getWithDefault("pill", false)) r = r.withTrimmedLeft((float) b.getProperties().getWithDefault("labelX", 0.0) - 2.0f);
        const bool toggleKey = ! b.getProperties().getWithDefault("segment", false) && ! b.getProperties().getWithDefault("pill", false)
                               && (b.getClickingTogglesState() || (bool) b.getProperties().getWithDefault("toggleKey", false));
        if (toggleKey || b.getProperties().getWithDefault("segment", false)) r = r.withTrimmedBottom(5.0f);
        if (down) r = r.translated(0.0f, 1.0f);
        g.setFont(buttonFont());
        g.setColour(juce::Colour((juce::uint32) (juce::int64) b.getProperties().getWithDefault("labelColour", (juce::int64) pal.ink.getARGB())));
        g.drawText(b.getButtonText().toUpperCase(), r, juce::Justification::centred, false);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box) override
    {
        controls::State st;
        st.hover = box.isMouseOver(true) && ! down;
        st.disabled = ! box.isEnabled();
        controls::drawCombo(g, juce::Rectangle<float>(0, 0, (float) w, (float) h), pal, st, box.isPopupActive());
    }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(10, 0, box.getWidth() - box.getHeight() - 12, box.getHeight());
        label.setFont(getComboBoxFont(box));
    }
    static inline bool keyboardUsed = false;     // focus rings only after keyboard navigation

    juce::Font getComboBoxFont(juce::ComboBox&) override { return body(); }
    juce::Font getPopupMenuFont() override { return body(); }
    juce::Font getLabelFont(juce::Label&) override { return controlLabel(); }
private:
    Tier tier;
    Palette pal;
};

//==============================================================================
// Panel helper: surface, radius-lg, hairline top highlight, section title.
inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, const Palette& p, const juce::String& title = {})
{
    controls::drawPanelFrame(g, r, p, title, [](float h) { return font(Weight::SemiBold, h); });
}
inline void drawSectionLabel(juce::Graphics& g, juce::Point<float> at, const juce::String& text, const Palette& p)
{
    controls::drawSectionLabel(g, at, text, p, [](float h) { return font(Weight::SemiBold, h); });
}

//==============================================================================
// Header: hallmark + wordmark left, the plugin's own controls in getControlArea().
class HeaderBar : public juce::Component
{
public:
    HeaderBar(Tier t, const juce::String& productWord, const juce::String& taglineText = {})
        : tier(t), pal(palette(t)), word(productWord), tagline(taglineText) { setOpaque(true); }
    juce::Rectangle<int> getControlArea() const { return getLocalBounds().withTrimmedLeft(wordEnd + size::space6).reduced(size::space3, 8); }

    void paint(juce::Graphics& g) override
    {
        g.setGradientFill(juce::ColourGradient(controls::mix(pal.surface, pal.ink, 0.025f), 0.0f, 0.0f, pal.surface, 0.0f, (float) getHeight(), false));
        g.fillAll();
        g.setColour(pal.line);
        g.fillRect(0, getHeight() - 2, getWidth(), 1);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRect(0, getHeight() - 1, getWidth(), 1);
        const float right = drawProductMark(g, { (float) size::space4, (getHeight() - 32.0f) * 0.5f }, 1.0f, tier, word, tagline,
                                            [](float h, bool b) { return font(b ? Weight::Bold : Weight::SemiBold, h); });
        const int newEnd = (int) std::ceil(juce::jmax(right, (float) size::space4 + 40.0f + 150.0f));
        if (newEnd != wordEnd) { wordEnd = newEnd; if (auto* p = getParentComponent()) p->resized(); }
    }

private:
    Tier tier;
    Palette pal;
    juce::String word, tagline;
    int wordEnd = 260;
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
// Knob cell: the signature knob, a spaced-caps label under it and the value (hero: in a value window).
class Knob : public juce::Component, public juce::SettableTooltipClient
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
        slider.addMouseListener(this, false);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, paramID, slider);
        if (auto* p = state.getParameter(paramID))
        {
            slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
            param = p;
        }
        slider.onValueChange = [this] { repaint(textArea()); };
    }
    juce::Slider& getSlider() { return slider; }
    void setTooltip(const juce::String& t) { SettableTooltipClient::setTooltip(t); slider.setTooltip(t); }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromBottom(textHeight());
        slider.setBounds(r);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override  { repaint(); }
    void mouseDown(const juce::MouseEvent&) override  { repaint(); }
    void mouseUp(const juce::MouseEvent&) override    { repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto* lf = dynamic_cast<LookAndFeel*>(&getLookAndFeel());
        const auto pal = lf ? lf->colours() : palette(Tier::Epsilon);
        const bool active = slider.isMouseOverOrDragging();
        const bool dragging = slider.isMouseButtonDown();
        auto r = textArea();
        auto labelArea = r.removeFromTop(16);
        g.setFont(knobLabel(isHero ? 10.5f : 10.0f));
        g.setColour(! isEnabled() ? pal.inkMuted.withAlpha(0.4f) : active ? pal.ink : pal.inkMuted);
        g.drawText(label.toUpperCase(), labelArea, juce::Justification::centred, false);

        // value + unit as two runs: number in ink, unit smaller in inkMuted
        juce::String text = param ? param->getCurrentValueAsText() : juce::String(slider.getValue(), 1);
        juce::String num = text, unit;
        const int sp = text.lastIndexOfChar(' ');
        if (sp > 0) { num = text.substring(0, sp); unit = text.substring(sp + 1); }
        const auto vCol = dragging ? pal.live : pal.ink;
        const auto uCol = dragging ? pal.live : pal.inkMuted;
        auto valueArea = r.withHeight(isHero ? 26 : 16).toFloat();
        if (isHero)
        {
            const auto win = juce::Rectangle<float>(76.0f, 22.0f).withCentre({ valueArea.getCentreX(), valueArea.getY() + 13.0f });
            controls::drawValueWindow(g, win, pal, active);
            valueArea = win;
        }
        const auto fNum = font(Weight::Medium, isHero ? 13.0f : 12.0f), fUnit = font(Weight::Medium, isHero ? 10.5f : 10.5f);
        const float wNum = (float) juce::GlyphArrangement::getStringWidthInt(fNum, num);
        const float wUnit = unit.isEmpty() ? 0.0f : (float) juce::GlyphArrangement::getStringWidthInt(fUnit, unit) + 2.0f;
        const float x0 = valueArea.getCentreX() - (wNum + wUnit) * 0.5f;
        g.setFont(fNum);
        g.setColour(isEnabled() ? vCol : vCol.withAlpha(0.4f));
        g.drawText(num, juce::Rectangle<float>(x0, valueArea.getY(), wNum + 2.0f, valueArea.getHeight()), juce::Justification::centredLeft, false);
        if (unit.isNotEmpty())
        {
            g.setFont(fUnit);
            g.setColour(isEnabled() ? uCol : uCol.withAlpha(0.4f));
            g.drawText(unit, juce::Rectangle<float>(x0 + wNum + 2.0f, valueArea.getY() + 0.5f, wUnit + 4.0f, valueArea.getHeight()), juce::Justification::centredLeft, false);
        }
    }

private:
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
    juce::String label;
    bool isHero;
    int textHeight() const { return isHero ? 44 : 32; }
    juce::Rectangle<int> textArea() const { return getLocalBounds().removeFromBottom(textHeight()); }
};

//==============================================================================
// Segmented switch bound to a choice parameter.
class ModeSwitch : public juce::Component, public juce::SettableTooltipClient, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
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
            b->getProperties().set("segment", true);
            b->onClick = [this, i] { choice->beginChangeGesture(); *choice = i; choice->endChangeGesture(); };
            addAndMakeVisible(b);
        }
        state.addParameterListener(id, this);
        refresh();
    }
    ~ModeSwitch() override { state.removeParameterListener(paramID, this); cancelPendingUpdate(); }
    // Lay the choices out in this many rows (long lists like a genre picker): each choice becomes a
    // machined keycap with a lume bar, in a recessed tray.
    void setRows(int numRows)
    {
        rows = juce::jmax(1, numRows);
        for (auto* b : buttons) { b->getProperties().set("segment", rows == 1); b->getProperties().set("toggleKey", rows > 1); }
        resized();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(rows > 1 ? 5 : 0);
        const int n = buttons.size();
        const int perRow = (n + rows - 1) / rows;
        const int rowH = r.getHeight() / rows;
        for (int row = 0; row < rows; ++row)
        {
            auto line = r.removeFromTop(rowH);
            const int first = row * perRow, count = juce::jmin(perRow, n - first);
            if (count <= 0) break;
            const int w = line.getWidth() / perRow;
            for (int i = 0; i < count; ++i) buttons[first + i]->setBounds(line.removeFromLeft(w).reduced(rows > 1 ? 3 : 0, rows > 1 ? 3 : 0));
        }
    }
    void paint(juce::Graphics& g) override
    {
        auto* lf = dynamic_cast<LookAndFeel*>(&getLookAndFeel());
        const auto pal = lf ? lf->colours() : palette(Tier::Epsilon);
        if (rows > 1) { controls::drawWell(g, getLocalBounds().toFloat(), pal); return; }
        std::vector<juce::Rectangle<float>> segs;
        int sel = -1;
        for (int i = 0; i < buttons.size(); ++i) { segs.push_back(buttons[i]->getBounds().toFloat()); if (buttons[i]->getToggleState()) sel = i; }
        controls::drawSegmentTrack(g, getLocalBounds().toFloat(), segs, sel, pal);
    }

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String paramID;
    juce::AudioParameterChoice* choice = nullptr;
    juce::OwnedArray<juce::TextButton> buttons;
    int rows = 1;
    void parameterChanged(const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { refresh(); }
    void refresh() { for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState(choice->getIndex() == i, juce::dontSendNotification); repaint(); }
};

} // namespace epsilon_ui
