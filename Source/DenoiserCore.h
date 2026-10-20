// ============================================================================
//  DenoiserCore.h  -  Supresor de ruido espectral, ligero, SIN dependencias.
//
//  Pipeline (todo en float, sin asignaciones en el hilo de audio):
//    STFT (sqrt-Hann, 75 % solape)
//      -> estimador de ruido MCRA (minimos + probabilidad de voz)
//      -> SNR a priori "decision-directed" (Ephraim-Malah)
//      -> ganancia MMSE-LSA (tabla de E1) + suelo de reduccion
//      -> suavizado de ganancia en frecuencia
//    -> iSTFT por overlap-add
//
//  Trucos de eficiencia:
//   * Estereo en UNA sola FFT compleja (L en la parte real, R en la imaginaria).
//   * Ganancia enlazada L/R: no se mueve la imagen estereo.
//   * Memoria total: unos pocos KB. CPU: <1 % de un nucleo.
// ============================================================================
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace nsup
{
struct Params
{
    float reductionDb = 18.0f; // atenuacion maxima (0..40 dB)
    float sensitivity = 0.5f;  // 0..1  (mas = mas agresivo)
    float smoothing   = 0.6f;  // 0..1  (mas = menos "ruido musical", mas suave)
    float gateThresholdDb = -80.0f; // umbral del noise gate; <= -79.5 => gate apagado
    bool  suppressBreath  = false;  // casilla "Respiracion": atenua soplos/respiracion cerca del microfono
    bool  suppressKeys    = false;  // casilla "Teclado": atenua clics de teclas
};

class Denoiser
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        nch  = std::max (1, std::min (2, numChannels));
        N    = sampleRate <= 50000.0 ? 512 : (sampleRate <= 100000.0 ? 1024 : 2048);
        hop  = N / 4;
        half = N / 2;
        mask = N - 1;

        const double fps = sampleRate / hop;
        L = std::max (8, (int) std::lround (0.8 * fps)); // ventana de minimos: 0.8-1.6 s

        const double pi = 3.14159265358979323846;
        win.resize (N); wi.resize (N / 2); wr.resize (N / 2); rev.resize (N);
        for (int n = 0; n < N; ++n)
            win[n] = (float) std::sqrt (0.5 * (1.0 - std::cos (2.0 * pi * n / N)));
        for (int k = 0; k < N / 2; ++k)
        {
            wr[k] =  (float) std::cos (2.0 * pi * k / N);
            wi[k] = -(float) std::sin (2.0 * pi * k / N);
        }
        int bits = 0; while ((1 << bits) < N) ++bits;
        for (int i = 0; i < N; ++i)
        {
            int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[i] = r;
        }
        for (int c = 0; c < 2; ++c) { inRing[c].assign (N, 0.f); outRing[c].assign (N, 0.f); }
        // Noise gate: el detector mira la ENTRADA y su ganancia se aplica N muestras despues,
        // junto con el audio ya procesado => anticipacion (look-ahead) gratuita.
        gateRing.assign (N, 1.f);
        gateLook = std::min (N - 1, (int) std::lround (0.004 * sampleRate));   // 4 ms
        gateEnvRel  = (float) std::exp (-1.0 / (0.010 * sampleRate));          // caida de envolvente 10 ms
        gateCoefA   = (float) (1.0 - std::exp (-1.0 / (0.0015 * sampleRate))); // apertura 1.5 ms
        gateCoefR   = (float) (1.0 - std::exp (-1.0 / (0.040 * sampleRate)));  // cierre 40 ms
        gateHoldLen = (int) std::lround (0.080 * sampleRate);                  // mantener 80 ms
        re.assign (N, 0.f); im.assign (N, 0.f);
        const int B = half + 1;
        for (auto* v : { &P, &Pf, &S, &Smin, &Stmp, &pS, &lam, &Aprev2, &G, &Gs })
            v->assign (B, 0.f);

        // Tabla de exp(0.5*E1(v)) para la ganancia LSA, v en [1e-4, 30]
        lsaTab.resize (kTab + 1);
        for (int i = 0; i <= kTab; ++i)
        {
            const double x = std::exp (kLnMin + (kLnMax - kLnMin) * i / kTab);
            lsaTab[i] = (float) std::exp (0.5 * expint1 (x));
        }

        // ---- Detectores de respiracion / teclado: bandas (en bins) y buffers de tono ----
        auto bin = [&] (double hz) { return std::max (0, std::min (half, (int) std::lround (hz * N / sampleRate))); };
        kLfA = bin (100);  kLfB = bin (400);
        kRamp0 = bin (300); kRamp1 = bin (800);                          // zona atenuada por un clic
        kHfA = bin (1800); kHfB = bin (12000);                            // banda de deteccion
        kBtA = bin (80);   kBtB = bin (8000);  kBLoB = bin (1000); kBHiA = bin (2000);
        dec = std::max (1, (int) std::lround (sampleRate / 12000.0));   // detector de tono a ~12 kHz
        const double fsd = sampleRate / dec;
        vLagMin = std::max (4, (int) std::lround (0.0025 * fsd));        // 400 Hz
        vLagMax = std::max (vLagMin + 4, (int) std::lround (0.0133 * fsd)); // 75 Hz
        vWin    = std::max (32, (int) std::lround (0.032 * fsd));
        vLen    = vWin + vLagMax + 8;
        vBuf.assign (vLen, 0.f); vTmp.assign (vLen, 0.f); vCs.assign (vLen + 1, 0.f);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inRing[c].begin(), inRing[c].end(), 0.f);
            std::fill (outRing[c].begin(), outRing[c].end(), 0.f);
        }
        for (auto* v : { &P, &Pf, &S, &Smin, &Stmp, &pS, &lam, &Aprev2 }) std::fill (v->begin(), v->end(), 0.f);
        std::fill (G.begin(), G.end(), 1.f);
        std::fill (gateRing.begin(), gateRing.end(), 1.f);
        gEnv = 0.f; gGain = 1.f; gOpen = false; gHold = 0;
        std::fill (vBuf.begin(), vBuf.end(), 0.f); vw = 0; decCnt = 0; decAcc = hpX = hpY = vLast = 0.f;
        for (int i = 0; i < 8; ++i) hfH[i] = lfH[i] = 0.f;
        histPos = 0; clickGain = clickTarget = breathGain = 1.f; clickHold = 0; bScore = 0; bLevel = 0.f;
        wp = rp = cnt = 0; minCnt = 0; frames = 0;
    }

    int  getLatencySamples() const { return N; }
    void setParams (const Params& p) { prm = p; }

    // Procesa en sitio. ch[0..nch-1] -> numSamples muestras cada uno.
    void process (float* const* ch, int numSamples)
    {
        const bool  gateOn   = prm.gateThresholdDb > -79.5f;
        const float thr      = std::pow (10.f, prm.gateThresholdDb / 20.f);
        const float thrClose = thr * 0.7079f; // histeresis de 3 dB
        const bool  breathOn = prm.suppressBreath;

        for (int n = 0; n < numSamples; ++n)
        {
            // --- detector del gate (sobre la entrada) ---
            float a = std::fabs (ch[0][n]);
            if (nch > 1) a = std::max (a, std::fabs (ch[1][n]));
            if (breathOn) feedVoicing (nch > 1 ? 0.5f * (ch[0][n] + ch[1][n]) : ch[0][n]);
            gEnv = a > gEnv ? a : gEnv * gateEnvRel;
            if (gEnv < 1e-20f) gEnv = 0.f;

            float target = 1.f;
            if (gateOn)
            {
                if (!gOpen) { if (gEnv > thr) { gOpen = true; gHold = gateHoldLen; } }
                else if (gEnv > thrClose) gHold = gateHoldLen;
                else if (gHold > 0) --gHold;
                else gOpen = false;
                target = gOpen ? 1.f : kGateFloor;
            }
            gGain += (target - gGain) * (target > gGain ? gateCoefA : gateCoefR);
            const float gOut = gateRing[(wp + gateLook) & mask]; // ganancia de "hace N - look muestras"
            gateRing[wp] = gGain;

            for (int c = 0; c < nch; ++c) inRing[c][wp] = ch[c][n];
            for (int c = 0; c < nch; ++c) { ch[c][n] = outRing[c][rp] * gOut; outRing[c][rp] = 0.f; }
            wp = (wp + 1) & mask; rp = (rp + 1) & mask;
            if (++cnt == hop) { cnt = 0; processFrame(); }
        }
    }

