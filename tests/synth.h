// Senal sintetica de prueba: "voz" armonica con formantes + ruido (blanco+marron+zumbido)
#pragma once
#include <vector>
#include <cmath>
#include <random>
struct Synth
{
    double sr; int total;
    std::vector<float> clean, noise, noisy;
    // noiseScale 0.3 => SNR de entrada ~ +3 dB. Los primeros 3 s son solo ruido.
    Synth (double sampleRate, double seconds, float noiseScale)
        : sr (sampleRate), total ((int) (sampleRate * seconds)), clean (total), noise (total), noisy (total)
    {
        std::mt19937 rng (1); std::normal_distribution<float> nd (0.f, 1.f);
        double ph = 0; const double pi = 3.14159265358979;
        for (int n = 0; n < total; ++n)
        {
            double t = n / sr, f0 = 120 + 40 * std::sin (2 * pi * 0.7 * t);
            ph += 2 * pi * f0 / sr;
            double env = std::fmod (t, 0.9) < 0.5 ? 0.5 * (1 - std::cos (2 * pi * std::fmod (t, 0.9) / 0.5)) : 0.0;
            if (t < 3.0) env = 0;
            double s = 0;
            for (int h = 1; h < 40; ++h) { double f = h * f0;
                double form = std::exp (-std::pow ((f - 600) / 300, 2)) + 0.6 * std::exp (-std::pow ((f - 1800) / 500, 2)) + 0.3 * std::exp (-std::pow ((f - 3200) / 700, 2));
                s += form / h * std::sin (h * ph); }
            clean[n] = (float) (0.12 * env * s);
        }
        float lp = 0;
        for (int n = 0; n < total; ++n)
        {
            float w = nd (rng); lp = 0.98f * lp + 0.02f * w;
            float nz = 0.02f * w + 0.25f * lp + 0.01f * (float) std::sin (2 * pi * 50 * n / sr) + 0.005f * (float) std::sin (2 * pi * 150 * n / sr);
            nz *= noiseScale; noise[n] = nz; noisy[n] = clean[n] + nz;
        }
    }
    static double pw (const std::vector<float>& x, int a, int b) { double s = 0; for (int i = a; i < b; i++) s += (double) x[i] * x[i]; return s / (b - a); }
    // potencia en pausas entre silabas (t > 4 s y fase > 0.6 s dentro de cada ciclo de 0.9 s)
    double gapPower (const std::vector<float>& x, int lat) const
    { double a = 0; long c = 0; for (int n = 4 * (int) sr; n < total - lat; n++) { if (std::fmod (n / sr, 0.9) > 0.6) { a += (double) x[n] * x[n]; c++; } } return a / c; }
    // potencia durante la voz (fase 0.1..0.4 s del ciclo) de una senal alineada
    double voicePower (const std::vector<float>& x) const
    { double a = 0; long c = 0; for (int n = 4 * (int) sr; n < total; n++) { double ph = std::fmod (n / sr, 0.9); if (ph > 0.1 && ph < 0.4) { a += (double) x[n] * x[n]; c++; } } return a / c; }
};
