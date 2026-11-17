#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

/*  The editor is the exact HTML interface from the preview (ui/index.html),
    shown in a native WebView. JS <-> C++ talk through JUCE events:
      JS  -> C++ : "ready", "set" {id,value}, "gesture" {id,start}
      C++ -> JS  : "init" {heat,mode,mix,autogain,bypass},
                   "state" {heat,mode,mix,autogain,bypass,peak:[l,r],spec:[96]}            */
class OverheatAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit OverheatAudioProcessorEditor (OverheatAudioProcessor&);
    ~OverheatAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);
    juce::DynamicObject* paramsObject() const;
    void computeSpectrum (juce::Array<juce::var>& out);

    void onSet (const juce::var& v);
    void onGesture (const juce::var& v);

    OverheatAudioProcessor& proc;

    static constexpr int numBands = 96;
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::blackman, false };
    std::array<float, fftSize * 2> fftData {};
    std::array<float, numBands> smooth {};

    bool pageReady = false;

    juce::WebBrowserComponent web;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverheatAudioProcessorEditor)
};
