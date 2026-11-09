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
    const bool learn = juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown();
    if (! files.isEmpty() && onFileDropped) onFileDropped(juce::File(files[0]), learn);
    repaint();
}

void GrooveGrid::refresh(bool force)
{
    const auto serial = proc.getLoopSerial();
    const auto g = proc.currentGroove();
    const auto bpm = proc.gridBpm();
    const auto playing = proc.getPlayingSlice();
    const auto learnSerial = proc.getLearnSerial();
    const bool changed = learnSerial != lastLearn || serial != lastSerial || g.groove != lastG.groove || g.amount != lastG.amount || g.swing != lastG.swing
                      || g.tighten != lastG.tighten || g.humanizeMs != lastG.humanizeMs || g.accent != lastG.accent
                      || g.feelMs != lastG.feelMs || std::abs(bpm - lastBpm) > 0.01;
    if (serial != lastSerial) { loop = proc.getLoop(); cache = {}; }
    if (changed || force)
    {
        places.clear();
        if (loop)
        {
            const auto t = bounce::templateFor(g);
            const double msPerStep = 60000.0 / std::max(20.0, bpm) / 4.0;
            for (int i = 0; i < (int) loop->slices.size(); ++i)
                places.push_back(bounce::place(loop->slices[(size_t) i], i, g, t, msPerStep));
        }
        lastSerial = serial; lastG = g; lastBpm = bpm; lastLearn = learnSerial;
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
const juce::String dot = juce::String(juce::CharPointer_UTF8("  \xc2\xb7  "));
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
        const auto k = proc.timeFactor();
        const juce::String mode = k < 1.0 ? dot + "Half-time" : k > 1.0 ? dot + "Double-time" : juce::String();
        if (proc.isPreviewing() && ! proc.hostIsPlaying()) return "Preview " + juce::String(proc.playbackBpm(), 1) + " BPM" + mode;
        if (proc.hostIsPlaying()) return "Playing " + juce::String(proc.playbackBpm(), 1) + " BPM" + mode;
        return "Stopped" + mode;
    };
    footer.cpuPercent = [this] { return proc.getCpuPercent(); };

    for (auto* b : { &loadButton, &previewButton, &syncButton, &learnButton, &prevPreset, &nextPreset }) addAndMakeVisible(b);
    previewButton.setClickingTogglesState(true);
    previewButton.getProperties().set("pill", true);
    syncButton.getProperties().set("pill", true);
    previewButton.onClick = [this] { proc.setPreview(previewButton.getToggleState()); };
    loadButton.onClick = [this] { chooseFile(false); };
    learnButton.onClick = [this] { chooseFile(true); };
    learnButton.setTooltip("Copy the timing feel of any reference loop (a drummer, a break, a beat you love) onto your loop. Tip: Alt-drop a file on the grid.");

    buildPresetMenu();
    presetBox.setTextWhenNothingSelected("Presets");
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.applyPreset(presetBox.getSelectedId() - 1); };
    addAndMakeVisible(presetBox);
    auto step = [this](int d)
    {
        const int cur = proc.getCurrentPreset();
        const int next = cur < 0 ? (d > 0 ? 0 : bounce::presetCount - 1) : (cur + d + bounce::presetCount) % bounce::presetCount;
        presetBox.setSelectedId(next + 1, juce::sendNotificationSync);
    };
    prevPreset.onClick = [step] { step(-1); };
    nextPreset.onClick = [step] { step(1); };

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

    grid.onFileDropped = [this](const juce::File& f, bool learn) { load(f, learn); };
    addAndMakeVisible(grid);
    grooveSwitch.setRows(3);
    addAndMakeVisible(grooveSwitch);
    addAndMakeVisible(timeSwitch);
    for (auto* k : { &amount, &swing, &tighten, &humanize, &accent, &feel, &output, &sensitivity, &tempo }) addAndMakeVisible(k);

    barsBox.addItemList({ "Auto", "1 bar", "2 bars", "4 bars", "8 bars" }, 1);
    addAndMakeVisible(barsBox);
    barsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state, "bars", barsBox);
    syncButton.setClickingTogglesState(true);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state, "sync", syncButton);

    setResizable(true, true);
    setResizeLimits(960 * 3 / 4, 600 * 3 / 4, 960 * 2, 600 * 2);
    getConstrainer()->setFixedAspectRatio(960.0 / 600.0);
    setSize(960, 600);
    startTimerHz(30);
}

EpsilonBounceEditor::~EpsilonBounceEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void EpsilonBounceEditor::buildPresetMenu()
{
    presetBox.clear(juce::dontSendNotification);
    juce::String family;
    for (int i = 0; i < bounce::presetCount; ++i)
    {
        const auto& pr = bounce::presets[i];
        if (family != pr.family) { family = pr.family; presetBox.addSectionHeading(family.toUpperCase()); }
        presetBox.addItem(pr.name, i + 1);
    }
}

