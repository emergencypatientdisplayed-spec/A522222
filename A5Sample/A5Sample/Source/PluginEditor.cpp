#include "PluginEditor.h"

// ============================================================ WaveformView

WaveformView::WaveformView (A5SampleProcessor& p, std::function<void (int)> cb)
    : proc (p), onSelect (std::move (cb)) {}

void WaveformView::refresh()
{
    peaks = proc.computePeaks (juce::jmax (1, getWidth()));
    repaint();
}

int WaveformView::sampleToX (int s) const
{
    const int total = juce::jmax (1, proc.getTotalSamples());
    return (int) ((double) s / total * getWidth());
}

int WaveformView::xToSample (int x) const
{
    return (int) ((double) x / juce::jmax (1, getWidth()) * proc.getTotalSamples());
}

void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101113));
    if (proc.getTotalSamples() == 0)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (16.0f);
        g.drawText ("Drop an audio file here or press Load", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const int n = proc.getNumSlices();
    for (int i = 0; i < n; ++i)
    {
        const auto s = proc.getSlice (i);
        const int x0 = sampleToX (s.start), x1 = sampleToX (s.end);
        g.setColour (a5::padColour (i, true).withAlpha (i == selected ? 0.38f : 0.14f));
        g.fillRect (x0, 0, x1 - x0, getHeight());
    }

    const float mid = (float) getHeight() * 0.5f;
    g.setColour (a5::accent.withAlpha (0.9f));
    for (int x = 0; x < (int) peaks.size() / 2 && x < getWidth(); ++x)
        g.drawVerticalLine (x, mid - peaks[(size_t) x * 2 + 1] * mid, mid - peaks[(size_t) x * 2] * mid + 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.7f));
    for (int i = 0; i < n; ++i)
        g.drawVerticalLine (sampleToX (proc.getSlice (i).start), 0.0f, (float) getHeight());
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    const int n = proc.getNumSlices();
    dragging = -1;
    for (int i = 1; i < n; ++i)
        if (std::abs (sampleToX (proc.getSlice (i).start) - e.x) <= 6) { dragging = i; return; }

    const int s = xToSample (e.x);
    for (int i = 0; i < n; ++i)
    {
        const auto sl = proc.getSlice (i);
        if (s >= sl.start && s < sl.end) { selected = i; onSelect (i); repaint(); return; }
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    proc.moveBoundary (dragging, xToSample (e.x));
    repaint();
}

// ============================================================ PadGrid

PadGrid::PadGrid (A5SampleProcessor& p, std::function<void (int)> cb)
    : proc (p), onSelect (std::move (cb))
{
    startTimerHz (30);
}

juce::Rectangle<float> PadGrid::padBounds (int i) const
{
    const float w = (float) getWidth() / 8.0f, h = (float) getHeight() / 4.0f;
    return juce::Rectangle<float> ((float) (i % 8) * w, (float) (i / 8) * h, w, h).reduced (3.0f);
}

int PadGrid::padAt (juce::Point<int> p) const
{
    for (int i = 0; i < A5SampleProcessor::numPads; ++i)
        if (padBounds (i).contains (p.toFloat())) return i;
    return -1;
}

void PadGrid::paint (juce::Graphics& g)
{
    const int n = proc.getNumSlices();
    for (int i = 0; i < A5SampleProcessor::numPads; ++i)
    {
        const bool has = i < n;
        const bool on = proc.keyboardState.isNoteOn (1, A5SampleProcessor::baseNote + i);
        auto r = padBounds (i);
        auto c = a5::padColour (i, has);
        g.setColour (on ? c.brighter (0.6f) : c);
        g.fillRoundedRectangle (r, 5.0f);
        if (i == selected)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (r, 5.0f, 2.0f);
        }
        if (has)
        {
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.setFont (12.0f);
            g.drawText (juce::MidiMessage::getMidiNoteName (A5SampleProcessor::baseNote + i, true, true, 3),
                        r.reduced (6.0f, 3.0f), juce::Justification::bottomRight);
        }
    }
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int i = padAt (e.getPosition());
    if (i < 0 || i >= proc.getNumSlices()) return;
    held = i;
    selected = i;
    proc.keyboardState.noteOn (1, A5SampleProcessor::baseNote + i, 1.0f);
    onSelect (i);
}

void PadGrid::mouseUp (const juce::MouseEvent&)
{
    if (held >= 0) proc.keyboardState.noteOff (1, A5SampleProcessor::baseNote + held, 0.0f);
    held = -1;
}

// ============================================================ Editor

