#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>

/*  OVERHEAT — saturation engine.
    Same maths as the browser preview (ui/src/template.html, MODES):

      pre high-shelf 2.5 kHz + peak 900 Hz (both scale with heat)
        -> waveshaper (one curve per mode, rebuilt when heat/mode move; runs oversampled)
        -> DC blocker 25 Hz -> low-pass that darkens as heat rises
        -> auto gain (matches wet loudness to the input) or a fixed compensation

    The engine is split in three stages so the processor can run the shaper inside
    juce::dsp::Oversampling:  preFilter() -> shape() [oversampled] -> postFilter().   */
namespace overheat
{
constexpr int numModes = 5;
constexpr double pi = 3.14159265358979323846;

struct ModeSpec
{
    float preDb;     // high-shelf gain at 100 %
    float midDb;     // 900 Hz peak gain at 100 %
    float lpDrop;    // low-pass drop in Hz at 100 % (19 kHz - h^2 * lpDrop)
    float gain;      // fixed make-up when auto gain is off
};

//                                      THERMAL                    TUBE                       TAPE                         FUZZ                       GRIT
constexpr ModeSpec modeSpecs[numModes] = { { 0.95f, 0.63f, 2205.0f, 1.0f }, { 0.315f, 0.79f, 1890.0f, 1.0f }, { -0.315f, 0.315f, 2835.0f, 1.05f }, { 0.63f, 0.945f, 2520.0f, 0.9f }, { 0.79f, 0.47f, 2360.0f, 0.95f } };

// raw transfer functions, x in [-1, 1], h = heat 0..1  (identical to the preview)
inline double rawShape (int mode, double x, double h)
{
    const auto T = [] (double v) { return std::tanh (v); };
    switch (mode)
    {
        case 0: // THERMAL: asymmetric diode + soft fold
        {
            const double d = 1.0 + h * h * 4.725, b = 0.038 + 0.057 * h;
            double den = T (d + b) - T (b);
            if (den == 0.0) den = 1.0;
            double y = (T (d * x + b) - T (b)) / den;
            if (h > 0.6)
            {
                const double k = (h - 0.6) / 0.4 * 0.088;
                y = (1.0 - k) * y + k * std::sin (y * pi * (1.0 + 0.7 * k));
            }
            return y;
        }
        case 1: // TUBE: triode-like, even harmonics
        {
            const double d = 1.0 + h * h * 3.15;
            if (x >= 0.0) return (1.0 - std::exp (-d * x)) / (1.0 - std::exp (-d));
            return -(1.0 - std::exp (d * 1.126 * x)) / (1.0 - std::exp (-d * 1.126)) * 0.944;
        }
        case 2: // TAPE: soft symmetric
        {
            const double d = 1.0 + h * h * 2.2;
            return T (d * x) / T (d);
        }
        case 3: // FUZZ: soft -> hard clip blend
        {
            const double d = 1.0 + h * h * 3.78, v = d * x;
            const double hard = std::clamp (v, -1.0, 1.0), soft = T (v);
            const double k = h * 0.22;
            return ((1.0 - k) * soft + k * hard) / ((1.0 - k) * T (d) + k * std::min (1.0, d));
        }
        default: // GRIT: half-wave rectified bite
        {
            const double d = 1.0 + h * h * 3.465;
            double y = T (d * x) / T (d);
            const double r = 0.107 * h;
            return y + r * (std::abs (y) - 0.5);
        }
    }
}

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    // RBJ cookbook
    void setHighShelf (double sr, double f, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * f / sr;
        const double cs = std::cos (w), sn = std::sin (w);
        const double alpha = sn / 2.0 * std::sqrt (2.0);              // S = 1
        const double sa = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1) + (A - 1) * cs + sa), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sa),
             (A + 1) - (A - 1) * cs + sa, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sa);
    }
    void setPeak (double sr, double f, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * f / sr;
        const double alpha = std::sin (w) / (2.0 * q), cs = std::cos (w);
        set (1 + alpha * A, -2 * cs, 1 - alpha * A, 1 + alpha / A, -2 * cs, 1 - alpha / A);
    }
    void setLowPass (double sr, double f, double q) noexcept
    {
        f = std::clamp (f, 20.0, sr * 0.45);
        const double w = 2.0 * pi * f / sr, alpha = std::sin (w) / (2.0 * q), cs = std::cos (w);
        set ((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + alpha, -2 * cs, 1 - alpha);
    }
    void setHighPass (double sr, double f, double q) noexcept
    {
        const double w = 2.0 * pi * f / sr, alpha = std::sin (w) / (2.0 * q), cs = std::cos (w);
        set ((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + alpha, -2 * cs, 1 - alpha);
    }

    float process (float xf) noexcept
    {
        const double x = xf;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() noexcept { z1 = z2 = 0.0; }
};

class Engine
{
public:
    static constexpr int maxCh = 2;
    static constexpr int lutSize = 4097;   // odd: x = 0 falls exactly on a point

    // written by the processor from the parameters
    std::atomic<float> targetHeat { 0.0f };    // 0..1
    std::atomic<int>   targetMode { 0 };
    std::atomic<bool>  autoGain { true };

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;
        nch = std::clamp (numChannels, 1, maxCh);
        heat = targetHeat.load();
        mode = std::clamp (targetMode.load(), 0, numModes - 1);
        rebuildLut();
        updateFilters();
        msIn = msWet = 0.0;
        agGain = 1.0;
        // ~300 ms loudness followers, ~250 ms gain glide
        followCoef = std::exp (-1.0 / (0.30 * sr));
        glideCoef  = std::exp (-1.0 / (0.25 * sr));
        reset();
    }

    void reset()
    {
        for (auto& c : ch) { c.pre.reset(); c.mid.reset(); c.dc.reset(); c.lp.reset(); }
    }

    // once per block, before processing: glide heat, rebuild curve / filters if needed
    void beginBlock (int numSamples)
    {
        const double tau = 0.03;   // 30 ms
        const double a = 1.0 - std::exp (-(double) numSamples / (tau * sr));
        const double th = std::clamp ((double) targetHeat.load(), 0.0, 1.0);
        heat += (th - heat) * a;
        if (std::abs (th - heat) < 1.0e-4) heat = th;

        const int m = std::clamp (targetMode.load(), 0, numModes - 1);
        if (m != mode || std::abs (heat - lutHeat) > 0.0015)
        {
            mode = m;
            rebuildLut();
            updateFilters();
        }
    }

    // stage 1 (base rate): measure input loudness, pre-emphasis
    void preFilter (float* const* data, int numChannels, int n) noexcept
    {
        const int c0 = std::min (numChannels, nch);
        for (int i = 0; i < n; ++i)
        {
            double s = 0.0;
            for (int c = 0; c < c0; ++c) s += (double) data[c][i] * data[c][i];
            msIn = followCoef * msIn + (1.0 - followCoef) * (s / c0);
        }
        for (int c = 0; c < c0; ++c)
        {
            auto& st = ch[(size_t) c];
            float* x = data[c];
            for (int i = 0; i < n; ++i)
                x[i] = st.mid.process (st.pre.process (x[i]));
        }
    }

    // stage 2 (oversampled): the waveshaper
    void shape (float* const* data, int numChannels, int n) noexcept
    {
        for (int c = 0; c < numChannels; ++c)
        {
            float* x = data[c];
            for (int i = 0; i < n; ++i)
                x[i] = lookup (x[i]);
        }
    }

    // stage 3 (base rate): DC block, darkening low-pass, gain
    void postFilter (float* const* data, int numChannels, int n) noexcept
    {
        const int c0 = std::min (numChannels, nch);
        const double fixedGain = modeSpecs[mode].gain / (1.0 + heat * 0.35);
        const bool ag = autoGain.load();

        for (int i = 0; i < n; ++i)
        {
            double s = 0.0;
            for (int c = 0; c < c0; ++c)
            {
                auto& st = ch[(size_t) c];
                float y = st.lp.process (st.dc.process (data[c][i]));
                data[c][i] = y;
                s += (double) y * y;
            }
            msWet = followCoef * msWet + (1.0 - followCoef) * (s / c0);

            double target = fixedGain;
            if (ag)
            {
                target = agGain;   // hold while silent
                if (msIn > 4.0e-6 && msWet > 4.0e-6)
                    target = std::clamp (std::sqrt (msIn / msWet), 0.3, 1.6);
            }
            agGain = glideCoef * agGain + (1.0 - glideCoef) * target;

            for (int c = 0; c < c0; ++c)
                data[c][i] = (float) (data[c][i] * agGain);
        }
    }

    double getHeat() const noexcept { return heat; }

private:
    struct Channel { Biquad pre, mid, dc, lp; };

    float lookup (float x) const noexcept
    {
        // same as WebAudio's WaveShaper: input clamped to [-1, 1], linear interpolation
        const float p = (std::clamp (x, -1.0f, 1.0f) + 1.0f) * 0.5f * (float) (lutSize - 1);
        const int i = std::min ((int) p, lutSize - 2);
        const float f = p - (float) i;
        return lut[(size_t) i] + (lut[(size_t) i + 1] - lut[(size_t) i]) * f;
    }

    void rebuildLut()
    {
        lutHeat = heat;
        double mx = 1.0e-6;
        std::array<double, lutSize> tmp {};
        for (int i = 0; i < lutSize; ++i)
        {
            const double x = (double) i / (lutSize - 1) * 2.0 - 1.0;
            tmp[(size_t) i] = rawShape (mode, x, heat);
            mx = std::max (mx, std::abs (tmp[(size_t) i]));
        }
        // centre the curve so silence stays silent, then normalise (as in the preview)
        const double z = rawShape (mode, 0.0, heat);
        const double norm = mx + std::abs (z);
        for (int i = 0; i < lutSize; ++i)
            lut[(size_t) i] = (float) ((tmp[(size_t) i] - z) / norm);
    }

    void updateFilters()
    {
        const auto& M = modeSpecs[mode];
        const double h = heat;
        for (auto& st : ch)
        {
            st.pre.setHighShelf (sr, 2500.0, h * M.preDb);
            st.mid.setPeak (sr, 900.0, 0.8, h * M.midDb);
            st.dc.setHighPass (sr, 25.0, 0.7071);
            st.lp.setLowPass (sr, 19000.0 - h * h * M.lpDrop, 0.7071);
        }
    }

    double sr = 44100.0;
    int nch = 2;
    double heat = 0.0, lutHeat = -1.0;
    int mode = 0;
    std::array<float, lutSize> lut {};
    std::array<Channel, maxCh> ch {};
    double msIn = 0.0, msWet = 0.0, agGain = 1.0, followCoef = 0.999, glideCoef = 0.999;
};
} // namespace overheat
