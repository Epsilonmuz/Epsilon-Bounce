#pragma once
#include "PluginProcessor.h"
#include "../Shared/epsilon_ui/EpsilonUI.h"

// The hero: the loop's waveform under a 16th grid. Each hit shows where it was (hollow tick) and
// where the groove puts it (champagne bar), joined by a short line - you see the bounce.
class GrooveGrid : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit GrooveGrid(EpsilonBounceProcessor& p) : proc(p) {}
    std::function<void(const juce::File&)> onFileDropped;
    void refresh(bool force);
    void paint(juce::Graphics&) override;
    void resized() override { cache = {}; }
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    EpsilonBounceProcessor& proc;
    std::shared_ptr<const bounce::LoopData> loop;
    std::vector<bounce::Placement> places;
    bounce::GrooveSettings lastG {};
    double lastBpm = 0.0;
    int lastSerial = -1, lastPlaying = -2;
    bool dragOver = false;
    juce::Image cache;       // waveform + grid, redrawn only when the loop or size changes
    void renderCache();
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
    epsilon_ui::HeaderBar header { epsilon_ui::Tier::Epsilon, "Bounce" };
    epsilon_ui::FooterBar footer { epsilon_ui::Tier::Epsilon, JucePlugin_VersionString };

    juce::TextButton loadButton { "Load loop" }, previewButton { "Preview" };
    std::unique_ptr<juce::TextButton> dragOwned;   // drag-to-DAW source
    juce::Label fileLabel;
    GrooveGrid grid { proc };
    epsilon_ui::ModeSwitch grooveSwitch { proc.state, "groove" };
    epsilon_ui::Knob amount { proc.state, "amount", "Amount", true };
    epsilon_ui::Knob swing { proc.state, "swing", "Swing" }, tighten { proc.state, "tighten", "Tighten" },
                     humanize { proc.state, "humanize", "Humanize" }, accent { proc.state, "accent", "Accent" },
                     feel { proc.state, "feel", "Feel" }, output { proc.state, "output", "Output" },
                     sensitivity { proc.state, "sensitivity", "Sensitivity" }, tempo { proc.state, "tempo", "Tempo" };
    juce::ComboBox barsBox;
    juce::TextButton syncButton { "Host sync" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> barsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> heroPanel, feelPanel, slicePanel;
    juce::String message;

    void load(const juce::File& f);
    void timerCallback() override;
    void mouseDrag(const juce::MouseEvent&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpsilonBounceEditor)
};
