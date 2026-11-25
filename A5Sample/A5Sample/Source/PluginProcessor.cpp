#include "PluginProcessor.h"
#include "PluginEditor.h"

A5SampleProcessor::A5SampleProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
}

void A5SampleProcessor::prepareToPlay (double sr, int)
{
    hostSampleRate = sr;
    for (auto& v : voices) v.active = false;
}

bool A5SampleProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* A5SampleProcessor::createEditor()
{
    return new A5SampleEditor (*this);
}

// ---------------------------------------------------------------- loading

bool A5SampleProcessor::loadFile (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr) return false;

    const int n = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                 (juce::int64) (reader->sampleRate * 600.0));
    if (n < 1024) return false;

    juce::AudioBuffer<float> buf (2, n);
    buf.clear();
    reader->read (&buf, 0, n, 0, true, true);
    if (reader->numChannels == 1)
        buf.copyFrom (1, 0, buf, 0, 0, n);

    {
        const juce::ScopedLock sl (lock);
        for (auto& v : voices) v.active = false;
        sample = std::move (buf);
        fileSampleRate = reader->sampleRate;
        loadedFile = f;
    }
    autoSlice();
    return true;
}

// ---------------------------------------------------------------- slicing

std::vector<int> A5SampleProcessor::detectOnsets (int maxSlices) const
{
    const int hop = 512;
    const int total = sample.getNumSamples();
    const int frames = total / hop;
    if (frames < 8) return { 0 };

    const float* L = sample.getReadPointer (0);
    const float* R = sample.getReadPointer (1);

    std::vector<float> energy ((size_t) frames, 0.0f), flux ((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        float e = 0.0f;
        for (int i = 0; i < hop * 2 && f * hop + i < total; ++i)
            e += std::abs (L[f * hop + i]) + std::abs (R[f * hop + i]);
        energy[(size_t) f] = e / (float) (hop * 4);
    }
    for (int f = 1; f < frames; ++f)
        flux[(size_t) f] = juce::jmax (0.0f, energy[(size_t) f] - energy[(size_t) f - 1]);

    const int minGap = (int) (0.08 * fileSampleRate / hop) + 1;
    std::vector<std::pair<int, float>> peaks;   // frame, strength
    for (int f = 1; f < frames - 1; ++f)
    {
        float mean = 0.0f; int cnt = 0;
        for (int k = juce::jmax (0, f - 8); k <= juce::jmin (frames - 1, f + 8); ++k) { mean += flux[(size_t) k]; ++cnt; }
        const float thr = (mean / (float) cnt) * 1.6f + 1.0e-4f;
        const float v = flux[(size_t) f];
        if (v > thr && v >= flux[(size_t) f - 1] && v >= flux[(size_t) f + 1])
        {
            if (! peaks.empty() && f - peaks.back().first < minGap)
            {
                if (v > peaks.back().second) peaks.back() = { f, v };
            }
            else peaks.push_back ({ f, v });
        }
    }

    if ((int) peaks.size() > maxSlices - 1)
    {
        std::sort (peaks.begin(), peaks.end(), [] (auto& a, auto& b) { return a.second > b.second; });
        peaks.resize ((size_t) maxSlices - 1);
        std::sort (peaks.begin(), peaks.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    }

    std::vector<int> starts { 0 };
    for (auto& p : peaks) starts.push_back (p.first * hop);
    return starts;
}

void A5SampleProcessor::applySlicePoints (const std::vector<int>& starts)
{
    const juce::ScopedLock sl (lock);
    const int total = sample.getNumSamples();
    std::vector<Slice> out;
    for (size_t i = 0; i < starts.size(); ++i)
    {
        Slice s;
        s.start = starts[i];
        s.end = (i + 1 < starts.size()) ? starts[i + 1] : total;
        out.push_back (s);
    }
    slices = std::move (out);
}

void A5SampleProcessor::autoSlice()
{
    std::vector<int> starts;
    {
        const juce::ScopedLock sl (lock);
        if (sample.getNumSamples() == 0) return;
        starts = detectOnsets (numPads);
    }
    if (starts.size() < 4) { gridSlice (16); return; }
    applySlicePoints (starts);
}

void A5SampleProcessor::gridSlice (int n)
{
    int total;
    { const juce::ScopedLock sl (lock); total = sample.getNumSamples(); }
    if (total == 0) return;
    n = juce::jlimit (1, numPads, n);
    std::vector<int> starts;
    for (int i = 0; i < n; ++i) starts.push_back ((int) ((int64_t) total * i / n));
    applySlicePoints (starts);
}

int A5SampleProcessor::getNumSlices() const { const juce::ScopedLock sl (lock); return (int) slices.size(); }
int A5SampleProcessor::getTotalSamples() const { const juce::ScopedLock sl (lock); return sample.getNumSamples(); }

Slice A5SampleProcessor::getSlice (int i) const
{
    const juce::ScopedLock sl (lock);
    return juce::isPositiveAndBelow (i, (int) slices.size()) ? slices[(size_t) i] : Slice();
}

void A5SampleProcessor::setSlice (int i, const Slice& s)
{
    const juce::ScopedLock sl (lock);
    if (juce::isPositiveAndBelow (i, (int) slices.size())) slices[(size_t) i] = s;
}

void A5SampleProcessor::moveBoundary (int i, int newStart)
{
    const juce::ScopedLock sl (lock);
    if (i <= 0 || i >= (int) slices.size()) return;
    const int lo = slices[(size_t) i - 1].start + 512;
    const int hi = slices[(size_t) i].end - 512;
    if (lo > hi) return;
    newStart = juce::jlimit (lo, hi, newStart);
    slices[(size_t) i].start = newStart;
    slices[(size_t) i - 1].end = newStart;
}

std::vector<float> A5SampleProcessor::computePeaks (int columns) const
{
    const juce::ScopedLock sl (lock);
    std::vector<float> out ((size_t) columns * 2, 0.0f);
    const int total = sample.getNumSamples();
    if (total == 0 || columns <= 0) return out;
    const float* L = sample.getReadPointer (0);
    for (int c = 0; c < columns; ++c)
    {
        const int a = (int) ((int64_t) total * c / columns);
        const int b = juce::jmax (a + 1, (int) ((int64_t) total * (c + 1) / columns));
        float mn = 0.0f, mx = 0.0f;
        for (int i = a; i < b && i < total; ++i) { mn = juce::jmin (mn, L[i]); mx = juce::jmax (mx, L[i]); }
        out[(size_t) c * 2] = mn;
        out[(size_t) c * 2 + 1] = mx;
    }
    return out;
}

// ---------------------------------------------------------------- voices

void A5SampleProcessor::startVoice (int note, float velocity)
{
    const int idx = note - baseNote;
    if (! juce::isPositiveAndBelow (idx, (int) slices.size())) return;

    if (monoMode.load())
        for (auto& v : voices) if (v.active) v.releasing = true;

    Voice* target = nullptr;
    for (auto& v : voices) if (! v.active) { target = &v; break; }
    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices) if (v.age < target->age) target = &v;
    }

    auto& v = *target;
    v.s = slices[(size_t) idx];
    v.active = true;
    v.releasing = false;
    v.note = note;
    v.age = ++voiceCounter;
    v.pos = 0.0;
    v.inc = std::pow (2.0, (double) v.s.semitones / 12.0) * fileSampleRate / hostSampleRate;
    v.env = 0.0f;
    v.attackRate = 1.0f / (float) (juce::jmax (0.001f, v.s.attack) * (float) hostSampleRate);
    v.releaseRate = 1.0f / (float) (juce::jmax (0.005f, v.s.release) * (float) hostSampleRate);
    v.s.level *= (0.4f + 0.6f * velocity);

    v.filtL.reset(); v.filtR.reset();
    const float f = v.s.filter;
    v.filterOn = true;
    if (f < 0.47f)
        v.filtL.setCoefficients (juce::IIRCoefficients::makeLowPass (hostSampleRate, 200.0 * std::pow (100.0, f / 0.5)));
    else if (f > 0.53f)
        v.filtL.setCoefficients (juce::IIRCoefficients::makeHighPass (hostSampleRate, 20.0 * std::pow (500.0, (f - 0.5) / 0.5)));
    else
        v.filterOn = false;
    v.filtR.setCoefficients (v.filtL.getCoefficients());
}

