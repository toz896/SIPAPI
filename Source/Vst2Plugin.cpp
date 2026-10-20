// ============================================================================
//  Vst2Plugin.cpp - MilsDenoise como DLL VST2 de 64 bits, pensado para
//  Equalizer APO. Sin JUCE, sin dependencias: solo kernel32/user32/gdi32.
//
//  Notas de compatibilidad con Equalizer APO (leido de su codigo fuente):
//   * Llama a effEditGetRect ANTES de effEditOpen -> siempre devolvemos un rect valido.
//   * Pisa AEffect::user -> usamos AEffect::object.
//   * Sin effFlagsProgramChunks guarda "NombreParam valor" en config.txt
//     (editable a mano), asi que NO declaramos chunks.
//   * Los nombres de parametro son cortos (<= 7 caracteres) y sin espacios.
// ============================================================================
#include "Vst2Abi.h"
#include "DenoiserCore.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#endif

namespace
{
constexpr int kNumParams = 7;
enum { pReduce = 0, pSens, pSmooth, pGate, pOutput, pBreath, pKeys };
inline bool isToggle (int i) { return i == pBreath || i == pKeys; }

struct ParamInfo { const char* name; const wchar_t* title; const char* label; float def; };
const ParamInfo kParams[kNumParams] = {
    { "Reduce", L"Reducci\u00f3n",    "dB", 18.f / 40.f },  // 0..40 dB
    { "Sens",   L"Sensibilidad",       "%",  0.50f },        // 0..100 %
    { "Smooth", L"Suavizado",          "%",  0.60f },        // 0..100 %
    { "Gate",   L"Gate (umbral)",      "dB", 0.0f },         // -80..0 dB, 0 = apagado
    { "Output", L"Salida",             "dB", 0.5f },         // -12..+12 dB
    { "Breath", L"Respiraci\u00f3n",   "",   0.0f },         // casilla: 0 = Off, 1 = On
    { "Keys",   L"Teclado",            "",   0.0f },         // casilla: 0 = Off, 1 = On
};

float toReal (int i, float n)
{
    switch (i)
    {
        case pReduce: return 40.f * n;
        case pSens:
        case pSmooth: return 100.f * n;
        case pGate:   return -80.f + 80.f * n;
        case pBreath:
        case pKeys:   return n >= 0.5f ? 1.f : 0.f;
        default:      return -12.f + 24.f * n;
    }
}

void formatValue (int i, float n, char* out, size_t cap)
{
    const float v = toReal (i, n);
    if (isToggle (i)) { snprintf (out, cap, v > 0.5f ? "On" : "Off"); return; }
    if (i == pGate && v <= -79.5f) { snprintf (out, cap, "Off"); return; }
    if (i == pReduce || i == pOutput || i == pGate) snprintf (out, cap, "%.1f", (double) v);
    else snprintf (out, cap, "%.0f", (double) v);
}

void copyStr (void* dst, const char* src, size_t maxLen) // maxLen incluye el NUL
{
    char* d = (char*) dst; size_t i = 0;
    for (; src[i] && i + 1 < maxLen; ++i) d[i] = src[i];
    d[i] = 0;
}

constexpr int kNumSliders = 5;
constexpr int kW = 400, kRowH = 36, kTop = 44, kH = kTop + kNumSliders * kRowH + 44 + 10;

class LdPlugin
{
public:
    explicit LdPlugin (HostCallback cb) : host (cb)
    {
        std::memset (&fx, 0, sizeof (fx));
        fx.magic = kEffectMagic;
        fx.dispatcher = &LdPlugin::dispatch;
        fx.process = &LdPlugin::processAcc;
        fx.setParameter = &LdPlugin::setParam;
        fx.getParameter = &LdPlugin::getParam;
        fx.processReplacing = &LdPlugin::processRepl;
        fx.processDoubleReplacing = nullptr;
        fx.numPrograms = 1; fx.numParams = kNumParams; fx.numInputs = 2; fx.numOutputs = 2;
        fx.flags = effFlagsCanReplacing | effFlagsHasEditor;
        fx.uniqueID = ('M' << 24) | ('D' << 16) | ('n' << 8) | '1';
        fx.version = 1000;
        fx.object = this;
        for (int i = 0; i < kNumParams; ++i) norm[i].store (kParams[i].def);
        rect.top = 0; rect.left = 0; rect.bottom = (int16_t) kH; rect.right = (int16_t) kW;
        denoiser.prepare (48000.0, 2);
        fx.initialDelay = denoiser.getLatencySamples();
        ready.store (true);
    }