A5SampleEditor::A5SampleEditor (A5SampleProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      wave (p, [this] (int i) { selectPad (i); }),
      pads (p, [this] (int i) { selectPad (i); })
{
    setSize (780, 580);

    for (auto* b : { &loadBtn, &autoBtn, &gridBtn }) addAndMakeVisible (b);
    addAndMakeVisible (monoBtn);
    addAndMakeVisible (reverseBtn);
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (wave);
    addAndMakeVisible (pads);

    nameLabel.setText (proc.getLoadedName().isEmpty() ? "no sample loaded" : proc.getLoadedName(), juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile()) loadAndRefresh (fc.getResult());
                              });
    };
    autoBtn.onClick = [this] { proc.autoSlice(); selectPad (0); wave.refresh(); };
    gridBtn.onClick = [this] { proc.gridSlice (16); selectPad (0); wave.refresh(); };

    monoBtn.setToggleState (proc.monoMode.load(), juce::dontSendNotification);
    monoBtn.onClick = [this] { proc.monoMode = monoBtn.getToggleState(); };

    reverseBtn.onClick = [this] { editSelected ([this] (Slice& s) { s.reverse = reverseBtn.getToggleState(); }); };

    setupKnob (levelK,   levelL,   "Level",   0.0, 1.5, 1.0);
    setupKnob (filterK,  filterL,  "Filter",  0.0, 1.0, 0.5);
    setupKnob (attackK,  attackL,  "Attack",  0.001, 2.0, 0.002, 0.0, 0.1);
    setupKnob (releaseK, releaseL, "Release", 0.005, 3.0, 0.08, 0.0, 0.3);
    setupKnob (keyK,     keyL,     "Key Shift", -12.0, 12.0, 0.0, 1.0);

    levelK.onValueChange   = [this] { editSelected ([this] (Slice& s) { s.level = (float) levelK.getValue(); }); };
    filterK.onValueChange  = [this] { editSelected ([this] (Slice& s) { s.filter = (float) filterK.getValue(); }); };
    attackK.onValueChange  = [this] { editSelected ([this] (Slice& s) { s.attack = (float) attackK.getValue(); }); };
    releaseK.onValueChange = [this] { editSelected ([this] (Slice& s) { s.release = (float) releaseK.getValue(); }); };
    keyK.onValueChange     = [this] { editSelected ([this] (Slice& s) { s.semitones = (float) keyK.getValue(); }); };

    refreshControls();
    wave.refresh();
}

A5SampleEditor::~A5SampleEditor() = default;

void A5SampleEditor::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& name,
                                double lo, double hi, double def, double step, double skewMid)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    s.setRange (lo, hi, step);
    if (skewMid > 0.0) s.setSkewFactorFromMidPoint (skewMid);
    s.setDoubleClickReturnValue (true, def);
    s.setValue (def, juce::dontSendNotification);
    s.setColour (juce::Slider::rotarySliderFillColourId, a5::accent);
    addAndMakeVisible (s);

    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (l);
}

void A5SampleEditor::editSelected (const std::function<void (Slice&)>& fn)
{
    if (selected >= proc.getNumSlices()) return;
    auto s = proc.getSlice (selected);
    fn (s);
    proc.setSlice (selected, s);
}

void A5SampleEditor::refreshControls()
{
    const auto s = proc.getSlice (selected);
    levelK.setValue (s.level, juce::dontSendNotification);
    filterK.setValue (s.filter, juce::dontSendNotification);
    attackK.setValue (s.attack, juce::dontSendNotification);
    releaseK.setValue (s.release, juce::dontSendNotification);
    keyK.setValue (s.semitones, juce::dontSendNotification);
    reverseBtn.setToggleState (s.reverse, juce::dontSendNotification);
}

void A5SampleEditor::selectPad (int i)
{
    selected = i;
    pads.setSelected (i);
    wave.setSelected (i);
    refreshControls();
}

void A5SampleEditor::loadAndRefresh (const juce::File& f)
{
    if (! proc.loadFile (f)) return;
    nameLabel.setText (f.getFileName(), juce::dontSendNotification);
    selectPad (0);
    wave.refresh();
}

void A5SampleEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() > 0) loadAndRefresh (juce::File (files[0]));
}

void A5SampleEditor::paint (juce::Graphics& g)
{
    g.fillAll (a5::bg);
    g.setColour (a5::panel);
    g.fillRect (0, 0, getWidth(), 48);
    g.fillRect (0, 268, getWidth(), 130);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("A5", 14, 0, 36, 48, juce::Justification::centredLeft);
    g.setColour (a5::accent);
    g.setFont (juce::FontOptions (22.0f));
    g.drawText ("Sample", 46, 0, 90, 48, juce::Justification::centredLeft);
}

void A5SampleEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (48).reduced (8, 10);
    top.removeFromLeft (130);
    loadBtn.setBounds (top.removeFromLeft (70));   top.removeFromLeft (6);
    autoBtn.setBounds (top.removeFromLeft (90));   top.removeFromLeft (6);
    gridBtn.setBounds (top.removeFromLeft (80));   top.removeFromLeft (10);
    monoBtn.setBounds (top.removeFromLeft (70));
    nameLabel.setBounds (top);

    wave.setBounds (r.removeFromTop (220).reduced (8, 6));

    auto ctl = r.removeFromTop (130).reduced (8, 6);
    const int kw = 90;
    for (auto pair : { std::pair<juce::Slider*, juce::Label*> { &levelK, &levelL },
                       { &filterK, &filterL }, { &attackK, &attackL },
                       { &releaseK, &releaseL }, { &keyK, &keyL } })
    {
        auto cell = ctl.removeFromLeft (kw);
        pair.second->setBounds (cell.removeFromTop (18));
        pair.first->setBounds (cell);
    }
    reverseBtn.setBounds (ctl.removeFromLeft (100).withSizeKeepingCentre (100, 28));

    pads.setBounds (r.reduced (8, 6));
}
