#include "PluginEditor.h"

using namespace epsilon_ui;

//==============================================================================
// GrooveGrid

bool GrooveGrid::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File(f).hasFileExtension("wav;aif;aiff;flac;ogg;mp3")) return true;
    return false;
}

void GrooveGrid::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver = false;
    if (! files.isEmpty() && onFileDropped) onFileDropped(juce::File(files[0]));
    repaint();
}

void GrooveGrid::refresh(bool force)
{
    const auto serial = proc.getLoopSerial();
    const auto g = proc.currentGroove();
    const auto bpm = proc.playbackBpm();
    const auto playing = proc.getPlayingSlice();
    const bool changed = serial != lastSerial || g.groove != lastG.groove || g.amount != lastG.amount || g.swing != lastG.swing
                      || g.tighten != lastG.tighten || g.humanizeMs != lastG.humanizeMs || g.accent != lastG.accent
                      || g.feelMs != lastG.feelMs || std::abs(bpm - lastBpm) > 0.01;
    if (serial != lastSerial) { loop = proc.getLoop(); cache = {}; }
    if (changed || force)
    {
        places.clear();
        if (loop)
        {
            const auto t = bounce::grooveTemplate(g.groove);
            const double msPerStep = 60000.0 / std::max(20.0, bpm) / 4.0;
            for (int i = 0; i < (int) loop->slices.size(); ++i)
                places.push_back(bounce::place(loop->slices[(size_t) i], i, g, t, msPerStep));
        }
        lastSerial = serial; lastG = g; lastBpm = bpm;
        repaint();
    }
    else if (playing != lastPlaying)
        repaint();
    lastPlaying = playing;
}

void GrooveGrid::renderCache()
{
    const auto pal = palette(Tier::Epsilon);
    const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() ? (float) juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale : 1.0f;
    cache = juce::Image(juce::Image::ARGB, juce::jmax(1, juce::roundToInt(getWidth() * scale)), juce::jmax(1, juce::roundToInt(getHeight() * scale)), true, juce::SoftwareImageType());
    juce::Graphics g(cache);
    g.addTransform(juce::AffineTransform::scale(scale));
    const auto r = getLocalBounds().toFloat();
    g.setColour(pal.bg);
    g.fillRoundedRectangle(r, size::radiusMd);
    if (! loop || loop->slices.empty()) return;

    const int steps = loop->bars * bounce::stepsPerBar;
    for (int s = 0; s <= steps; ++s)
    {
        const float x = r.getX() + r.getWidth() * (float) s / (float) steps;
        g.setColour(s % 16 == 0 ? pal.inkMuted.withAlpha(0.6f) : s % 4 == 0 ? pal.line : pal.line.withAlpha(0.45f));
        g.drawVerticalLine(juce::roundToInt(x), r.getY() + (s % 4 == 0 ? 0.0f : r.getHeight() * 0.35f), r.getBottom());
    }
    // waveform, mirrored around the centre
    const auto mid = r.getCentreY() + 10.0f, amp = r.getHeight() * 0.32f;
    juce::Path wave;
    const int n = (int) loop->overview.size();
    wave.startNewSubPath(r.getX(), mid);
    for (int i = 0; i < n; ++i) wave.lineTo(r.getX() + r.getWidth() * (float) i / (float) (n - 1), mid - amp * loop->overview[(size_t) i]);
    for (int i = n - 1; i >= 0; --i) wave.lineTo(r.getX() + r.getWidth() * (float) i / (float) (n - 1), mid + amp * loop->overview[(size_t) i]);
    wave.closeSubPath();
    g.setColour(pal.raised.brighter(0.25f));
    g.fillPath(wave);
    // bar numbers
    g.setFont(caption());
    g.setColour(pal.inkMuted);
    for (int b = 0; b < loop->bars; ++b)
        g.drawText(juce::String(b + 1), juce::Rectangle<float>(r.getX() + r.getWidth() * (float) (b * 16) / (float) steps + 5.0f, r.getY() + 3.0f, 30.0f, 14.0f),
                   juce::Justification::centredLeft, false);
}