    AEffect fx;

private:
    HostCallback host;
    nsup::Denoiser denoiser;
    std::atomic<float> norm[kNumParams];
    std::atomic<bool> ready { false }, dirty { false };
    float lastGain = 1.f;
    ERect rect;

    // ------------------------------------------------------------ audio
    void process (float** in, float** out, int n)
    {
        if (n <= 0 || !out) return;
        for (int c = 0; c < 2; ++c)
        {
            if (!out[c]) continue;
            if (in && in[c]) { if (out[c] != in[c]) std::memcpy (out[c], in[c], sizeof (float) * (size_t) n); }
            else std::memset (out[c], 0, sizeof (float) * (size_t) n);
        }
        if (!ready.load (std::memory_order_acquire) || !out[0] || !out[1]) return;

        nsup::Params p;
        p.reductionDb     = toReal (pReduce, norm[pReduce].load());
        p.sensitivity     = norm[pSens].load();
        p.smoothing       = norm[pSmooth].load();
        p.gateThresholdDb = toReal (pGate, norm[pGate].load());
        p.suppressBreath  = norm[pBreath].load() >= 0.5f;
        p.suppressKeys    = norm[pKeys].load()   >= 0.5f;
        denoiser.setParams (p);
        denoiser.process (out, n);

        const float target = std::pow (10.f, toReal (pOutput, norm[pOutput].load()) / 20.f);
        if (target != lastGain || target != 1.f)
        {
            const float step = (target - lastGain) / (float) n;
            for (int c = 0; c < 2; ++c) { float g = lastGain; for (int i = 0; i < n; ++i) { g += step; out[c][i] *= g; } }
            lastGain = target;
        }
    }
    static void VSTCALL processRepl (AEffect* e, float** in, float** out, VstInt32 n) { ((LdPlugin*) e->object)->process (in, out, n); }
    static void VSTCALL processAcc  (AEffect* e, float** in, float** out, VstInt32 n) { ((LdPlugin*) e->object)->process (in, out, n); }

    // ------------------------------------------------------------ parametros
    static void  VSTCALL setParam (AEffect* e, VstInt32 i, float v)
    { if (i >= 0 && i < kNumParams) { auto* p = (LdPlugin*) e->object; p->norm[i].store (std::min (1.f, std::max (0.f, v))); p->dirty.store (true); } }
    static float VSTCALL getParam (AEffect* e, VstInt32 i)
    { return (i >= 0 && i < kNumParams) ? ((LdPlugin*) e->object)->norm[i].load() : 0.f; }

