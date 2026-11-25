#pragma once
#include "PluginProcessor.h"

namespace a5
{
    const juce::Colour bg      { 0xff1b1d21 };
    const juce::Colour panel   { 0xff25282d };
    const juce::Colour accent  { 0xffff8a1f };
    inline juce::Colour padColour (int i, bool has)
    {
        return juce::Colour::fromHSV (0.04f + 0.24f * (float) i / 31.0f, 0.85f, has ? 0.78f : 0.22f, 1.0f);
    }
}

// Waveform with coloured slice regions; click to select, drag a marker to move it.
class WaveformView : public juce::Component
{
public:
    WaveformView (A5SampleProcessor& p, std::function<void (int)> onSelect);
    void refresh();
    void setSelected (int i) { selected = i; repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override { refresh(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

private:
    int sampleToX (int s) const;
    int xToSample (int x) const;

    A5SampleProcessor& proc;
    std::function<void (int)> onSelect;
    std::vector<float> peaks;
    int selected = 0, dragging = -1;
};

// 8 x 4 pad grid, triggers MIDI notes 36..67 through the processor's keyboard state.
class PadGrid : public juce::Component, private juce::Timer
{
public:
    PadGrid (A5SampleProcessor& p, std::function<void (int)> onSelect);
    ~PadGrid() override { stopTimer(); }
    void setSelected (int i) { selected = i; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<float> padBounds (int i) const;
    int padAt (juce::Point<int>) const;

    A5SampleProcessor& proc;
    std::function<void (int)> onSelect;
    int selected = 0, held = -1;
};

class A5SampleEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget
{
public:
    explicit A5SampleEditor (A5SampleProcessor&);
    ~A5SampleEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void loadAndRefresh (const juce::File&);
    void selectPad (int i);
    void refreshControls();
    void editSelected (const std::function<void (Slice&)>& fn);
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& name,
                    double lo, double hi, double def, double step = 0.0, double skewMid = 0.0);

    A5SampleProcessor& proc;
    int selected = 0;

    juce::TextButton loadBtn { "Load" }, autoBtn { "Auto Slice" }, gridBtn { "Grid x16" };
    juce::ToggleButton monoBtn { "Mono" }, reverseBtn { "Reverse" };
    juce::Label nameLabel;

    juce::Slider levelK, filterK, attackK, releaseK, keyK;
    juce::Label levelL, filterL, attackL, releaseL, keyL;

    WaveformView wave;
    PadGrid pads;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (A5SampleEditor)
};