void GrooveGrid::paint(juce::Graphics& g)
{
    const auto pal = palette(Tier::Epsilon);
    const auto r = getLocalBounds().toFloat();
    if (! loop || loop->slices.empty())
    {
        g.setColour(pal.bg);
        g.fillRoundedRectangle(r, size::radiusMd);
        g.setColour(dragOver ? pal.live : pal.line);
        const float dashes[] { 6.0f, 5.0f };
        juce::Path outline; outline.addRoundedRectangle(r.reduced(6.0f), size::radiusMd);
        juce::Path dashed; juce::PathStrokeType(1.5f).createDashedStroke(dashed, outline, dashes, 2);
        g.fillPath(dashed);
        g.setFont(body());
        g.setColour(pal.ink);
        g.drawText(proc.isAnalysing() ? "Finding the hits..." : "Drop a loop here", r.withTrimmedBottom(20.0f), juce::Justification::centred, false);
        g.setFont(caption());
        g.setColour(pal.inkMuted);
        g.drawText("WAV, AIFF or FLAC  \xc2\xb7  up to 60 s  \xc2\xb7  drums, percs, melodies", r.withTrimmedTop(30.0f), juce::Justification::centred, false);
        return;
    }
    if (cache.isNull() || cache.getWidth() < getWidth()) renderCache();
    g.drawImage(cache, r);

    const int steps = loop->bars * bounce::stepsPerBar;
    const auto xOf = [&](double step) { return r.getX() + r.getWidth() * (float) (std::fmod(step + steps, (double) steps) / steps); };
    const auto playing = proc.getPlayingSlice();
    for (size_t i = 0; i < loop->slices.size() && i < places.size(); ++i)
    {
        const auto& s = loop->slices[i];
        const float x0 = xOf(s.gridStep), x1 = xOf(places[i].step);
        const float h = juce::jlimit(0.25f, 1.0f, s.peak * 1.6f);
        const float top = r.getY() + 18.0f + (1.0f - h) * (r.getHeight() - 36.0f) * 0.5f;
        // where it was
        g.setColour(pal.inkMuted);
        g.drawRect(juce::Rectangle<float>(x0 - 1.5f, r.getBottom() - 14.0f, 3.0f, 10.0f), 1.0f);
        // the move
        if (std::abs(x1 - x0) > 1.0f && std::abs(x1 - x0) < r.getWidth() * 0.5f)
        {
            g.setColour(pal.live.withAlpha(0.55f));
            g.drawLine(x0, r.getBottom() - 9.0f, x1, r.getBottom() - 9.0f, 1.5f);
        }
        // where the groove puts it
        const bool hot = (int) i == playing;
        g.setColour(hot ? pal.focus : pal.live.withAlpha(0.35f + 0.65f * juce::jlimit(0.0f, 1.0f, places[i].gain * h)));
        g.fillRoundedRectangle(juce::Rectangle<float>(x1 - (hot ? 2.0f : 1.5f), top, hot ? 4.0f : 3.0f, r.getBottom() - 16.0f - top), 1.5f);
    }
    if (dragOver)
    {
        g.setColour(pal.live);
        g.drawRoundedRectangle(r.reduced(1.0f), size::radiusMd, 2.0f);
    }
}

//==============================================================================
// A button you drag: rendering happens when the drag starts.
namespace
{
struct DragOutButton : juce::TextButton
{
    using TextButton::TextButton;
    std::function<void()> onDragStart;
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 6 && ! started && onDragStart) { started = true; onDragStart(); }
    }
    void mouseUp(const juce::MouseEvent& e) override { started = false; TextButton::mouseUp(e); }
    bool started = false;
};
}

//==============================================================================
EpsilonBounceEditor::EpsilonBounceEditor(EpsilonBounceProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&look);
    setOpaque(true);
    addAndMakeVisible(header);
    addAndMakeVisible(footer);
    footer.statusText = [this]
    {
        if (proc.isPreviewing() && ! proc.hostIsPlaying()) return juce::String("Preview ") + juce::String(proc.playbackBpm(), 1) + " BPM";
        if (proc.hostIsPlaying()) return juce::String("Playing ") + juce::String(proc.playbackBpm(), 1) + " BPM";
        return juce::String("Stopped");
    };
    footer.cpuPercent = [this] { return proc.getCpuPercent(); };

    for (auto* b : { &loadButton, &previewButton, &syncButton }) addAndMakeVisible(b);
    previewButton.setClickingTogglesState(true);
    previewButton.getProperties().set("pill", true);
    syncButton.getProperties().set("pill", true);
    previewButton.onClick = [this] { proc.setPreview(previewButton.getToggleState()); };
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Load a loop", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) load(fc.getResult()); });
    };

    auto drag = std::make_unique<DragOutButton>("Drag out");
    drag->setTooltip("Drag the bounced loop (2 loops long) into your DAW");
    auto* dragRaw = drag.get();
    dragRaw->onDragStart = [this]
    {
        const auto f = proc.renderToFile(2);
        if (f.existsAsFile()) performExternalDragDropOfFiles({ f.getFullPathName() }, false);
    };
    dragRaw->onClick = [this] { message = "Hold and drag this into a track"; repaint(); };
    addAndMakeVisible(dragRaw);
    dragRaw->setComponentID("drag");
    dragOwned.reset(drag.release());

    fileLabel.setFont(body());
    fileLabel.setColour(juce::Label::textColourId, look.colours().ink);
    addAndMakeVisible(fileLabel);

    grid.onFileDropped = [this](const juce::File& f) { load(f); };
    addAndMakeVisible(grid);
    addAndMakeVisible(grooveSwitch);
    for (auto* k : { &amount, &swing, &tighten, &humanize, &accent, &feel, &output, &sensitivity, &tempo }) addAndMakeVisible(k);

    barsBox.addItemList({ "Auto", "1 bar", "2 bars", "4 bars", "8 bars" }, 1);
    addAndMakeVisible(barsBox);
    barsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state, "bars", barsBox);
    syncButton.setClickingTogglesState(true);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state, "sync", syncButton);

    setResizable(true, true);
    setResizeLimits(900 * 3 / 4, 560 * 3 / 4, 900 * 2, 560 * 2);
    getConstrainer()->setFixedAspectRatio(900.0 / 560.0);
    setSize(900, 560);
    startTimerHz(30);
}