    // ------------------------------------------------------------ dispatcher
    static VstIntPtr VSTCALL dispatch (AEffect* e, VstInt32 op, VstInt32 idx, VstIntPtr val, void* ptr, float opt)
    {
        auto* p = (LdPlugin*) e->object;
        switch (op)
        {
            case effOpen:  return 0;
            case effClose: p->closeEditor(); delete p; return 0;
            case effGetProgram: return 0;
            case effSetProgram: case effSetProgramName: return 0;
            case effGetProgramName: if (ptr) copyStr (ptr, "Default", 24); return 0;
            case effGetParamName:
                if (ptr && idx >= 0 && idx < kNumParams) { copyStr (ptr, kParams[idx].name, 8); }
                return 0;
            case effGetParamLabel:
                if (ptr && idx >= 0 && idx < kNumParams) { copyStr (ptr, kParams[idx].label, 8); }
                return 0;
            case effGetParamDisplay:
                if (ptr && idx >= 0 && idx < kNumParams) { char b[16]; formatValue (idx, p->norm[idx].load(), b, sizeof b); copyStr (ptr, b, 8); }
                return 0;
            case effCanBeAutomated: return (idx >= 0 && idx < kNumParams) ? 1 : 0;
            case effSetSampleRate:  p->setSampleRate (opt); return 0;
            case effSetBlockSize:   return 0;
            case effMainsChanged:   if (val) p->denoiser.reset(); return 0;
            case effStartProcess: case effStopProcess: return 0;
            case effEditGetRect:    if (ptr) *(ERect**) ptr = &p->rect; return 1;   // siempre valido
            case effEditOpen:       return p->openEditor (ptr) ? 1 : 0;
            case effEditClose:      p->closeEditor(); return 0;
            case effEditIdle:       p->idleEditor(); return 0;
            case effGetPlugCategory: return kPlugCategEffect;
            case effGetEffectName:    if (ptr) copyStr (ptr, "MilsDenoise", 32); return 1;
            case effGetProductString: if (ptr) copyStr (ptr, "MilsDenoise", 64); return 1;
            case effGetVendorString:  if (ptr) copyStr (ptr, "OpenDSP", 64);     return 1;
            case effGetVendorVersion: return 1000;
            case effGetVstVersion:    return 2400;
            case effGetTailSize:      return 0;
            default: return 0;  // incluye effCanDo, effSetProcessPrecision, chunks, etc.
        }
    }

    void setSampleRate (float sr)
    {
        if (sr < 8000.f || sr > 768000.f) return;
        ready.store (false, std::memory_order_release);
        denoiser.prepare ((double) sr, 2);
        fx.initialDelay = denoiser.getLatencySamples();
        lastGain = std::pow (10.f, toReal (pOutput, norm[pOutput].load()) / 20.f);
        ready.store (true, std::memory_order_release);
        if (host) host (&fx, audioMasterIOChanged, 0, 0, nullptr, 0.f);
    }

    void notifyHost (int i)
    { if (host) host (&fx, audioMasterAutomate, i, 0, nullptr, norm[i].load()); }

    // ------------------------------------------------------------ editor
#ifdef _WIN32
    HWND  hwnd = nullptr;
    HFONT fontText = nullptr, fontTitle = nullptr;
    int   dragIdx = -1;
    static constexpr int kTrackX0 = 140, kTrackX1 = 300;

