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
    const bool alt = juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown();
    if (! files.isEmpty() && onFileDropped) onFileDropped(juce::File(files[0]), alt);
    repaint();
}

void GrooveGrid::refresh(bool force)
{
    const auto serial = proc.getLoopSerial(slot);
    const auto g = proc.grooveFor(slot);
    const auto bpm = proc.gridBpm();
    const auto playing = proc.getPlayingSlice(slot);
    const auto learnSerial = proc.getLearnSerial();
    const int leader = proc.leaderOf(slot);
    const bool lead = proc.leads(slot);
    const bool changed = leader != lastLeader || lead != lastLeads || g.shiftSteps != lastG.shiftSteps || ! (g.learned == lastG.learned) || learnSerial != lastLearn || serial != lastSerial || g.groove != lastG.groove || g.amount != lastG.amount || g.swing != lastG.swing
                      || g.tighten != lastG.tighten || g.humanizeMs != lastG.humanizeMs || g.accent != lastG.accent
                      || g.feelMs != lastG.feelMs || std::abs(bpm - lastBpm) > 0.01;
    if (serial != lastSerial) { loop = proc.getLoop(slot); cache = {}; }
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
        lastSerial = serial; lastG = g; lastBpm = bpm; lastLearn = learnSerial; lastLeader = leader; lastLeads = lead;
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
    controls::drawWell(g, r, pal);
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
    // bar numbers (bar 1 carries the lane's name instead)
    g.setFont(caption());
    g.setColour(pal.inkMuted);
    for (int b = 1; b < loop->bars; ++b)
        g.drawText(juce::String(b + 1), juce::Rectangle<float>(r.getX() + r.getWidth() * (float) (b * 16) / (float) steps + 5.0f, r.getY() + 3.0f, 30.0f, 14.0f),
                   juce::Justification::centredLeft, false);
}