private:
    // ---------------- FFT radix-2 (in situ, tablas precalculadas) ------------
    void fft (float* a, float* b) const
    {
        for (int i = 0; i < N; ++i) { const int j = rev[i]; if (j > i) { std::swap (a[i], a[j]); std::swap (b[i], b[j]); } }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int h = len >> 1, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0, t = 0; k < h; ++k, t += step)
                {
                    const float c = wr[t], s = wi[t];
                    const int p = i + k, q = p + h;
                    const float xr = a[q] * c - b[q] * s, xi = a[q] * s + b[q] * c;
                    a[q] = a[p] - xr; b[q] = b[p] - xi; a[p] += xr; b[p] += xi;
                }
        }
    }

    // E1(x) (integral exponencial), Numerical-Recipes style. Solo en prepare().
    static double expint1 (double x)
    {
        const double euler = 0.5772156649015329;
        if (x <= 1.0)
        {
            double sum = -std::log (x) - euler, term = 1.0;
            for (int k = 1; k < 60; ++k)
            {
                term *= -x / k;
                const double d = -term / k;
                sum += d;
                if (std::fabs (d) < 1e-12) break;
            }
            return sum;
        }
        double b = x + 1.0, c = 1e30, d = 1.0 / b, h = d;
        for (int i = 1; i < 200; ++i)
        {
            const double a = -(double) i * i;
            b += 2.0; d = 1.0 / (a * d + b); c = b + a / c;
            const double del = c * d; h *= del;
            if (std::fabs (del - 1.0) < 1e-10) break;
        }
        return h * std::exp (-x);
    }

    float lsaTerm (float v) const
    {
        if (v >= 30.f) return 1.f;
        const float x = std::log (std::max (v, 1e-4f));
        const float f = (x - (float) kLnMin) * (kTab / (float) (kLnMax - kLnMin));
        int i = (int) f; if (i < 0) i = 0; if (i >= kTab) i = kTab - 1;
        const float fr = f - (float) i;
        return lsaTab[i] + (lsaTab[i + 1] - lsaTab[i]) * fr;
    }

    // ---------------- Estimador de ruido (MCRA, Cohen 2003) ------------------
    void estimateNoise()
    {
        const float aS = 0.85f, delta = 5.0f, aP = 0.2f, aD = 0.95f, cap = 8.0f;
        const int B = half + 1;

        for (int k = 0; k < B; ++k)
            Pf[k] = (k > 0 && k < half) ? 0.25f * P[k - 1] + 0.5f * P[k] + 0.25f * P[k + 1] : P[k];

        if (frames == 0)
        {
            for (int k = 0; k < B; ++k) { S[k] = Smin[k] = Stmp[k] = Pf[k]; lam[k] = P[k]; pS[k] = 0.f; Aprev2[k] = 0.f; }
        }
        const bool learning = frames < 25; // adquisicion rapida inicial
        const bool roll = (++minCnt >= L);
        if (roll) minCnt = 0;

        for (int k = 0; k < B; ++k)
        {
            S[k] = aS * S[k] + (1.f - aS) * Pf[k];
            if (roll) { Smin[k] = std::min (Stmp[k], S[k]); Stmp[k] = S[k]; }
            else      { Smin[k] = std::min (Smin[k], S[k]); Stmp[k] = std::min (Stmp[k], S[k]); }

            const float I = (S[k] > delta * Smin[k]) ? 1.f : 0.f;
            pS[k] = aP * pS[k] + (1.f - aP) * I;
            const float ad = learning ? 0.85f : (aD + (1.f - aD) * pS[k]);
            lam[k] = ad * lam[k] + (1.f - ad) * P[k];
            lam[k] = std::min (lam[k], cap * Smin[k] + 1e-20f); // anti-enganche tras voz
        }
        if (frames < 1000000) ++frames;
    }

    // ---------------- Respiracion y teclado -----------------------------------
    // Detector de tono (voz sonora): correlacion cruzada normalizada sobre la senal
    // diezmada a ~12 kHz y filtrada pasa-altos (~95 Hz). Valor ~0 = ruido/soplo, ~1 = voz sonora.
    void feedVoicing (float x)
    {
        decAcc += x;
        if (++decCnt < dec) return;
        const float d = decAcc / (float) dec; decAcc = 0.f; decCnt = 0;
        const float y = d - hpX + 0.95f * hpY; hpX = d; hpY = y;
        if (std::fabs (hpY) < 1e-25f) hpY = 0.f;
        vBuf[vw] = y; if (++vw >= vLen) vw = 0;
    }

    float computeVoicing()
    {
        for (int i = 0; i < vLen; ++i) { int j = vw + i; if (j >= vLen) j -= vLen; vTmp[i] = vBuf[j]; }
        vCs[0] = 0.f;
        for (int i = 0; i < vLen; ++i) vCs[i + 1] = vCs[i] + vTmp[i] * vTmp[i];
        const int a = vLen - vWin;
        const float e0 = vCs[vLen] - vCs[a];
        if (e0 < 1e-12f) return 0.f;
        float best = 0.f;
        for (int tau = vLagMin; tau <= vLagMax; ++tau)
        {
            const float* x = vTmp.data() + a;
            const float* y = x - tau;
            float c = 0.f;
            for (int n = 0; n < vWin; ++n) c += x[n] * y[n];
            const float e1 = vCs[a + vWin - tau] - vCs[a - tau];
            const float v = c / std::sqrt (e0 * e1 + 1e-20f);
            if (v > best) best = v;
        }
        return best;
    }

    void analyzeEvents()
    {
        const float eps = 1e-20f;
        float eHf = 0.f, nHf = 0.f, eLf = 0.f, nLf = 0.f;
        for (int k = kHfA; k <= kHfB; ++k) { eHf += P[k]; nHf += lam[k]; }
        for (int k = kLfA; k <= kLfB; ++k) { eLf += P[k]; nLf += lam[k]; }
        const int cur = histPos, prev = (histPos + 4) & 7;   // trama actual y la de hace 4 (sin solape)
        hfH[cur] = eHf; lfH[cur] = eLf;
        const float ePrevHf = hfH[prev], ePrevLf = lfH[prev];
        histPos = (histPos + 1) & 7;
        dbgHfOverNoise = eHf / (nHf + eps);

        // ---- TECLADO: subida brusca y SELECTIVA en agudos (no en graves) -------------------
        // Un clic sube >12 dB en agudos respecto a hace ~10 ms sin que suban igual los graves;
        // la voz sonora sube en graves y agudos a la vez, asi que no cumple la condicion.
        if (prm.suppressKeys && frames > 40)
        {
            const float baseHf = std::max (ePrevHf, nHf), baseLf = std::max (ePrevLf, nLf);
            const float jHf = 10.f * std::log10 ((eHf + eps) / (baseHf + eps));
            const float jLf = 10.f * std::log10 ((eLf + eps) / (baseLf + eps));
            dbgJHf = jHf; dbgJLf = jLf;
            if (jHf > kClickJumpDb && (jHf - jLf) > kClickSelDb && eHf > 6.f * nHf)
            {
                const float depthDb = std::min (kClickMaxDepthDb, jHf - 4.f);
                clickTarget = std::min (clickTarget, std::pow (10.f, -depthDb / 20.f));
                clickHold = kClickHoldFrames;
            }
        }
        if (prm.suppressKeys && clickHold > 0) { --clickHold; clickGain = clickTarget; }
        else { clickTarget = 1.f; clickHold = 0; clickGain += (1.f - clickGain) * 0.35f; }
        if (clickGain > 0.999f) clickGain = 1.f;

        // ---- RESPIRACION: soplo sin tono, dominado por graves, claramente sobre el ruido ----
        if (prm.suppressBreath)
        {
            float eT = 0.f, nT = 0.f, eLo = 0.f, eHi = 0.f;
            // referencia de ruido = MINIMO de largo plazo (0.8-1.6 s) x1.7: un soplo no la contamina
            for (int k = kBtA; k <= kBtB; ++k)  { eT += P[k]; nT += 1.7f * Smin[k]; }
            for (int k = kBtA; k <= kBLoB; ++k) eLo += P[k];
            for (int k = kBHiA; k <= kBtB; ++k) eHi += P[k];
            if ((frames & 1) == 0) vLast = computeVoicing();

            dbgLoud = eT / (nT + eps); dbgTilt = eHi / (eLo + eps);
            const bool wasActive = bScore >= kBreathMinFrames;
            const float loudThr = (wasActive ? 0.7f : 1.f) * kBreathLoudRatio;   // histeresis: una vez activo, mas permisivo
            const bool like = (eT > loudThr * (nT + eps)) && (vLast < kBreathVoicedMax) && (eHi < kBreathTilt * eLo);
            if (vLast > kBreathVoicedHard) bScore = 0;                   // hay voz sonora
            else if (like) bScore = std::min (bScore + 1, 60);
            else bScore = std::max (0, bScore - 2);
            if (like) bLevel = (bScore <= 1) ? eT : 0.9f * bLevel + 0.1f * eT;
            if (wasActive && eT > kBreathJump * bLevel) bScore = 0;      // aparece algo mucho mas fuerte: no es respiracion

            const float tgt = (bScore >= kBreathMinFrames) ? kBreathGain : 1.f;
            const float c = tgt < breathGain ? 0.12f : (vLast > kBreathVoicedHard ? 0.30f : 0.10f);
            breathGain += (tgt - breathGain) * c;
        }
        else { bScore = 0; breathGain += (1.f - breathGain) * 0.2f; }
        if (breathGain > 0.999f) breathGain = 1.f;
    }

    // ---------------- Ganancia MMSE-LSA -------------------------------------
    void computeGain()
    {
        const int B = half + 1;
        const float gmin  = std::pow (10.f, -prm.reductionDb / 20.f);
        const float over  = 1.f + 2.f * prm.sensitivity;          // 1..3 (0..+4.8 dB)
        const float beta  = 0.90f + 0.09f * prm.smoothing;        // 0.90..0.99
        const float xiMin = std::max (1e-5f, 0.5f * gmin * gmin);

        for (int k = 0; k < B; ++k)
        {
            const float lamE = std::max (lam[k] * over, 1e-12f);
            const float gam  = P[k] / lamE;
            float xi = beta * Aprev2[k] / lamE + (1.f - beta) * std::max (gam - 1.f, 0.f);
            xi = std::max (xi, xiMin);
            const float q = xi / (1.f + xi);
            float g = q * lsaTerm (q * gam);
            g = std::max (std::min (g, 1.f), gmin);
            if (breathGain < 1.f) g *= breathGain;                       // respiracion: banda ancha
            if (clickGain < 1.f && k >= kRamp0)                          // teclado: desde ~300 Hz (rampa 300-800 Hz)
            {
                const float r = k >= kRamp1 ? 1.f : (float) (k - kRamp0) / (float) std::max (1, kRamp1 - kRamp0);
                g *= 1.f + (clickGain - 1.f) * r;
            }
            G[k] = g;
        }
        for (int k = 0; k < B; ++k) // suavizado en frecuencia [.25 .5 .25]
        {
            const float a = G[k > 0 ? k - 1 : 0], c = G[k < half ? k + 1 : half];
            Gs[k] = 0.25f * a + 0.5f * G[k] + 0.25f * c;
            Aprev2[k] = Gs[k] * Gs[k] * P[k];
        }
    }

    void processFrame()
    {
        const bool st = (nch > 1);
        for (int i = 0; i < N; ++i)
        {
            const int idx = (wp + i) & mask;
            re[i] = inRing[0][idx] * win[i];
            im[i] = st ? inRing[1][idx] * win[i] : 0.f;
        }
        fft (re.data(), im.data());

        for (int k = 0; k <= half; ++k)
        {
            const int kk = (N - k) & mask;
            P[k] = 0.25f * (re[k] * re[k] + im[k] * im[k] + re[kk] * re[kk] + im[kk] * im[kk]);
        }
        estimateNoise();
        analyzeEvents();
        computeGain();

        for (int k = 0; k <= half; ++k)
        {
            re[k] *= Gs[k]; im[k] *= Gs[k];
            if (k > 0 && k < half) { re[N - k] *= Gs[k]; im[N - k] *= Gs[k]; }
        }
        fft (im.data(), re.data()); // FFT inversa (sin normalizar) por intercambio re<->im

        const float sc = 0.5f / (float) N; // 1/N (iFFT) * 1/2 (suma de Hann a 75 %)
        for (int i = 0; i < N; ++i)
        {
            const int idx = (rp + i) & mask;
            outRing[0][idx] += re[i] * win[i] * sc;
            if (st) outRing[1][idx] += im[i] * win[i] * sc;
        }
    }

    // ---------------- Estado -------------------------------------------------
    static constexpr int    kTab   = 512;
    static constexpr double kLnMin = -9.210340371976184;  // ln(1e-4)
    static constexpr double kLnMax =  3.401197381662155;  // ln(30)

    static constexpr float kGateFloor = 0.001f; // -60 dB

    // --- Constantes de los detectores (ajustables) ---
    static constexpr float kClickJumpDb = 12.f, kClickSelDb = 5.f, kClickMaxDepthDb = 24.f;
    static constexpr int   kClickHoldFrames = 6;
    static constexpr float kBreathLoudRatio = 3.5f, kBreathVoicedMax = 0.55f, kBreathVoicedHard = 0.65f;
    static constexpr float kBreathTilt = 0.7f, kBreathJump = 6.f, kBreathGain = 0.1f; // -20 dB
    static constexpr int   kBreathMinFrames = 16;                                      // ~43 ms

    Params prm;
    // respiracion / teclado
    int   kLfA = 0, kLfB = 0, kHfA = 0, kHfB = 0, kRamp0 = 0, kRamp1 = 0, kBtA = 0, kBtB = 0, kBLoB = 0, kBHiA = 0;
    int   dec = 4, vLagMin = 30, vLagMax = 160, vWin = 384, vLen = 552, vw = 0, decCnt = 0;
    float decAcc = 0.f, hpX = 0.f, hpY = 0.f, vLast = 0.f;
    std::vector<float> vBuf, vTmp, vCs;
    float hfH[8] = {}, lfH[8] = {};
    int   histPos = 0;
    float clickGain = 1.f, clickTarget = 1.f; int clickHold = 0;
    float breathGain = 1.f, bLevel = 0.f;     int bScore = 0;
    float dbgHfOverNoise = 0.f, dbgJHf = 0.f, dbgJLf = 0.f, dbgLoud = 0.f, dbgTilt = 0.f; // solo diagnostico
    // gate
    std::vector<float> gateRing;
    int   gateLook = 0, gateHoldLen = 0, gHold = 0;
    float gateEnvRel = 0.f, gateCoefA = 0.f, gateCoefR = 0.f, gEnv = 0.f, gGain = 1.f;
    bool  gOpen = false;
    int nch = 1, N = 512, hop = 128, half = 256, mask = 511, L = 300;
    int wp = 0, rp = 0, cnt = 0, minCnt = 0, frames = 0;
    std::vector<float> inRing[2], outRing[2];
    std::vector<float> win, wr, wi, re, im, lsaTab;
    std::vector<int>   rev;
    std::vector<float> P, Pf, S, Smin, Stmp, pS, lam, Aprev2, G, Gs;
};
} // namespace nsup