    static HINSTANCE moduleHandle()
    {
        HMODULE h = nullptr;
        GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR) &LdPlugin::wndProc, &h);
        return (HINSTANCE) h;
    }

    bool openEditor (void* parent)
    {
        if (!parent || hwnd) return hwnd != nullptr;
        static const wchar_t* cls = L"MilsDenoiseEditorWnd";
        WNDCLASSEXW wc; std::memset (&wc, 0, sizeof (wc)); wc.cbSize = sizeof (wc);
        wc.style = CS_DBLCLKS; wc.lpfnWndProc = &LdPlugin::wndProc; wc.hInstance = moduleHandle();
        wc.hCursor = LoadCursorW (nullptr, (LPCWSTR) IDC_ARROW); wc.lpszClassName = cls;
        RegisterClassExW (&wc); // si ya existe, falla sin problema
        hwnd = CreateWindowExW (0, cls, L"", WS_CHILD | WS_VISIBLE, 0, 0, kW, kH, (HWND) parent, nullptr, wc.hInstance, this);
        return hwnd != nullptr;
    }
    void closeEditor() { if (hwnd) { DestroyWindow (hwnd); hwnd = nullptr; } }
    void idleEditor()  { if (hwnd && dirty.exchange (false)) InvalidateRect (hwnd, nullptr, FALSE); }

    void setFromX (int i, int x)
    {
        float v = (float) (x - kTrackX0) / (float) (kTrackX1 - kTrackX0);
        v = std::min (1.f, std::max (0.f, v));
        norm[i].store (v); notifyHost (i); InvalidateRect (hwnd, nullptr, FALSE);
    }
    static int rowAt (int y) { int r = (y - kTop) / kRowH; return (y >= kTop && r >= 0 && r < kNumSliders) ? r : -1; }
    // fila de casillas (debajo de los deslizadores): izquierda = Respiracion, derecha = Teclado
    static int checkAt (int x, int y)
    { const int y0 = kTop + kNumSliders * kRowH; if (y < y0 || y >= y0 + 44) return -1; return x < kW / 2 ? pBreath : pKeys; }
    void toggle (int i) { norm[i].store (norm[i].load() >= 0.5f ? 0.f : 1.f); notifyHost (i); InvalidateRect (hwnd, nullptr, FALSE); }

    void paint (HDC given = nullptr)
    {
        PAINTSTRUCT ps; HDC dc = given ? given : BeginPaint (hwnd, &ps);
        HDC m = CreateCompatibleDC (dc); HBITMAP bmp = CreateCompatibleBitmap (dc, kW, kH);
        HGDIOBJ oldBmp = SelectObject (m, bmp);
        auto brush = [&](COLORREF c) { return CreateSolidBrush (c); };
        RECT all = { 0, 0, kW, kH }; HBRUSH b = brush (RGB (27, 31, 36)); FillRect (m, &all, b); DeleteObject (b);
        SetBkMode (m, TRANSPARENT);

        SelectObject (m, fontTitle); SetTextColor (m, RGB (255, 255, 255));
        RECT t = { 14, 8, 300, 34 }; DrawTextW (m, L"MilsDenoise", -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject (m, fontText);  SetTextColor (m, RGB (140, 148, 158));
        RECT t2 = { 120, 8, kW - 14, 34 }; DrawTextW (m, L"supresor de ruido + gate", -1, &t2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        for (int i = 0; i < kNumSliders; ++i)
        {
            const int y = kTop + i * kRowH, cy = y + kRowH / 2;
            const float v = norm[i].load();
            SetTextColor (m, RGB (210, 214, 220));
            RECT lr = { 14, y, kTrackX0 - 8, y + kRowH }; DrawTextW (m, kParams[i].title, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT tr = { kTrackX0, cy - 3, kTrackX1, cy + 3 }; b = brush (RGB (58, 64, 72)); FillRect (m, &tr, b); DeleteObject (b);
            const int tx = kTrackX0 + (int) (v * (kTrackX1 - kTrackX0));
            RECT fr = { kTrackX0, cy - 3, tx, cy + 3 }; b = brush (RGB (79, 195, 247)); FillRect (m, &fr, b); DeleteObject (b);
            HBRUSH tb = brush (RGB (235, 240, 245)); HGDIOBJ ob = SelectObject (m, tb); HGDIOBJ op = SelectObject (m, GetStockObject (NULL_PEN));
            Ellipse (m, tx - 7, cy - 7, tx + 8, cy + 8); SelectObject (m, op); SelectObject (m, ob); DeleteObject (tb);

            char s[24]; formatValue (i, v, s, sizeof s);
            wchar_t w[40]; if (std::strcmp (s, "Off") == 0) _snwprintf (w, 40, L"Off");
            else _snwprintf (w, 40, L"%S %S", s, kParams[i].label);
            w[39] = 0;
            SetTextColor (m, RGB (255, 255, 255));
            RECT vr = { kTrackX1 + 12, y, kW - 10, y + kRowH }; DrawTextW (m, w, -1, &vr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        // casillas
        for (int i = pBreath; i <= pKeys; ++i)
        {
            const int x0 = (i == pBreath ? 14 : kW / 2 + 14), y0 = kTop + kNumSliders * kRowH + 14;
            const bool on = norm[i].load() >= 0.5f;
            RECT box = { x0, y0, x0 + 18, y0 + 18 };
            b = brush (on ? RGB (79, 195, 247) : RGB (58, 64, 72)); FillRect (m, &box, b); DeleteObject (b);
            if (on) { HPEN pen = CreatePen (PS_SOLID, 2, RGB (20, 24, 28)); HGDIOBJ op = SelectObject (m, pen);
                MoveToEx (m, x0 + 4, y0 + 9, nullptr); LineTo (m, x0 + 8, y0 + 13); LineTo (m, x0 + 14, y0 + 5);
                SelectObject (m, op); DeleteObject (pen); }
            SetTextColor (m, RGB (210, 214, 220));
            RECT lr = { x0 + 28, y0 - 2, x0 + 170, y0 + 22 }; DrawTextW (m, kParams[i].title, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        BitBlt (dc, 0, 0, kW, kH, m, 0, 0, SRCCOPY);
        SelectObject (m, oldBmp); DeleteObject (bmp); DeleteDC (m); if (!given) EndPaint (hwnd, &ps);
    }

    static LRESULT CALLBACK wndProc (HWND h, UINT msg, WPARAM w, LPARAM l)
    {
        if (msg == WM_NCCREATE)
        {
            auto* self = (LdPlugin*) ((CREATESTRUCTW*) l)->lpCreateParams;
            SetWindowLongPtrW (h, GWLP_USERDATA, (LONG_PTR) self);
            self->hwnd = h;
            return DefWindowProcW (h, msg, w, l);
        }
        auto* s = (LdPlugin*) GetWindowLongPtrW (h, GWLP_USERDATA);
        if (!s) return DefWindowProcW (h, msg, w, l);
        const int x = (short) LOWORD (l), y = (short) HIWORD (l);
        switch (msg)
        {
            case WM_CREATE:
                s->fontText  = CreateFontW (-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
                s->fontTitle = CreateFontW (-18, 0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
                return 0;
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: s->paint(); return 0;
            case WM_PRINTCLIENT: s->paint ((HDC) w); return 0;
            case WM_LBUTTONDOWN:
            { int r = rowAt (y); if (r >= 0) { s->dragIdx = r; SetCapture (h); s->setFromX (r, x); }
              else { int c = checkAt (x, y); if (c >= 0) s->toggle (c); }
              return 0; }
            case WM_MOUSEMOVE:
                if (s->dragIdx >= 0 && (w & MK_LBUTTON)) { s->setFromX (s->dragIdx, x); }
                return 0;
            case WM_LBUTTONUP:
                if (s->dragIdx >= 0) { s->dragIdx = -1; ReleaseCapture(); }
                return 0;
            case WM_LBUTTONDBLCLK: // doble clic = valor por defecto
            { int r = rowAt (y); if (r >= 0) { s->norm[r].store (kParams[r].def); s->notifyHost (r); InvalidateRect (h, nullptr, FALSE); }
              else { int c = checkAt (x, y); if (c >= 0) s->toggle (c); }   // 2o clic rapido = otro cambio
              return 0; }
            case WM_MOUSEWHEEL:
            {
                POINT pt = { x, y }; ScreenToClient (h, &pt); int r = rowAt (pt.y);
                if (r >= 0) { float d = (GET_WHEEL_DELTA_WPARAM (w) > 0 ? 0.02f : -0.02f);
                    s->norm[r].store (std::min (1.f, std::max (0.f, s->norm[r].load() + d))); s->notifyHost (r); InvalidateRect (h, nullptr, FALSE); }
                return 0;
            }
            case WM_DESTROY:
                if (s->fontText)  DeleteObject (s->fontText);
                if (s->fontTitle) DeleteObject (s->fontTitle);
                s->fontText = s->fontTitle = nullptr; s->hwnd = nullptr;
                SetWindowLongPtrW (h, GWLP_USERDATA, 0);
                return 0;
        }
        return DefWindowProcW (h, msg, w, l);
    }
#else
    bool openEditor (void*) { return false; }   // sin GUI fuera de Windows (solo para pruebas)
    void closeEditor() {}
    void idleEditor()  { dirty.store (false); }
#endif
};
} // namespace

#if defined(_WIN32)
 #define LD_EXPORT
#else
 #define LD_EXPORT __attribute__ ((visibility ("default")))
#endif

// Los simbolos se exportan con Vst2Plugin.def en Windows (VSTPluginMain y "main").
extern "C" LD_EXPORT AEffect* VSTPluginMain (HostCallback cb)
{
    auto* p = new LdPlugin (cb);
    return &p->fx;
}