void GrooveGrid::paint(juce::Graphics& g)
{
    const auto pal = palette(Tier::Epsilon);
    const auto r = getLocalBounds().toFloat();
    if (! loop || loop->slices.empty())
    {
        controls::drawWell(g, r, pal);
        g.setColour(dragOver ? pal.live : pal.line);
        const float dashes[] { 6.0f, 5.0f };
        juce::Path outline; outline.addRoundedRectangle(r.reduced(6.0f), size::radiusMd);
        juce::Path dashed; juce::PathStrokeType(1.5f).createDashedStroke(dashed, outline, dashes, 2);
        g.fillPath(dashed);
        g.setFont(body());
        g.setColour(pal.ink);
        const bool isDrums = slot == EpsilonBounceProcessor::drums;
        g.drawText(proc.isAnalysing(slot) ? "Finding the hits..." : isDrums ? "Drop your DRUMS or percussion here" : "Drop your MELODY or sample here",
                   r.withTrimmedBottom(20.0f), juce::Justification::centred, false);
        g.setFont(caption());
        g.setColour(pal.inkMuted);
        g.drawText(juce::String::fromUTF8(isDrums ? "the melody locks into their pocket  \xc2\xb7  WAV, AIFF or FLAC  \xc2\xb7  up to 60 s"
                                                  : "loops, chops, flipped samples  \xc2\xb7  WAV, AIFF or FLAC  \xc2\xb7  up to 60 s"),
                   r.withTrimmedTop(30.0f), juce::Justification::centred, false);
        return;
    }
    if (cache.isNull() || cache.getWidth() < getWidth()) renderCache();
    g.drawImage(cache, r);

    const int steps = loop->bars * bounce::stepsPerBar;
    const auto xOf = [&](double step) { return r.getX() + r.getWidth() * (float) (std::fmod(step + steps, (double) steps) / steps); };
    const auto playing = proc.getPlayingSlice(slot);
    for (size_t i = 0; i < loop->slices.size() && i < places.size(); ++i)
    {
        const auto& s = loop->slices[i];
        const float x0 = xOf(s.gridStep), x1 = xOf(places[i].step);
        const float h = juce::jlimit(0.25f, 1.0f, s.peak * 1.6f);
        const float top = r.getY() + 32.0f + (1.0f - h) * (r.getHeight() - 50.0f) * 0.5f;   // clear of the lane's name row
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
    // Lane name, and what is happening to this loop: it leads, it is locked to the other, or it rides a groove.
    const bool isDrums = slot == EpsilonBounceProcessor::drums;
    const auto gs = proc.grooveFor(slot);
    const int leader = proc.leaderOf(slot);
    const bool lead = proc.leads(slot);
    const auto dotSep = juce::String(juce::CharPointer_UTF8("  \xc2\xb7  "));
    g.setFont(font(Weight::Bold, 10.5f).withExtraKerningFactor(0.16f));
    g.setColour(pal.trim);
    const juce::String tag = isDrums ? "DRUMS" : "MELODY";
    g.drawText(tag, juce::Rectangle<float>(r.getX() + 8.0f, r.getY() + 12.0f, 70.0f, 16.0f), juce::Justification::centredLeft, false);
    g.setFont(caption());
    g.setColour(pal.ink);
    g.drawText(proc.isAnalysing(slot) ? juce::String("Finding the hits...") : proc.getLoopName(slot),
               juce::Rectangle<float>(r.getX() + (isDrums ? 62.0f : 70.0f), r.getY() + 12.0f, r.getWidth() * 0.34f, 16.0f), juce::Justification::centredLeft, true);

    // The pocket this lane is locked to, as markers along the top - its notes land under them.
    const auto info = leader >= 0 ? proc.getPocketInfo(leader) : proc.getLearnInfo();
    const bool showMarkers = leader >= 0 || (! lead && info.valid && gs.groove == bounce::Groove::learned);
    juce::String status;
    if (lead)
    {
        const auto own = proc.getPocketInfo(slot);
        status = "LEADS" + dotSep + "ORIGINAL FEEL" + (own.valid ? dotSep + juce::String(juce::roundToInt(own.swingPercent)) + " % swing" : juce::String());
    }
    else if (showMarkers)
    {
        g.setColour(pal.trim);
        for (int b = 0; b < loop->bars; ++b)
            for (int st = 0; st < bounce::stepsPerBar; ++st)
                if (info.heard[(size_t) st])
                {
                    const float xs = xOf(b * bounce::stepsPerBar + st + gs.learned.offset[(size_t) st]);
                    juce::Path tri;
                    tri.addTriangle(xs - 4.0f, r.getY() + 3.0f, xs + 4.0f, r.getY() + 3.0f, xs, r.getY() + 10.0f);
                    g.fillPath(tri);
                }
        status = (leader == EpsilonBounceProcessor::melody ? "LOCKED TO MELODY: " : "LOCKED TO DRUMS: ") + info.name + dotSep + juce::String(juce::roundToInt(info.swingPercent)) + " % swing";
    }
    else
        status = "GROOVE: " + juce::String(bounce::grooveName(gs.groove)).toUpperCase();
    g.setFont(caption());
    g.setColour(lead ? pal.live : pal.trim);
    g.drawText(status, r.reduced(8.0f, 0.0f).withY(r.getY() + 12.0f).withHeight(16.0f).withTrimmedLeft(r.getWidth() * 0.45f), juce::Justification::centredRight, true);
    if (std::abs(loop->alignSteps) > 1e-6)
    {
        const double ms = loop->alignSteps * 60000.0 / loop->bpm / 4.0;
        g.setFont(caption());
        g.setColour(pal.inkMuted);
        g.drawText(juce::String::fromUTF8("ALIGNED ") + (ms > 0 ? "+" : "") + juce::String(ms, 1) + " ms",
                   r.reduced(8.0f, 2.0f).withHeight(16.0f).withY(r.getBottom() - 18.0f), juce::Justification::centredRight, false);
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
    loadButton.onClick = [this] { chooseFile(EpsilonBounceProcessor::melody); };
    loadButton.setTooltip("Load your melody, sample or loop into the MELODY lane (or drop a file on the lane).");
    learnButton.onClick = [this] { chooseFile(EpsilonBounceProcessor::drums); };
    learnButton.setTooltip("Load your drums or percussion into the DRUMS lane (or drop a file on the lane). With Pocket on DRUMS LEAD the melody locks into their pocket.");
    for (auto* b : { &clearMel, &clearDrum }) addAndMakeVisible(b);
    clearMel.setTooltip("Empty the MELODY lane.");
    clearDrum.setTooltip("Empty the DRUMS lane.");
    clearMel.onClick = [this] { proc.clearSlot(EpsilonBounceProcessor::melody); };
    clearDrum.onClick = [this] { proc.clearSlot(EpsilonBounceProcessor::drums); };
    linkSwitch.setTooltip("Who sets the pocket when both lanes are loaded. DRUMS LEAD: drums keep their feel, the melody locks to them. MELODY LEADS: the other way round. GROOVE: both ride the groove you pick below.");
    addAndMakeVisible(linkSwitch);
    melLevel.setTooltip("Volume of the MELODY lane in the plugin's output (fully down = off). Drag-outs are not affected.");
    drumLevel.setTooltip("Volume of the DRUMS lane in the plugin's output (fully down = off, if your drums already play in the DAW). Drag-outs are not affected.");
    addAndMakeVisible(melLevel); addAndMakeVisible(drumLevel);
    for (auto* b : { &holdButton, &alignButton }) { b->getProperties().set("pill", true); addAndMakeVisible(b); }
    holdButton.setTooltip("Hold keeps a note sounding until the next one starts, so moved notes leave no gaps. AUTO: on for Melodic and Grid, off for Drums.");
    holdButton.onClick = [this]
    {
        if (auto* p = proc.state.getParameter("hold"))
        {
            const int next = (juce::roundToInt(p->convertFrom0to1(p->getValue())) + 1) % 3;
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1((float) next)); p->endChangeGesture();
        }
    };
    alignButton.setClickingTogglesState(true);
    alignButton.setTooltip("Finds where the loop's beat really sits and fixes samples that start a few ms early or late.");
    alignAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state, "align", alignButton);
    materialSwitch.setTooltip("What to cut on: DRUMS = hits, MELODIC = note and chord changes, GRID = even chops on the beat (dense samples).");
    addAndMakeVisible(materialSwitch);
    addAndMakeVisible(chopSwitch);
    addAndMakeVisible(shift);

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

    const auto makeDrag = [this](int slot)
    {
        auto drag = std::make_unique<DragOutButton>("Drag out");
        drag->setTooltip(slot == EpsilonBounceProcessor::drums ? "Drag the DRUMS lane as you hear it (2 loops long) into your DAW"
                                                                : "Drag the re-grooved MELODY (2 loops long) into your DAW");
        drag->onDragStart = [this, slot] { dragOut(slot); };
        drag->onClick = [this] { message = "Hold and drag this into a track"; repaint(); };
        addAndMakeVisible(*drag);
        return drag;
    };
    dragMel = makeDrag(EpsilonBounceProcessor::melody);
    dragDrum = makeDrag(EpsilonBounceProcessor::drums);
    dragMel->setComponentID("drag");

    grid.onFileDropped = [this](const juce::File& f, bool alt) { load(f, alt ? EpsilonBounceProcessor::drums : EpsilonBounceProcessor::melody); };
    drumGrid.onFileDropped = [this](const juce::File& f, bool) { load(f, EpsilonBounceProcessor::drums); };
    grid.onDragOut = [this] { dragOut(EpsilonBounceProcessor::melody); };
    drumGrid.onDragOut = [this] { dragOut(EpsilonBounceProcessor::drums); };
    grid.setTooltip("MELODY lane. Drop a loop to load it; drag the lane into your DAW to take the re-grooved loop out. Hollow ticks = where notes were, bars = where they land.");
    drumGrid.setTooltip("DRUMS lane. Drop your drums or percussion; drag the lane into your DAW to take it out.");
    addAndMakeVisible(grid);
    addAndMakeVisible(drumGrid);
    grooveSwitch.setRows(3);
    grooveSwitch.setTooltip("The pocket both lanes ride when Pocket is on GROOVE, or when only one lane is loaded. Dimmed while one lane leads the other.");
    addAndMakeVisible(grooveSwitch);
    addAndMakeVisible(timeSwitch);
    for (auto* k : { &amount, &swing, &tighten, &humanize, &accent, &feel, &output, &sensitivity, &tempo }) addAndMakeVisible(k);
    shift.setTooltip("Slide where bar 1 starts, in 16ths - for samples that begin on a pickup.");

    barsBox.addItemList({ "Auto", "1 bar", "2 bars", "4 bars", "8 bars" }, 1);
    addAndMakeVisible(barsBox);
    barsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state, "bars", barsBox);
    syncButton.setClickingTogglesState(true);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state, "sync", syncButton);

    setResizable(true, true);
    setResizeLimits(1160 * 3 / 4, 780 * 3 / 4, 1160 * 2, 780 * 2);
    getConstrainer()->setFixedAspectRatio(1160.0 / 780.0);
    setSize(1160, 780);
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