void EpsilonBounceEditor::chooseFile(bool learn)
{
    chooser = std::make_unique<juce::FileChooser>(learn ? "Learn the groove of a reference loop" : "Load a loop", juce::File(),
                                                  "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, learn](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) load(fc.getResult(), learn); });
}

void EpsilonBounceEditor::load(const juce::File& f, bool learn)
{
    message = learn ? proc.learnFromFile(f) : proc.loadFile(f);
    repaint();
}

void EpsilonBounceEditor::timerCallback()
{
    grid.refresh(false);
    const auto name = proc.getLoopName();
    const auto title = proc.isAnalysing() ? juce::String("Finding the hits...") : name;
    if (title != loopTitle) { loopTitle = title; repaint(heroPanel.withHeight(40)); }
    tempo.setEnabled(! syncButton.getToggleState() || ! proc.hostIsPlaying());
    const int cur = proc.getCurrentPreset();
    if (presetBox.getSelectedId() != cur + 1) presetBox.setSelectedId(cur + 1, juce::dontSendNotification);
    learnButton.setButtonText(proc.isLearning() ? "Learning..." : "Learn groove");
    if (proc.getLearnSerial() != shownLearnSerial)
    {
        shownLearnSerial = proc.getLearnSerial();
        const auto info = proc.getLearnInfo();
        if (! info.valid && info.name.isNotEmpty()) message = "Not enough hits in \"" + info.name + "\" to learn a groove";
        else if (info.valid) message.clear();
        repaint(learnInfoArea);
        repaint(heroPanel.withHeight(40));
    }
}

void EpsilonBounceEditor::paint(juce::Graphics& g)
{
    const auto& pal = look.colours();
    g.fillAll(pal.bg);
    drawPanel(g, heroPanel.toFloat(), pal, "Groove");
    drawPanel(g, feelPanel.toFloat(), pal, "Feel");
    if (loopTitle.isNotEmpty())
    {
        g.setFont(body());
        g.setColour(pal.ink);
        g.drawText(loopTitle, heroPanel.withHeight(34).withTrimmedLeft(96).withTrimmedRight(size::space4), juce::Justification::centredLeft, true);
    }
    g.setFont(sectionLabel());
    g.setColour(pal.inkMuted);
    g.drawText("SLICING", slicePanel.withHeight(14), juce::Justification::centredLeft, false);
    g.drawText("BARS", barsBox.getBounds().translated(0, -18).withHeight(14), juce::Justification::centredLeft, false);
    g.drawText("TIME", timeSwitch.getBounds().translated(0, -18).withHeight(14), juce::Justification::centredLeft, false);

    // what the learned groove is, under the Learn button
    const auto info = proc.getLearnInfo();
    g.setFont(caption());
    if (info.valid)
    {
        g.setColour(pal.ink);
        g.drawText(info.name, learnInfoArea.withHeight(16), juce::Justification::centredLeft, true);
        g.setColour(pal.inkMuted);
        g.drawText(juce::String(info.hits) + " hits" + dot + juce::String(juce::roundToInt(info.swingPercent)) + " % swing" + dot + juce::String(info.steps) + "/16 steps",
                   learnInfoArea.withTrimmedTop(16).withHeight(16), juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour(pal.inkMuted);
        g.drawFittedText("Copy any loop's feel:\nAlt-drop it on the grid", learnInfoArea, juce::Justification::topLeft, 2);
    }
    if (message.isNotEmpty())
    {
        g.setColour(pal.meterHot);
        g.drawText(message, heroPanel.withHeight(34).withTrimmedLeft(96).reduced(size::space4, 0), juce::Justification::centredRight, true);
    }
}

void EpsilonBounceEditor::resized()
{
    auto r = getLocalBounds();
    header.setBounds(r.removeFromTop(size::header));
    footer.setBounds(r.removeFromBottom(size::footer));

    auto hc = header.getControlArea();
    loadButton.setBounds(hc.removeFromLeft(92));
    hc.removeFromLeft(size::space3);
    if (auto* d = findChildWithID("drag")) d->setBounds(hc.removeFromRight(88));
    hc.removeFromRight(size::space2);
    previewButton.setBounds(hc.removeFromRight(82));
    hc.removeFromRight(size::space3);
    prevPreset.setBounds(hc.removeFromLeft(30));
    hc.removeFromLeft(4);
    nextPreset.setBounds(hc.removeFromRight(30));
    hc.removeFromRight(4);
    presetBox.setBounds(hc);

    r.reduce(size::space6, size::space4);
    feelPanel = r.removeFromRight(juce::roundToInt(r.getWidth() * 0.32f));
    r.removeFromRight(size::space4);
    heroPanel = r;

    auto h = heroPanel.reduced(size::space4);
    h.removeFromTop(24);
    auto sliceRow = h.removeFromBottom(100);
    grooveSwitch.setBounds(h.removeFromBottom(84));
    h.removeFromBottom(size::space3);
    grid.setBounds(h);

    slicePanel = sliceRow;
    auto sr = sliceRow.withTrimmedTop(16);
    sensitivity.setBounds(sr.removeFromLeft(84));
    sr.removeFromLeft(size::space2);
    tempo.setBounds(sr.removeFromLeft(84));
    sr.removeFromLeft(size::space4);
    auto col = sr.removeFromLeft(120);
    barsBox.setBounds(col.withTrimmedTop(22).withHeight(28));
    syncButton.setBounds(col.withTrimmedTop(22 + 28 + size::space2).withHeight(26));
    sr.removeFromLeft(size::space4);
    learnButton.setBounds(sr.withTrimmedTop(22).withHeight(28).withWidth(juce::jmin(150, sr.getWidth())));
    learnInfoArea = sr.withTrimmedTop(22 + 28 + 6).withWidth(juce::jmin(200, sr.getWidth()));

    auto f = feelPanel.reduced(size::space4);
    f.removeFromTop(22);
    auto timeRow = f.removeFromBottom(30);
    f.removeFromBottom(22);
    timeSwitch.setBounds(timeRow);
    amount.setBounds(f.removeFromTop(size::knobHero + 56).withSizeKeepingCentre(140, size::knobHero + 56));
    const int cw = f.getWidth() / 3, rh = f.getHeight() / 2;
    auto row1 = f.removeFromTop(rh), row2 = f.removeFromTop(rh);
    for (auto* k : { &swing, &tighten, &humanize }) k->setBounds(row1.removeFromLeft(cw));
    for (auto* k : { &accent, &feel, &output }) k->setBounds(row2.removeFromLeft(cw));
}
