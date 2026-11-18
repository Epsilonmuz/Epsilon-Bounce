#pragma once
#include "PluginProcessor.h"
#include "../Shared/epsilon_ui/EpsilonUI.h"

// The hero: the loop's waveform under a 16th grid. Each hit shows where it was (hollow tick) and
// where the groove puts it (champagne bar), joined by a short line - you see the bounce.
// One lane per slot (v1.5): MELODY on top, DRUMS under it. Drop a loop on a lane to load it there;
// drag the lane (or its Drag out button) into the DAW to take the re-grooved loop out.
class GrooveGrid : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    GrooveGrid(EpsilonBounceProcessor& p, int slotIndex) : proc(p), slot(slotIndex) {}
    std::function<void(const juce::File&, bool alt)> onFileDropped;
    std::function<void()> onDragOut, onHover;
    void mouseEnter(const juce::MouseEvent&) override { if (loop && onHover) onHover(); }
    void mouseDrag(const juce::MouseEvent& e) override { if (loop && ! dragging && e.getDistanceFromDragStart() > 8 && onDragOut) { dragging = true; onDragOut(); } }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
    void refresh(bool force);
    void paint(juce::Graphics&) override;
    void resized() override { cache = {}; }
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    EpsilonBounceProcessor& proc;
    int slot = 0, lastLeader = -2;
    bool dragging = false, lastLeads = false;
    std::shared_ptr<const bounce::LoopData> loop;
    std::vector<bounce::Placement> places;
    bounce::GrooveSettings lastG {};
    double lastBpm = 0.0;
    int lastSerial = -1, lastPlaying = -2, lastLearn = -1;
    bool dragOver = false;
    juce::Image cache;       // waveform + grid, redrawn only when the loop or size changes
    void renderCache();
};

// Step 1 of the flow (v1.6), one per lane: the loop's own tempo -> the project tempo.
// "92.0 BPM -> 140.0  stretched, pitch kept". Click the tempo to type it; 1/2 and x2 re-read the same
// rhythm at half or double speed; Auto reads it again. Fit: AUTO (melody stretched, drums re-timed hit by
// hit), SLICES or STRETCH.
class TempoStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    TempoStrip(EpsilonBounceProcessor& p, int slotIndex, const epsilon_ui::Palette& pal);
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    EpsilonBounceProcessor& proc;
    int slot;
    const epsilon_ui::Palette& pal;
    juce::Label bpmLabel;
    juce::TextButton half { juce::String(juce::CharPointer_UTF8("\xc2\xbd")) }, dbl { juce::String(juce::CharPointer_UTF8("\xc3\x97" "2")) },
                     autoButton { "Auto" }, fitButton { "Fit: auto" };
    juce::Rectangle<int> statusArea;
    juce::String status;
    bool live = false;           // the lane is in step with the project tempo
    double shownSrc = -2.0, shownMatched = -2.0, shownTarget = -2.0;
    bool shownBusy = false, shownStretched = false, shownHave = false;
    int shownFit = -1;
};

// "Load drums": click to choose your drum loop, or drop it straight onto the button.
struct DropButton : juce::TextButton, juce::FileDragAndDropTarget
{
    using juce::TextButton::TextButton;
    std::function<void(const juce::File&)> onFileDropped;
    bool isInterestedInFileDrag(const juce::StringArray& f) override { return f.size() > 0 && juce::File(f[0]).hasFileExtension("wav;aif;aiff;flac;ogg;mp3"); }
    void fileDragEnter(const juce::StringArray&, int, int) override { setToggleState(true, juce::dontSendNotification); }
    void fileDragExit(const juce::StringArray&) override { setToggleState(false, juce::dontSendNotification); }
    void filesDropped(const juce::StringArray& f, int, int) override
    {
        setToggleState(false, juce::dontSendNotification);
        if (onFileDropped && f.size() > 0) onFileDropped(juce::File(f[0]));
    }
};

class EpsilonBounceEditor : public juce::AudioProcessorEditor, private juce::Timer, public juce::DragAndDropContainer
{
public:
    explicit EpsilonBounceEditor(EpsilonBounceProcessor&);
    ~EpsilonBounceEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    EpsilonBounceProcessor& proc;
    epsilon_ui::LookAndFeel look { epsilon_ui::Tier::Epsilon };
    epsilon_ui::HeaderBar header { epsilon_ui::Tier::Epsilon, "Bounce", juce::String (juce::CharPointer_UTF8 ("Loop re-groover")) };
    epsilon_ui::FooterBar footer { epsilon_ui::Tier::Epsilon, JucePlugin_VersionString };
    juce::TooltipWindow tooltips { this, 700 };

    juce::TextButton loadButton { "Load melody" }, previewButton { "Preview" };
    DropButton learnButton { "Load drums" };
    juce::TextButton clearMel { "Clear" }, clearDrum { "Clear" };
    std::unique_ptr<juce::TextButton> dragMel, dragDrum;   // drag-to-DAW sources, one per lane
    epsilon_ui::ModeSwitch linkSwitch { proc.state, "link" };
    epsilon_ui::Knob melLevel { proc.state, "melLevel", "Level" }, drumLevel { proc.state, "drumLevel", "Level" };
    juce::TextButton holdButton { "Hold: auto" }, alignButton { "Auto-align" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> alignAttachment;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox presetBox;
    GrooveGrid grid { proc, EpsilonBounceProcessor::melody }, drumGrid { proc, EpsilonBounceProcessor::drums };
    TempoStrip melTempo { proc, EpsilonBounceProcessor::melody, look.colours() }, drumTempo { proc, EpsilonBounceProcessor::drums, look.colours() };
    epsilon_ui::ModeSwitch grooveSwitch { proc.state, "groove" };
    epsilon_ui::ModeSwitch timeSwitch { proc.state, "time" };
    epsilon_ui::ModeSwitch materialSwitch { proc.state, "material" };
    epsilon_ui::ModeSwitch chopSwitch { proc.state, "chop" };
    epsilon_ui::Knob amount { proc.state, "amount", "Amount", true };
    epsilon_ui::Knob swing { proc.state, "swing", "Swing" }, tighten { proc.state, "tighten", "Tighten" },
                     humanize { proc.state, "humanize", "Humanize" }, accent { proc.state, "accent", "Accent" },
                     feel { proc.state, "feel", "Feel" }, output { proc.state, "output", "Output" },
                     sensitivity { proc.state, "sensitivity", "Sensitivity" }, tempo { proc.state, "tempo", "Tempo" },
                     shift { proc.state, "shift", "Shift" };
    juce::ComboBox barsBox;
    juce::TextButton syncButton { "Host sync" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> barsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> heroPanel, feelPanel, slicePanel, learnInfoArea;
    juce::Rectangle<int> barsLabel, chopLabel, cutLabel;
    juce::String message, loopTitle;
    bool messageIsNote = false;       // grey info line instead of a red problem
    int shownLearnSerial = -1, shownNoteSerial = 0;

    void load(const juce::File& f, int slot);
    void chooseFile(int slot);
    void dragOut(int slot);
    void buildPresetMenu();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpsilonBounceEditor)
};
