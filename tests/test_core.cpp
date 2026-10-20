#include "../Source/DenoiserCore.h"
#include "synth.h"
#include <cstdio>
#include <chrono>
using namespace std;

static vector<float> run (const vector<float>& in, nsup::Params p, double sr, int nch, double& cpuPct, int& lat)
{
    nsup::Denoiser d; d.prepare (sr, nch); d.setParams (p);
    vector<float> out = in, out2 = in; int total = (int) in.size();
    auto t0 = chrono::high_resolution_clock::now();
    for (int i = 0; i < total; i += 480)
    {
        int n = min (480, total - i); float* ch[2] = { out.data() + i, out2.data() + i }; d.process (ch, n);
    }
    cpuPct = 100 * chrono::duration<double> (chrono::high_resolution_clock::now() - t0).count() / (total / sr);
    lat = d.getLatencySamples();
    vector<float> al (total, 0.f); for (int n = 0; n + lat < total; ++n) al[n] = out[n + lat];
    return al;
}

int main()
{
    const double sr = 48000; Synth s (sr, 20, 0.3f); int lat; double cpu;
    printf("SNR de entrada (voz): %.1f dB\n", 10*log10 (Synth::pw (s.clean, 3*48000, s.total) / Synth::pw (s.noise, 3*48000, s.total)));

    nsup::Params p; p.reductionDb = 18; p.sensitivity = 0.5f; p.smoothing = 0.6f;
    auto o = run (s.noisy, p, sr, 1, cpu, lat);
    vector<float> err (s.total); for (int n = 0; n < s.total; ++n) err[n] = o[n] - s.clean[n];
    double gapIn = s.gapPower (s.noisy, lat), gapOut = s.gapPower (o, lat);
    printf("[Denoiser] atenuacion en pausas: %.1f dB | SNR salida: %.1f dB | CPU mono %.2f %% | latencia %d (%.1f ms)\n",
           10*log10 (gapIn / gapOut), 10*log10 (Synth::pw (s.clean, 3*48000, s.total) / Synth::pw (err, 3*48000, s.total)), cpu, lat, 1000.0*lat/sr);

    // --- NOISE GATE: umbral entre el pico de ruido y el pico de voz ---
    float noisePeak = 0, voicePeak = 0;
    for (int n = 0; n < 3 * 48000; ++n) noisePeak = max (noisePeak, fabs (s.noisy[n]));
    for (int n = 3 * 48000; n < s.total; ++n) voicePeak = max (voicePeak, fabs (s.noisy[n]));
    double thrDb = 20 * log10 (noisePeak * 1.5);
    printf("[Gate] pico ruido %.1f dBFS, pico voz %.1f dBFS -> umbral de prueba %.1f dB\n", 20*log10 (noisePeak), 20*log10 (voicePeak), thrDb);
    nsup::Params pg = p; pg.gateThresholdDb = (float) thrDb;
    auto og = run (s.noisy, pg, sr, 1, cpu, lat);
    printf("[Gate] pausas: sin gate %.1f dB -> con gate %.1f dB (atenuacion total vs entrada)\n", 10*log10 (gapIn / gapOut), 10*log10 (gapIn / s.gapPower (og, lat)));
    printf("[Gate] energia de voz conservada (fase 0.1-0.4 s): %.2f dB respecto a sin gate (0 = intacta)\n", 10*log10 (s.voicePower (og) / s.voicePower (o)));
    printf("[Gate] CPU con gate: %.2f %%\n", cpu);

    // umbral demasiado alto: debe silenciar todo (comportamiento esperado de un gate)
    nsup::Params ph2 = p; ph2.gateThresholdDb = 0.f; auto oh = run (s.noisy, ph2, sr, 1, cpu, lat);
    printf("[Gate] umbral 0 dB (cierra todo): potencia de voz %.1f dB bajo sin gate\n", 10*log10 (s.voicePower (o) / max (1e-20, s.voicePower (oh))));

    // estereo = mono
    auto o1 = run (s.noisy, pg, sr, 1, cpu, lat);
    nsup::Denoiser ds; ds.prepare (sr, 2); ds.setParams (pg); vector<float> L = s.noisy, R = s.noisy;
    for (int i = 0; i < s.total; i += 480) { int n = min (480, s.total - i); float* ch[2] = { L.data() + i, R.data() + i }; ds.process (ch, n); }
    double md = 0; for (int n = 0; n + lat < s.total; ++n) md = max (md, (double) fabs (L[n + lat] - o1[n]));
    printf("Estereo vs mono (con gate): diferencia max %.2e\n", md);

    // robustez: silencio digital y muestras extremas
    nsup::Denoiser dz; dz.prepare (44100, 2); dz.setParams (pg); vector<float> z (44100, 0.f), z2 (44100, 0.f);
    for (int i = 0; i < 44100; i += 7) { z[i] = 1.f; z2[i] = -1.f; }
    float* chz[2] = { z.data(), z2.data() }; dz.process (chz, 44100);
    bool bad = false; for (float v : z) if (!std::isfinite (v)) bad = true;
    printf("Impulsos de escala completa + gate -> NaN/Inf: %s\n", bad ? "SI (MAL)" : "no");
    for (double r : { 22050.0, 44100.0, 88200.0, 96000.0, 192000.0 })
    { nsup::Denoiser d; d.prepare (r, 2); nsup::Params q; q.gateThresholdDb = -40; d.setParams (q); vector<float> a (4096, 0.1f), b (4096, 0.1f); float* c[2] = { a.data(), b.data() }; d.process (c, 4096);
      printf("  fs=%.0f -> latencia %d muestras (%.1f ms)\n", r, d.getLatencySamples(), 1000.0 * d.getLatencySamples() / r); }
}