void EpsilonBounceEditor::chooseFile(int slot)
{
    chooser = std::make_unique<juce::FileChooser>(slot == EpsilonBounceProcessor::drums ? "Load your drums or percussion" : "Load your melody, sample or loop", juce::File(),
                                                  "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, slot](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) load(fc.getResult(), slot); });
}

void EpsilonBounceEditor::load(const juce::File& f, int slot)
{
    message = proc.loadFile(f, slot);
    repaint();
}

void EpsilonBounceEditor::dragOut(int slot)
{
    const auto f = proc.renderToFile(2, slot);
    if (f.existsAsFile()) performExternalDragDropOfFiles({ f.getFullPathName() }, false);
}

void EpsilonBounceEditor::timerCallback()
{
    grid.refresh(false);
    drumGrid.refresh(false);
    {
        const bool haveMel = proc.getLoop(EpsilonBounceProcessor::melody) != nullptr, haveDrum = proc.getLoop(EpsilonBounceProcessor::drums) != nullptr;
        for (auto* c : std::initializer_list<juce::Component*> { dragMel.get(), &clearMel }) if (c->isEnabled() != haveMel) c->setEnabled(haveMel);
        for (auto* c : std::initializer_list<juce::Component*> { dragDrum.get(), &clearDrum }) if (c->isEnabled() != haveDrum) c->setEnabled(haveDrum);
        const bool both = haveMel && haveDrum;
        if (linkSwitch.getAlpha() != (both ? 1.0f : 0.5f)) linkSwitch.setAlpha(both ? 1.0f : 0.5f);
        // while one lane leads, the groove picker is not what places the notes: dim it
        const float ga = both && proc.link() != EpsilonBounceProcessor::Link::groove ? 0.4f : 1.0f;
        if (grooveSwitch.getAlpha() != ga) grooveSwitch.setAlpha(ga);
    }
    tempo.setEnabled(! syncButton.getToggleState() || ! proc.hostIsPlaying());
    const int cur = proc.getCurrentPreset();
    if (presetBox.getSelectedId() != cur + 1) presetBox.setSelectedId(cur + 1, juce::dontSendNotification);
    learnButton.setButtonText(proc.isAnalysing(EpsilonBounceProcessor::drums) ? "Listening..." : "Load drums");
    {
        const int h = juce::roundToInt(proc.state.getParameter("hold")->convertFrom0to1(proc.state.getParameter("hold")->getValue()));
        const juce::String txt = h == 1 ? "Hold: on" : h == 2 ? "Hold: off" : juce::String("Hold: auto");
        if (holdButton.getButtonText() != txt) holdButton.setButtonText(txt);
        holdButton.setToggleState(proc.holdActive(), juce::dontSendNotification);
        const bool isGrid = proc.material() == bounce::Material::grid;
        if (chopSwitch.isEnabled() != isGrid) { chopSwitch.setEnabled(isGrid); chopSwitch.setAlpha(isGrid ? 1.0f : 0.4f); }
    }
    if (proc.getLearnSerial() != shownLearnSerial)
    {
        shownLearnSerial = proc.getLearnSerial();
        const auto info = proc.getLearnInfo();
        if (! info.valid && info.name.isNotEmpty()) message = "Not enough hits in \"" + info.name + "\" to learn a groove";
        else if (info.valid) message.clear();
        repaint(learnInfoArea);
    }
}

void EpsilonBounceEditor::paint(juce::Graphics& g)
{
    const auto& pal = look.colours();
    g.fillAll(pal.bg);
    drawPanel(g, heroPanel.toFloat(), pal, "Pocket");
    if (message.isNotEmpty()) { g.setColour(pal.surface); g.fillRect(grid.getBounds().withHeight(20).translated(0, -22)); }
    drawPanel(g, feelPanel.toFloat(), pal, "Feel");
    // section labels: the Hallmark's floating bar + spaced caps
    drawSectionLabel(g, { (float) slicePanel.getX(), (float) slicePanel.getY() + 7.0f }, "Slicing", pal);
    drawSectionLabel(g, { (float) barsLabel.getX(), (float) barsLabel.getCentreY() }, "Bars", pal);
    drawSectionLabel(g, { (float) chopLabel.getX(), (float) chopLabel.getCentreY() }, "Grid chop", pal);
    drawSectionLabel(g, { (float) cutLabel.getX() + 4.0f, (float) cutLabel.getCentreY() }, "Cut on", pal);
    drawSectionLabel(g, { (float) timeSwitch.getX(), (float) timeSwitch.getY() - 11.0f }, "Time", pal);

    // what the learned groove is, under the Learn button
    const auto info = proc.getLearnInfo();
    g.setFont(caption());
    if (learnInfoArea.isEmpty()) {}
    else if (info.valid)
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
        g.drawFittedText("Drop your drum loop here\nto lock this loop to it", learnInfoArea, juce::Justification::topLeft, 2);
    }
    if (message.isNotEmpty())
    {
        g.setColour(pal.meterHot);
        g.drawText(message, grid.getBounds().withHeight(20).reduced(8, 0).translated(0, -22), juce::Justification::centredRight, true);
    }
}