void A5SampleProcessor::releaseVoices (int note)
{
    for (auto& v : voices) if (v.active && v.note == note) v.releasing = true;
}

void A5SampleProcessor::renderVoices (juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    if (numSamples <= 0) return;
    const float* srcL = sample.getReadPointer (0);
    const float* srcR = sample.getReadPointer (1);
    float* outL = out.getWritePointer (0);
    float* outR = out.getWritePointer (1);
    const float master = masterLevel.load();

    for (auto& v : voices)
    {
        if (! v.active) continue;
        const int len = v.s.end - v.s.start;

        for (int i = 0; i < numSamples; ++i)
        {
            if (v.pos >= len - 1) { v.active = false; break; }

            if (! v.releasing) { if (v.env < 1.0f) v.env = juce::jmin (1.0f, v.env + v.attackRate); }
            else { v.env -= v.releaseRate; if (v.env <= 0.0f) { v.active = false; break; } }

            const int i0 = (int) v.pos;
            const float fr = (float) (v.pos - i0);
            int a, b;
            if (v.s.reverse) { a = v.s.end - 1 - i0; b = juce::jmax (v.s.start, a - 1); }
            else             { a = v.s.start + i0;   b = juce::jmin (v.s.end - 1, a + 1); }

            float l = srcL[a] + (srcL[b] - srcL[a]) * fr;
            float r = srcR[a] + (srcR[b] - srcR[a]) * fr;
            if (v.filterOn) { l = v.filtL.processSingleSampleRaw (l); r = v.filtR.processSingleSampleRaw (r); }

            const float g = v.env * v.s.level * master;
            outL[startSample + i] += l * g;
            outR[startSample + i] += r * g;
            v.pos += v.inc;
        }
    }
}