EpsilonBounceEditor::~EpsilonBounceEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void EpsilonBounceEditor::load(const juce::File& f)
{
    message = proc.loadFile(f);
    repaint();
}

void EpsilonBounceEditor::timerCallback()
{
    grid.refresh(false);
    const auto name = proc.getLoopName();
    const auto text = proc.isAnalysing() ? juce::String("Finding the hits...") : name.isEmpty() ? juce::String("No loop loaded") : name;
    if (fileLabel.getText() != text) fileLabel.setText(text, juce::dontSendNotification);
    tempo.setEnabled(! syncButton.getToggleState() || ! proc.hostIsPlaying());
}

void EpsilonBounceEditor::mouseDrag(const juce::MouseEvent&) {}

void EpsilonBounceEditor::paint(juce::Graphics& g)
{
    const auto& pal = look.colours();
    g.fillAll(pal.bg);
    drawPanel(g, heroPanel.toFloat(), pal, "Groove");
    drawPanel(g, feelPanel.toFloat(), pal, "Feel");
    // slicing row inside the hero panel
    g.setFont(sectionLabel());
    g.setColour(pal.inkMuted);
    g.drawText("SLICING", slicePanel.withHeight(14), juce::Justification::centredLeft, false);
    g.drawText("BARS", barsBox.getBounds().translated(0, -18).withHeight(14), juce::Justification::centredLeft, false);
    if (message.isNotEmpty())
    {
        g.setFont(caption());
        g.setColour(pal.meterHot);
        g.drawText(message, heroPanel.withTrimmedLeft(110).withHeight(34).reduced(size::space4, 0), juce::Justification::centredRight, false);
    }
}

void EpsilonBounceEditor::resized()
{
    const float s = getWidth() / 900.0f;
    juce::ignoreUnused(s);
    auto r = getLocalBounds();
    header.setBounds(r.removeFromTop(size::header));
    footer.setBounds(r.removeFromBottom(size::footer));

    auto hc = header.getControlArea();
    loadButton.setBounds(hc.removeFromLeft(96));
    hc.removeFromLeft(size::space2);
    if (auto* d = findChildWithID("drag")) d->setBounds(hc.removeFromRight(90));
    hc.removeFromRight(size::space2);
    previewButton.setBounds(hc.removeFromRight(84));
    hc.removeFromRight(size::space3);
    fileLabel.setBounds(hc);

    r.reduce(size::space6, size::space4);
    feelPanel = r.removeFromRight(juce::roundToInt(r.getWidth() * 0.33f));
    r.removeFromRight(size::space4);
    heroPanel = r;

    auto h = heroPanel.reduced(size::space4);
    h.removeFromTop(22);
    auto sliceRow = h.removeFromBottom(104);
    grooveSwitch.setBounds(h.removeFromBottom(30).withSizeKeepingCentre(juce::jmin(h.getWidth(), 380), 30));
    h.removeFromBottom(size::space3);
    grid.setBounds(h);

    slicePanel = sliceRow;
    auto sr = sliceRow.withTrimmedTop(18);
    sensitivity.setBounds(sr.removeFromLeft(86));
    sr.removeFromLeft(size::space3);
    tempo.setBounds(sr.removeFromLeft(86));
    sr.removeFromLeft(size::space6);
    auto col = sr.removeFromLeft(130);
    barsBox.setBounds(col.withTrimmedTop(26).withHeight(28));
    syncButton.setBounds(col.withTrimmedTop(26 + 28 + size::space2).withHeight(26).withWidth(110));

    auto f = feelPanel.reduced(size::space4);
    f.removeFromTop(22);
    amount.setBounds(f.removeFromTop(size::knobHero + 58).withSizeKeepingCentre(140, size::knobHero + 58));
    f.removeFromTop(size::space2);
    const int cw = f.getWidth() / 3, rh = juce::jmin(f.getHeight() / 2, 100);
    auto row1 = f.removeFromTop(rh), row2 = f.removeFromTop(rh);
    for (auto* k : { &swing, &tighten, &humanize }) k->setBounds(row1.removeFromLeft(cw));
    for (auto* k : { &accent, &feel, &output }) k->setBounds(row2.removeFromLeft(cw));
}
