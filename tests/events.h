// Generadores sinteticos de eventos para las pruebas (respiracion, clics de teclado, fricativas, explosivas).
#pragma once
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
static const double SR_EV = 48000;
using namespace std;
// ----------------------------------------------------------- generadores
struct Rng { mt19937 g { 7 }; normal_distribution<float> n { 0.f, 1.f }; float w() { return n (g); } };
static vector<float> breathEvent (double seconds, float amp, Rng& r)
{   // soplo: ruido LF-dominante (rumble 120 Hz + banda 1.5 kHz) + algo de aire, envolvente suave
    int len = (int) (seconds * SR_EV); vector<float> x (len); float a = 0, b = 0, pk = 0;
    for (int i = 0; i < len; ++i) {
        a += 0.12f * (r.w() - a);      // LP ~ 1.5 kHz
        b += 0.015f * (r.w() - b);     // LP ~ 120 Hz
        double t = (double) i / len, env = pow (sin (3.14159265 * pow (t, 0.8)), 2);
        x[i] = (float) env * (a * 1.0f + b * 6.0f + 0.1f * r.w()); pk = max (pk, fabs (x[i])); }
    for (auto& v : x) v *= amp / pk; return x;
}
static vector<float> clickEvent (float amp, Rng& r)
{   // clic de tecla: rafaga HF (2-8 kHz) de ~3 ms de constante + "thock" de 700 Hz
    int len = (int) (0.040 * SR_EV); vector<float> x (len); float lp = 0, lp2 = 0, pk = 0;
    for (int i = 0; i < len; ++i) {
        double t = i / SR_EV; float w = r.w(); lp += 0.2f * (w - lp); lp2 += 0.04f * (w - lp2);
        float hf = lp - lp2; double env = exp (-t / 0.003) * min (1.0, t / 0.0004);
        x[i] = (float) (env * hf * 3.0 + 0.5 * exp (-t / 0.007) * sin (2 * 3.14159265 * 700 * t) * min (1.0, t / 0.0004)); pk = max (pk, fabs (x[i])); }
    for (auto& v : x) v *= amp / pk; return x;
}
static vector<float> plosiveEvent (float amp, Rng& r)   // explosiva seca (p/t/k): rafaga HF de ~8 ms, ataque rapido
{
    int len = (int) (0.020 * SR_EV); vector<float> x (len); float lp = 0, lp2 = 0, pk = 0;
    for (int i = 0; i < len; ++i) { float w = r.w(); lp += 0.4f * (w - lp); lp2 += 0.08f * (w - lp2); float hf = lp - lp2;
        double t = i / SR_EV, env = min (1.0, t / 0.0005) * exp (-t / 0.0025); x[i] = (float) (env * hf); pk = max (pk, fabs (x[i])); }
    for (auto& v : x) v *= amp / pk;
    return x;
}
static vector<float> sEvent (double seconds, float amp, Rng& r)   // fricativa /s/: ruido 3-8 kHz, subida 15 ms
{
    int len = (int) (seconds * SR_EV); vector<float> x (len); float lp = 0, lp2 = 0, pk = 0;
    for (int i = 0; i < len; ++i) { float w = r.w(); lp += 0.5f * (w - lp); lp2 += 0.2f * (w - lp2); float hf = lp - lp2;
        double t = i / SR_EV, env = min (1.0, t / 0.015) * min (1.0, (seconds - t) / 0.02); x[i] = (float) (env * hf); pk = max (pk, fabs (x[i])); }
    for (auto& v : x) v *= amp / pk; return x;
}
static void addAt (vector<float>& base, const vector<float>& ev, double t) { int s = (int) (t * SR_EV); for (size_t i = 0; i < ev.size() && s + i < base.size(); ++i) base[s + i] += ev[i]; }

