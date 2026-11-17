#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    std::vector<std::byte> toBytes (const char* data, int size)
    {
        std::vector<std::byte> v ((size_t) size);
        std::memcpy (v.data(), data, (size_t) size);
        return v;
    }
}

static juce::WebBrowserComponent::Options baseOptions()
{
    auto o = juce::WebBrowserComponent::Options{};
   #if JUCE_WINDOWS
    // WebView2 needs a writable profile folder (plugins live in Program Files)
    o = o.withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
         .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                                      .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                               .getChildFile ("OVERHEAT_WebView2")));
   #endif
    return o;
}

OverheatAudioProcessorEditor::OverheatAudioProcessorEditor (OverheatAudioProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      web (baseOptions()
               .withNativeIntegrationEnabled()
               .withKeepPageLoadedWhenBrowserIsHidden()
               .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
               .withEventListener ("ready", [this] (const juce::var&)
               {
                   pageReady = true;
                   web.emitEventIfBrowserIsVisible ("init", juce::var (paramsObject()));
               })
               .withEventListener ("set",     [this] (const juce::var& v) { onSet (v); })
               .withEventListener ("gesture", [this] (const juce::var& v) { onGesture (v); }))
{
    addAndMakeVisible (web);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    setResizable (false, false);
    setSize (960, 550);
    startTimerHz (30);
}

OverheatAudioProcessorEditor::~OverheatAudioProcessorEditor()
{
    stopTimer();
}

void OverheatAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff07090b));
}

void OverheatAudioProcessorEditor::resized()
{
    web.setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> OverheatAudioProcessorEditor::getResource (const juce::String& url)
{
    if (url == "/" || url == "/index.html")
        return juce::WebBrowserComponent::Resource { toBytes (BinaryData::index_html, BinaryData::index_htmlSize),
                                                     "text/html" };
    return std::nullopt;
}

juce::DynamicObject* OverheatAudioProcessorEditor::paramsObject() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("heat",     proc.apvts.getRawParameterValue ("heat")->load());
    o->setProperty ("mode",     (int) proc.apvts.getRawParameterValue ("mode")->load());
    o->setProperty ("mix",      proc.apvts.getRawParameterValue ("mix")->load());
    o->setProperty ("autogain", proc.apvts.getRawParameterValue ("autogain")->load() > 0.5f);
    o->setProperty ("bypass",   proc.apvts.getRawParameterValue ("bypass")->load() > 0.5f);
    return o;
}

void OverheatAudioProcessorEditor::onSet (const juce::var& v)
{
    const auto id = v.getProperty ("id", "").toString();
    auto* param = proc.apvts.getParameter (id);
    if (param == nullptr) return;

    const float value = (float) (double) v.getProperty ("value", 0.0);
    const float norm = param->convertTo0to1 (param->getNormalisableRange().snapToLegalValue (value));
    if (std::abs (param->getValue() - norm) > 1.0e-6f)
        param->setValueNotifyingHost (norm);
}

void OverheatAudioProcessorEditor::onGesture (const juce::var& v)
{
    auto* param = proc.apvts.getParameter (v.getProperty ("id", "").toString());
    if (param == nullptr) return;
    if ((bool) v.getProperty ("start", false)) param->beginChangeGesture();
    else                                        param->endChangeGesture();
}

void OverheatAudioProcessorEditor::computeSpectrum (juce::Array<juce::var>& out)
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    proc.tapOut.read (fftData.data(), fftSize);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    const double nyq = sr * 0.5;
    const int half = fftSize / 2;

    out.ensureStorageAllocated (numBands);
    for (int i = 0; i < numBands; ++i)
    {
        // log bands 30 Hz .. 20 kHz (the UI maps them back with the same formula)
        const double f0 = 30.0 * std::pow (20000.0 / 30.0, (double) i / numBands);
        const double f1 = 30.0 * std::pow (20000.0 / 30.0, (double) (i + 1) / numBands);
        int a = (int) std::floor (f0 / nyq * half);
        int b = std::max (a + 1, (int) std::ceil (f1 / nyq * half));
        a = juce::jlimit (0, half - 1, a);
        b = juce::jlimit (a + 1, half, b);

        float m = 0.0f;
        for (int j = a; j < b; ++j) m = std::max (m, fftData[(size_t) j] / (float) fftSize);

        smooth[(size_t) i] = 0.5f * smooth[(size_t) i] + 0.5f * m;
        const float db = 20.0f * std::log10 (std::max (smooth[(size_t) i], 1.0e-9f));
        out.add (std::round (std::max (-90.0f, db) * 10.0f) / 10.0f);
    }
}

void OverheatAudioProcessorEditor::timerCallback()
{
    if (! pageReady) return;

    auto* o = paramsObject();

    juce::Array<juce::var> peak;
    peak.add (proc.peakL.exchange (0.0f));
    peak.add (proc.peakR.exchange (0.0f));
    o->setProperty ("peak", peak);

    juce::Array<juce::var> spec;
    computeSpectrum (spec);
    o->setProperty ("spec", spec);

    web.emitEventIfBrowserIsVisible ("state", juce::var (o));
}
