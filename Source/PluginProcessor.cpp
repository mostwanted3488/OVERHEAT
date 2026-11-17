#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ids
{
    static const juce::String heat     { "heat" };
    static const juce::String mode     { "mode" };
    static const juce::String mix      { "mix" };
    static const juce::String autogain { "autogain" };
    static const juce::String bypass   { "bypass" };
}

juce::AudioProcessorValueTreeState::ParameterLayout OverheatAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::heat, 1 }, "Heat",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::mode, 1 }, "Mode",
                                                        StringArray { "THERMAL", "TUBE", "TAPE", "FUZZ", "GRIT" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::mix, 1 }, "Mix",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::autogain, 1 }, "Auto Gain", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    return layout;
}

OverheatAudioProcessor::OverheatAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OVERHEAT", createLayout())
{
    pushParamsToEngine();
}

void OverheatAudioProcessor::pushParamsToEngine()
{
    engine.targetHeat.store (apvts.getRawParameterValue (ids::heat)->load() / 100.0f);
    engine.targetMode.store ((int) apvts.getRawParameterValue (ids::mode)->load());
    engine.autoGain.store (apvts.getRawParameterValue (ids::autogain)->load() > 0.5f);
}

bool OverheatAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void OverheatAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = juce::jmax (1, getTotalNumOutputChannels());
    preparedChannels = numCh;
    maxBlock = juce::jmax (1, samplesPerBlock);

    pushParamsToEngine();
    engine.prepare (sampleRate, numCh);

    // 4x oversampling around the shaper (2 stages of half-band IIR, low latency)
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numCh, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) juce::jmax (1, samplesPerBlock));
    oversampler->reset();

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), (juce::uint32) numCh };
    mixer.prepare (spec);
    mixer.setMixingRule (juce::dsp::DryWetMixingRule::linear);
    mixer.setWetLatency (oversampler->getLatencyInSamples());
    mixer.reset();

    setLatencySamples ((int) std::round (oversampler->getLatencyInSamples()));
}

void OverheatAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    if (oversampler == nullptr || n == 0)
        return;

    const int numCh = juce::jmin (numOut, preparedChannels);

    pushParamsToEngine();
    const bool bypass = apvts.getRawParameterValue (ids::bypass)->load() > 0.5f;
    const float mix   = apvts.getRawParameterValue (ids::mix)->load() / 100.0f;

    mixer.setWetMixProportion (bypass ? 0.0f : mix);

    // work in chunks no larger than what the oversampler / mixer were prepared for
    juce::dsp::AudioBlock<float> whole (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        auto block = whole.getSubBlock ((size_t) start, (size_t) len);

        std::array<float*, overheat::Engine::maxCh> base {};
        for (int c = 0; c < numCh && c < overheat::Engine::maxCh; ++c)
            base[(size_t) c] = block.getChannelPointer ((size_t) c);
        const int engCh = juce::jmin (numCh, overheat::Engine::maxCh);

        mixer.pushDrySamples (block);

        engine.beginBlock (len);
        engine.preFilter (base.data(), engCh, len);

        auto up = oversampler->processSamplesUp (block);
        {
            std::array<float*, overheat::Engine::maxCh> ptrs {};
            const int upCh = (int) juce::jmin ((size_t) overheat::Engine::maxCh, up.getNumChannels());
            for (int c = 0; c < upCh; ++c)
                ptrs[(size_t) c] = up.getChannelPointer ((size_t) c);
            engine.shape (ptrs.data(), upCh, (int) up.getNumSamples());
        }
        oversampler->processSamplesDown (block);

        engine.postFilter (base.data(), engCh, len);
        mixer.mixWetSamples (block);
    }

    tapOut.push (buffer.getArrayOfReadPointers(), numCh, n);

    const float pl = buffer.getMagnitude (0, 0, n);
    const float pr = numCh > 1 ? buffer.getMagnitude (1, 0, n) : pl;
    if (pl > peakL.load()) peakL.store (pl);
    if (pr > peakR.load()) peakR.store (pr);
}

void OverheatAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OverheatAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    pushParamsToEngine();
}

juce::AudioProcessorEditor* OverheatAudioProcessor::createEditor()
{
    return new OverheatAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OverheatAudioProcessor();
}
