// Emula la secuencia de llamadas de Equalizer APO (VSTPluginInstance.cpp) contra la DLL/SO del plugin.
#include "../Source/Vst2Abi.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#ifdef _WIN32
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif
#include "synth.h"
#include "events.h"
using namespace std;

static int automateCalls = 0, ioChanged = 0;
static VstIntPtr VSTCALL hostCb (AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float)
{
    if (op == audioMasterVersion) return 2400;
    if (op == audioMasterAutomate) ++automateCalls;
    if (op == audioMasterIOChanged) ++ioChanged;
    return 0;
}
static int fails = 0;
#define CHECK(c, msg) do { bool ok_ = (c); printf("  [%s] %s\n", ok_ ? "OK" : "FALLO", msg); if (!ok_) ++fails; } while (0)

int main (int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "./libld.so";
#ifdef _WIN32
    HMODULE lib = LoadLibraryA (path);
    if (!lib) { printf ("no se pudo cargar %s (err %lu)\n", path, GetLastError()); return 2; }
    auto mainFn = (AEffect* (*)(HostCallback)) GetProcAddress (lib, "VSTPluginMain");
    auto altMain = (AEffect* (*)(HostCallback)) GetProcAddress (lib, "main");
#else
    void* lib = dlopen (path, RTLD_NOW);
    if (!lib) { printf ("no se pudo cargar: %s\n", dlerror()); return 2; }
    auto mainFn = (AEffect* (*)(HostCallback)) dlsym (lib, "VSTPluginMain");
    AEffect* (*altMain)(HostCallback) = (AEffect* (*)(HostCallback)) dlsym (lib, "main");
#endif
    printf ("Carga y exportaciones\n");
    CHECK (mainFn != nullptr, "exporta VSTPluginMain");
#ifdef _WIN32
    CHECK (altMain != nullptr, "exporta tambien 'main' (hosts antiguos)");
#else
    (void) altMain;
#endif
    if (!mainFn) return 1;

    AEffect* e = mainFn (hostCb);
    CHECK (e && e->magic == kEffectMagic, "magic == 'VstP'");
    e->user = (void*) 0x1234; // APO pisa 'user'
    auto d = [&](int op, int idx = 0, intptr_t val = 0, void* ptr = nullptr, float opt = 0.f) { return e->dispatcher (e, op, idx, val, ptr, opt); };

    d (effOpen);
    printf ("Datos del plugin\n");
    CHECK (e->numInputs == 2 && e->numOutputs == 2, "2 entradas / 2 salidas");
    CHECK ((e->flags & effFlagsCanReplacing) != 0, "soporta processReplacing");
    CHECK ((e->flags & (1 << 5)) == 0, "NO declara chunks (APO guardara 'Nombre valor' legible)");
    CHECK (e->process && e->processReplacing, "punteros process/processReplacing no nulos");
    char buf[256]; memset (buf, 0, sizeof buf); d (effGetEffectName, 0, 0, buf);
    printf ("  nombre: '%s'\n", buf);

    map<string, int> idx;
    for (int i = 0; i < e->numParams; ++i)
    {
        memset (buf, 0, sizeof buf); d (effGetParamName, i, 0, buf); idx[buf] = i;
        char lab[256] = {0}, dis[256] = {0}; d (effGetParamLabel, i, 0, lab); d (effGetParamDisplay, i, 0, dis);
        printf ("  param %d: %-7s = %.3f  (%s %s)\n", i, buf, e->getParameter (e, i), dis, lab);
    }
    CHECK (idx.size() == 7 && idx.count ("Gate") && idx.count ("Reduce") && idx.count ("Breath") && idx.count ("Keys"), "7 parametros con nombres unicos (incl. Breath y Keys)");
    CHECK (e->getParameter (e, idx["Breath"]) == 0.f && e->getParameter (e, idx["Keys"]) == 0.f, "casillas Respiracion y Teclado apagadas por defecto");

    printf ("Editor (APO llama getRect ANTES de abrir)\n");
    ERect* r = nullptr; d (effEditGetRect, 0, 0, &r);
    CHECK (r && r->right - r->left > 100 && r->bottom - r->top > 100, "effEditGetRect devuelve un rect valido sin abrir el editor");

    // --- como APO: effSetSampleRate, effSetBlockSize, setParameter por nombre, mains, start ---
    const double sr = 48000; const int blk = 480;
    d (effSetSampleRate, 0, 0, nullptr, (float) sr); d (effSetBlockSize, 0, blk);
    printf ("  latencia declarada: %d muestras\n", e->initialDelay);
    e->setParameter (e, idx["Reduce"], 18.f / 40.f); e->setParameter (e, idx["Sens"], 0.5f);
    e->setParameter (e, idx["Smooth"], 0.6f);        e->setParameter (e, idx["Output"], 0.5f);
    d (effMainsChanged, 0, 1); d (effStartProcess);

    Synth s (sr, 20, 0.3f);
    auto runPass = [&](float gateNorm, bool inPlace) {
        e->setParameter (e, idx["Gate"], gateNorm); d (effMainsChanged, 0, 1);
        vector<float> L (s.noisy), R (s.noisy), oL (s.total), oR (s.total);
        for (int i = 0; i < s.total; i += blk)
        {
            int n = min (blk, s.total - i);
            float* in[2] = { L.data() + i, R.data() + i };
            float* out[2] = { inPlace ? in[0] : oL.data() + i, inPlace ? in[1] : oR.data() + i };
            e->processReplacing (e, in, out, n);
        }
        return inPlace ? L : oL;
    };
    int lat = e->initialDelay;
    auto align = [&](const vector<float>& v){ vector<float> a (v.size(), 0.f); for (size_t n = 0; n + lat < v.size(); ++n) a[n] = v[n + lat]; return a; };

    printf ("Proceso (buffers separados, como APO)\n");
    auto o0 = align (runPass (0.f, false));
    double gapIn = s.gapPower (s.noisy, lat);
    printf ("  gate OFF: atenuacion en pausas %.1f dB\n", 10 * log10 (gapIn / s.gapPower (o0, lat)));
    CHECK (10 * log10 (gapIn / s.gapPower (o0, lat)) > 10.0, "supresion de ruido activa (>10 dB en pausas)");

    float noisePeak = 0; for (int n = 0; n < 3 * 48000; ++n) noisePeak = max (noisePeak, fabs (s.noisy[n]));
    float thrDb = 20 * log10 (noisePeak * 1.5f); float gateNorm = (thrDb + 80.f) / 80.f;
    auto o1 = align (runPass (gateNorm, false));
    char disp[256] = {0}; d (effGetParamDisplay, idx["Gate"], 0, disp);
    printf ("  gate %s dB (norm %.3f): atenuacion en pausas %.1f dB\n", disp, gateNorm, 10 * log10 (gapIn / s.gapPower (o1, lat)));
    CHECK (10 * log10 (gapIn / s.gapPower (o1, lat)) > 35.0, "el gate apaga las pausas (>35 dB)");
    CHECK (fabs (10 * log10 (s.voicePower (o1) / s.voicePower (o0))) < 1.0, "el gate no daña la voz (<1 dB de cambio)");

    auto o2 = align (runPass (gateNorm, true));
    double md = 0; for (int n = 0; n < s.total; ++n) md = max (md, (double) fabs (o2[n] - o1[n]));
    CHECK (md < 1e-6, "modo in-place (in == out) identico al de buffers separados");

    auto o3 = align (runPass (0.f, false));
    double md2 = 0; for (int n = 0; n < s.total; ++n) md2 = max (md2, (double) fabs (o3[n] - o0[n]));
    CHECK (md2 < 1e-6, "tras effMainsChanged la salida es reproducible");

    e->setParameter (e, idx["Output"], 1.0f); // +12 dB
    auto o4 = align (runPass (0.f, false));
    double g = 10 * log10 (Synth::pw (o4, 4 * 48000, s.total) / Synth::pw (o0, 4 * 48000, s.total));
    printf ("  Salida +12 dB -> %.1f dB\n", g);
    CHECK (fabs (g - 12.0) < 0.5, "el mando Salida aplica +12 dB");
    e->setParameter (e, idx["Output"], 0.5f);

    printf ("Casillas Respiracion / Teclado (extremo a extremo por la API del plugin)\n");
    {
        Synth se (sr, 18, 0.1f); Rng rng; vector<float> in = se.noise; vector<double> tc, tb;
        for (int i = 0; i < 20; ++i) { double t = 4.0 + i * 0.43; tc.push_back (t); addAt (in, clickEvent (0.1f, rng), t); }
        for (int i = 0; i < 4; ++i)  { double t = 12.0 + i * 1.4; tb.push_back (t); addAt (in, breathEvent (0.5, 0.03f, rng), t); }
        auto runPlug = [&](float br, float ky) {
            e->setParameter (e, idx["Breath"], br); e->setParameter (e, idx["Keys"], ky); e->setParameter (e, idx["Gate"], 0.f);
            d (effMainsChanged, 0, 1);
            vector<float> L (in), R (in), oL (in.size()), oR (in.size());
            for (size_t i = 0; i < in.size(); i += blk) { int n = (int) min<size_t> (blk, in.size() - i);
                float* pin[2] = { L.data() + i, R.data() + i }; float* pout[2] = { oL.data() + i, oR.data() + i }; e->processReplacing (e, pin, pout, n); }
            return align (oL); };
        auto en = [&](const vector<float>& x, double t0, double t1) { double a = 0; for (int n = (int) (t0 * sr); n < (int) (t1 * sr); ++n) a += (double) x[n] * x[n]; return a; };
        auto base = runPlug (0, 0), keys = runPlug (0, 1), breath = runPlug (1, 0);
        double ck = 0; for (double t : tc) ck += 10 * log10 (en (base, t, t + 0.03) / en (keys, t, t + 0.03)); ck /= tc.size();
        double cb = 0; for (double t : tb) cb += 10 * log10 (en (base, t + 0.1, t + 0.5) / en (breath, t + 0.1, t + 0.5)); cb /= tb.size();
        printf ("  Teclado=On: clics -%.1f dB | Respiracion=On: soplos -%.1f dB (vs casillas apagadas)\n", ck, cb);
        CHECK (ck > 6.0, "casilla Teclado atenua los clics (>6 dB) a traves de la API");
        CHECK (cb > 6.0, "casilla Respiracion atenua los soplos (>6 dB) a traves de la API");
        double dK = 0; for (int n = 0; n < 3 * 48000; ++n) dK = max (dK, (double) fabs (keys[n] - base[n]));
        CHECK (dK < 1e-6, "con las casillas activas y sin eventos (primeros 3 s) la salida no cambia");
        char dd[16] = {0}; e->setParameter (e, idx["Keys"], 1.f); d (effGetParamDisplay, idx["Keys"], 0, dd); CHECK (strcmp (dd, "On") == 0, "Teclado muestra 'On'");
        e->setParameter (e, idx["Keys"], 0.f); d (effGetParamDisplay, idx["Keys"], 0, dd); CHECK (strcmp (dd, "Off") == 0, "Teclado muestra 'Off'");
        e->setParameter (e, idx["Breath"], 0.f); e->setParameter (e, idx["Keys"], 0.f);
    }

    printf ("Cambio de frecuencia de muestreo y bloques raros\n");
    d (effStopProcess); d (effMainsChanged, 0, 0);
    d (effSetSampleRate, 0, 0, nullptr, 96000.f); d (effSetBlockSize, 0, 1024);
    CHECK (ioChanged > 0, "avisa al host del cambio de latencia (audioMasterIOChanged)");
    d (effMainsChanged, 0, 1); d (effStartProcess);
    bool finite = true; vector<float> a (7919), b2 (7919);
    for (int i = 0; i < 7919; ++i) a[i] = b2[i] = 0.2f * sinf (i * 0.05f);
    for (int n : { 1, 3, 17, 480, 1024, 4096, 7919 }) {
        float* in[2] = { a.data(), b2.data() }; float* out[2] = { a.data(), b2.data() };
        e->processReplacing (e, in, out, n);
        for (int i = 0; i < n; ++i) if (!isfinite (a[i]) || !isfinite (b2[i])) finite = false; }
    CHECK (finite, "bloques de 1..7919 muestras a 96 kHz sin NaN/Inf");
    float* nul[2] = { nullptr, nullptr }; vector<float> o5 (512), o6 (512); float* out[2] = { o5.data(), o6.data() };
    e->processReplacing (e, nul, out, 512); CHECK (true, "entradas nulas no provocan fallo");

    printf ("Parametros y cierre\n");
    e->setParameter (e, 0, 5.f); CHECK (e->getParameter (e, 0) == 1.f, "valores fuera de rango se limitan a 0..1");
    e->setParameter (e, 99, 0.5f); CHECK (e->getParameter (e, 99) == 0.f, "indice invalido ignorado");
    d (effStopProcess); d (effMainsChanged, 0, 0); d (effClose);
    CHECK (true, "effClose libera sin fallo");
    printf ("\n%s (%d fallos)\n", fails ? "RESULTADO: HAY FALLOS" : "RESULTADO: TODO OK", fails);
    return fails ? 1 : 0;
}
