// Prueba de respiracion y teclado con senales sinteticas (ruido bajo + eventos).
#include "../Source/DenoiserCore.h"
#include "synth.h"
#include "events.h"
#include <cstdio>
#include <chrono>
#include <random>
using namespace std;
static const double SR = 48000;
using std::vector;

static vector<float> run (const vector<float>& in, const nsup::Params& p, double& cpu, int nch = 1)
{
    nsup::Denoiser d; d.prepare (SR, nch); d.setParams (p);
    vector<float> a = in, b = in; int total = (int) in.size();
    auto t0 = chrono::high_resolution_clock::now();
    for (int i = 0; i < total; i += 480) { int n = min (480, total - i); float* ch[2] = { a.data() + i, b.data() + i }; d.process (ch, n); }
    cpu = 100 * chrono::duration<double> (chrono::high_resolution_clock::now() - t0).count() / (total / SR);
    int lat = d.getLatencySamples(); vector<float> al (total, 0.f);
    for (int n = 0; n + lat < total; ++n) al[n] = a[n + lat];
    return al;
}
static double energy (const vector<float>& x, double t0, double t1) { double s = 0; for (int n = (int) (t0 * SR); n < (int) (t1 * SR) && n < (int) x.size(); ++n) s += (double) x[n] * x[n]; return s; }
static double peak (const vector<float>& x, double t0, double t1) { double m = 0; for (int n = (int) (t0 * SR); n < (int) (t1 * SR) && n < (int) x.size(); ++n) m = max (m, (double) fabs (x[n])); return m; }
static double db (double r) { return 10 * log10 (max (r, 1e-30)); }