void EpsilonBounceEditor::resized()
{
    auto r = getLocalBounds();
    header.setBounds(r.removeFromTop(size::header));
    footer.setBounds(r.removeFromBottom(size::footer));

    auto hc = header.getControlArea();
    loadButton.setBounds(hc.removeFromLeft(108));
    hc.removeFromLeft(size::space2);
    learnButton.setBounds(hc.removeFromLeft(100));
    hc.removeFromLeft(size::space3);
    previewButton.setBounds(hc.removeFromRight(82));
    hc.removeFromRight(size::space3);
    prevPreset.setBounds(hc.removeFromLeft(30));
    hc.removeFromLeft(4);
    nextPreset.setBounds(hc.removeFromRight(30));
    hc.removeFromRight(4);
    presetBox.setBounds(hc);

    r.reduce(size::space6, size::space4);
    feelPanel = r.removeFromRight(juce::roundToInt(r.getWidth() * 0.30f));
    r.removeFromRight(size::space4);
    heroPanel = r;

    auto h = heroPanel.reduced(size::space4);
    auto titleRow = h.removeFromTop(30);
    materialSwitch.setBounds(titleRow.removeFromRight(250).withTrimmedTop(2));
    cutLabel = titleRow.removeFromRight(70).withTrimmedRight(size::space2);
    titleRow.removeFromLeft(80);                                   // panel title
    linkSwitch.setBounds(titleRow.removeFromLeft(juce::jmin(330, titleRow.getWidth() - 8)).withTrimmedTop(2));
    h.removeFromTop(4);
    auto sliceRow = h.removeFromBottom(112);
    grooveSwitch.setBounds(h.removeFromBottom(108));
    h.removeFromBottom(size::space3);
    // two lanes, each with its own Drag out / Level / Clear column
    const int laneH = (h.getHeight() - size::space2) / 2;
    const auto lane = [](juce::Rectangle<int> area, GrooveGrid& gg, juce::TextButton* drag, epsilon_ui::Knob& level, juce::TextButton& clear)
    {
        auto col = area.removeFromRight(84);
        area.removeFromRight(size::space2);
        gg.setBounds(area);
        drag->setBounds(col.removeFromTop(26));
        clear.setBounds(col.removeFromBottom(24));
        level.setBounds(col.reduced(4, 2));
    };
    lane(h.removeFromTop(laneH), grid, dragMel.get(), melLevel, clearMel);
    h.removeFromTop(size::space2);
    lane(h, drumGrid, dragDrum.get(), drumLevel, clearDrum);

    slicePanel = sliceRow;
    auto sr = sliceRow.withTrimmedTop(16);
    for (auto* k : { &sensitivity, &shift, &tempo }) { k->setBounds(sr.removeFromLeft(66)); sr.removeFromLeft(2); }
    sr.removeFromLeft(size::space2);
    auto col = sr.removeFromLeft(104);
    barsLabel = col.withTrimmedTop(4).withHeight(14);
    barsBox.setBounds(col.withTrimmedTop(22).withHeight(28));
    syncButton.setBounds(col.withTrimmedTop(22 + 28 + size::space2).withHeight(26));
    sr.removeFromLeft(size::space2);
    auto col2 = sr.removeFromLeft(138);
    chopLabel = col2.withTrimmedTop(4).withHeight(14);
    chopSwitch.setBounds(col2.withTrimmedTop(22).withHeight(28));
    holdButton.setBounds(col2.withTrimmedTop(22 + 28 + size::space2).withHeight(26));
    sr.removeFromLeft(size::space2);
    alignButton.setBounds(sr.withTrimmedTop(22).withHeight(26).withWidth(juce::jmin(sr.getWidth(), 130)));
    learnInfoArea = {};   // what was learned is shown on the grid itself

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