void A5SampleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    const juce::ScopedTryLock sl (lock);
    if (! sl.isLocked() || sample.getNumSamples() == 0) return;

    int pos = 0;
    for (const auto metadata : midi)
    {
        const int at = juce::jlimit (pos, buffer.getNumSamples(), metadata.samplePosition);
        renderVoices (buffer, pos, at - pos);
        pos = at;

        const auto m = metadata.getMessage();
        if (m.isNoteOn())       startVoice (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) releaseVoices (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& v : voices) v.releasing = true;
    }
    renderVoices (buffer, pos, buffer.getNumSamples() - pos);
}

// ---------------------------------------------------------------- state

void A5SampleProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("A5Sample");
    xml.setAttribute ("path", loadedFile.getFullPathName());
    xml.setAttribute ("mono", monoMode.load());
    xml.setAttribute ("master", (double) masterLevel.load());
    {
        const juce::ScopedLock sl (lock);
        for (auto& s : slices)
        {
            auto* e = xml.createNewChildElement ("Slice");
            e->setAttribute ("start", s.start);   e->setAttribute ("end", s.end);
            e->setAttribute ("level", (double) s.level);
            e->setAttribute ("filter", (double) s.filter);
            e->setAttribute ("attack", (double) s.attack);
            e->setAttribute ("release", (double) s.release);
            e->setAttribute ("semi", (double) s.semitones);
            e->setAttribute ("rev", s.reverse);
        }
    }
    copyXmlToBinary (xml, dest);
}

void A5SampleProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName ("A5Sample")) return;

    monoMode = xml->getBoolAttribute ("mono");
    masterLevel = (float) xml->getDoubleAttribute ("master", 1.0);

    const juce::File f (xml->getStringAttribute ("path"));
    if (! f.existsAsFile() || ! loadFile (f)) return;

    std::vector<Slice> restored;
    for (auto* e : xml->getChildWithTagNameIterator ("Slice"))
    {
        Slice s;
        s.start = e->getIntAttribute ("start");   s.end = e->getIntAttribute ("end");
        s.level = (float) e->getDoubleAttribute ("level", 1.0);
        s.filter = (float) e->getDoubleAttribute ("filter", 0.5);
        s.attack = (float) e->getDoubleAttribute ("attack", 0.002);
        s.release = (float) e->getDoubleAttribute ("release", 0.08);
        s.semitones = (float) e->getDoubleAttribute ("semi", 0.0);
        s.reverse = e->getBoolAttribute ("rev");
        restored.push_back (s);
    }
    if (! restored.empty())
    {
        const juce::ScopedLock sl (lock);
        slices = std::move (restored);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new A5SampleProcessor();
}