int main()
{
    Synth sy (SR, 18, 0.1f); Rng rng; double cpu;
    nsup::Params off; off.reductionDb = 18; off.sensitivity = 0.5f; off.smoothing = 0.6f;
    nsup::Params br = off; br.suppressBreath = true;
    nsup::Params ky = off; ky.suppressKeys = true;
    nsup::Params both = off; both.suppressBreath = both.suppressKeys = true;

    // ---------- 0) regresion: casillas apagadas => salida IDENTICA a la version anterior ----------
    printf ("0) Regresion con casillas apagadas\n");
    {   vector<float> o = run (sy.noisy, off, cpu);
        // flags explicitamente false => mismo camino de codigo; comparar con dos corridas
        nsup::Params off2 = off; off2.suppressBreath = false; off2.suppressKeys = false;
        vector<float> o2 = run (sy.noisy, off2, cpu);
        double md = 0; for (size_t n = 0; n < o.size(); ++n) md = max (md, (double) fabs (o[n] - o2[n]));
        printf ("   reproducible: diferencia max %.1e\n", md); }

    // ---------- A) respiraciones en pausa (solo ruido de fondo) ----------
    printf ("A) RESPIRACION (6 soplos de 0.5 s, pico -30 dBFS, sobre ruido de fondo)\n");
    {   vector<float> in = sy.noise; for (int i = 0; i < 6; ++i) addAt (in, breathEvent (0.5, 0.03f, rng), 4.0 + i * 1.6);
        auto o0 = run (in, off, cpu), o1 = run (in, br, cpu); double sum = 0, lead = 0;
        for (int i = 0; i < 6; ++i) { double t = 4.0 + i * 1.6; double a = db (energy (o0, t + 0.10, t + 0.50) / energy (o1, t + 0.10, t + 0.50));
            double l = db (energy (o0, t, t + 0.10) / energy (o1, t, t + 0.10)); sum += a; lead += l; printf ("   soplo %d: tramo 100-500 ms -> -%.1f dB | primeros 100 ms -> -%.1f dB\n", i + 1, a, l); }
        printf ("   MEDIA tras el arranque: -%.1f dB (respecto a solo supresion de ruido)\n", sum / 6);
        auto o2 = run (in, off, cpu); (void) o2; }

    // ---------- B) clics de teclado en pausa ----------
    printf ("B) TECLADO (20 clics, pico -20 dBFS, en pausas)\n");
    {   vector<float> in = sy.noise; vector<double> ts; for (int i = 0; i < 20; ++i) { double t = 4.0 + i * 0.43 + (i % 3) * 0.05; ts.push_back (t); addAt (in, clickEvent (0.1f, rng), t); }
        auto o0 = run (in, off, cpu), o1 = run (in, ky, cpu); double se = 0, sp = 0, mn = 99, mx = 0;
        for (double t : ts) { double e = db (energy (o0, t, t + 0.03) / energy (o1, t, t + 0.03)); double p = 20 * log10 (peak (o0, t, t + 0.03) / max (1e-9, peak (o1, t, t + 0.03)));
            se += e; sp += p; mn = min (mn, e); mx = max (mx, e); }
        printf ("   energia del clic: media -%.1f dB (min -%.1f, max -%.1f) | pico: media -%.1f dB\n", se / 20, mn, mx, sp / 20); }

    // ---------- C) voz + clics encima de la voz ----------
    printf ("C) VOZ + CLICS SOBRE LA VOZ (fase 0.25 s del ciclo, con la voz sonando)\n");
    {   vector<float> in = sy.noisy; vector<double> ts; for (int i = 0; i < 12; ++i) { double t = 4.0 + i * 0.9 + 0.20; ts.push_back (t); addAt (in, clickEvent (0.1f, rng), t); }
        auto o0 = run (in, off, cpu), o1 = run (in, ky, cpu); double a0 = 0, a1 = 0;
        for (double t : ts) { for (int n = (int) (t * SR); n < (int) ((t + 0.03) * SR); ++n) { double e0 = o0[n] - sy.clean[n], e1 = o1[n] - sy.clean[n]; a0 += e0 * e0; a1 += e1 * e1; } }
        printf ("   error en la ventana del clic (vs voz limpia): -%.1f dB con la casilla\n", db (a0 / a1)); }

    // ---------- D) la voz no debe sufrir ----------
    printf ("D) PRESERVACION DE LA VOZ (voz sonora y consonantes; ambas casillas activas)\n");
    {   vector<float> in = sy.noisy; vector<double> tS, tT;
        for (int i = 0; i < 12; ++i) { double t = 4.0 + i * 0.9 + 0.56; tS.push_back (t); addAt (in, sEvent (0.100, 0.03f, rng), t); }
        for (int i = 0; i < 12; ++i) { double t = 4.0 + i * 0.9 + 0.78; tT.push_back (t); vector<float> b = plosiveEvent (0.08f, rng); addAt (in, b, t); }
        auto o0 = run (in, off, cpu), o1 = run (in, both, cpu);
        printf ("   voz sonora (fase 0.1-0.4): %+.2f dB\n", db (sy.voicePower (o1) / sy.voicePower (o0)));
        double es = 0, et = 0, ps = 0; for (double t : tS) es += db (energy (o1, t, t + 0.12) / energy (o0, t, t + 0.12));
        for (double t : tT) { et += db (energy (o1, t, t + 0.02) / energy (o0, t, t + 0.02)); ps += 20 * log10 (peak (o1, t, t + 0.02) / max (1e-9, peak (o0, t, t + 0.02))); }
        printf ("   fricativa /s/ (100 ms): %+.2f dB de media\n", es / 12);
        printf ("   explosiva seca p/t/k (burst 8 ms, nivel de voz): energia %+.1f dB, pico %+.1f dB de media  <- el detector la trata como clic\n", et / 12, ps / 12);
        vector<float> e0 (in.size()), e1 (in.size()); for (size_t n = 0; n < in.size(); ++n) { e0[n] = o0[n] - sy.clean[n]; e1[n] = o1[n] - sy.clean[n]; }
        // SNR solo voz sin eventos
        auto v0 = run (sy.noisy, off, cpu), v1 = run (sy.noisy, both, cpu); vector<float> r0 (sy.total), r1 (sy.total);
        for (int n = 0; n < sy.total; ++n) { r0[n] = v0[n] - sy.clean[n]; r1[n] = v1[n] - sy.clean[n]; }
        printf ("   solo voz (sin eventos): SNR de salida %.2f dB (casillas apagadas) vs %.2f dB (activas)\n",
                db (Synth::pw (sy.clean, 3 * 48000, sy.total) / Synth::pw (r0, 3 * 48000, sy.total)),
                db (Synth::pw (sy.clean, 3 * 48000, sy.total) / Synth::pw (r1, 3 * 48000, sy.total))); }

    // ---------- F) sin eventos: las casillas no deben alterar nada ----------
    printf ("F) SIN EVENTOS (solo ruido de fondo, 14 s): efecto de activar las casillas\n");
    {   auto o0 = run (sy.noise, off, cpu), o1 = run (sy.noise, both, cpu);
        printf ("   diferencia de nivel (4-14 s): %+.3f dB\n", db (energy (o1, 4, 14) / energy (o0, 4, 14)));
        double md = 0; for (size_t n = 4 * 48000; n < o0.size(); ++n) md = max (md, (double) fabs (o1[n] - o0[n]));
        printf ("   diferencia maxima muestra a muestra: %.2e\n", md); }

    // ---------- E) CPU Y ROBUSTEZ ----------
    printf ("E) CPU Y ROBUSTEZ\n");
    {   double c0, c1, c2; run (sy.noisy, off, c0); run (sy.noisy, ky, c1); run (sy.noisy, both, c2);
        printf ("   CPU mono: apagado %.2f %% | teclado %.2f %% | respiracion+teclado %.2f %%\n", c0, c1, c2);
        run (sy.noisy, both, c2, 2); printf ("   CPU estereo con ambas: %.2f %%\n", c2);
        for (double r : { 22050.0, 44100.0, 88200.0, 96000.0, 192000.0 }) { nsup::Denoiser d; d.prepare (r, 2); d.setParams (both);
            vector<float> a (r * 2), b (r * 2); for (size_t i = 0; i < a.size(); ++i) a[i] = b[i] = 0.3f * sinf (i * 0.03f) + 0.05f * rng.w();
            float* ch[2] = { a.data(), b.data() }; d.process (ch, (int) a.size()); bool bad = false; for (float v : a) if (!isfinite (v)) bad = true;
            printf ("   fs=%.0f: %s\n", r, bad ? "NaN/Inf (MAL)" : "ok"); }
        nsup::Denoiser d; d.prepare (48000, 2); d.setParams (both); vector<float> z (48000, 0.f), z2 (48000, 0.f); for (int i = 0; i < 48000; i += 5) { z[i] = 1.f; z2[i] = -1.f; }
        float* ch[2] = { z.data(), z2.data() }; d.process (ch, 48000); bool bad = false; for (float v : z) if (!isfinite (v)) bad = true;
        printf ("   impulsos de escala completa: %s\n", bad ? "NaN/Inf (MAL)" : "ok"); }
}
