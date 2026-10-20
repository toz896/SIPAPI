// Prueba de la interfaz en Windows (o Wine): abre el editor como lo hace Equalizer APO, simula raton y captura BMP.
#include "../Source/Vst2Abi.h"
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <cstring>
static int automate = 0; static float lastAuto = -1; static int lastIdx = -1;
static VstIntPtr VSTCALL hostCb (AEffect*, VstInt32 op, VstInt32 i, VstIntPtr, void*, float v)
{ if (op == audioMasterVersion) return 2400; if (op == audioMasterAutomate) { ++automate; lastIdx = i; lastAuto = v; } return 0; }
static int fails = 0;
#define CHECK(c, m) do { bool ok_ = (c); printf ("  [%s] %s\n", ok_ ? "OK" : "FALLO", m); if (!ok_) ++fails; } while (0)

static void saveBmp (HWND child, int w, int h, const char* file)
{
    HDC scr = GetDC (nullptr); HDC mem = CreateCompatibleDC (scr);
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 24; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection (scr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject (mem, bmp);
    SendMessageW (child, WM_PRINTCLIENT, (WPARAM) mem, PRF_CLIENT);
    GdiFlush();
    BITMAPFILEHEADER fh = {}; int stride = ((w * 3 + 3) / 4) * 4; fh.bfType = 0x4D42; fh.bfOffBits = sizeof (fh) + sizeof (BITMAPINFOHEADER); fh.bfSize = fh.bfOffBits + stride * h;
    bi.bmiHeader.biHeight = -h;
    FILE* f = fopen (file, "wb"); fwrite (&fh, sizeof fh, 1, f); fwrite (&bi.bmiHeader, sizeof (BITMAPINFOHEADER), 1, f); fwrite (bits, 1, (size_t) stride * h, f); fclose (f);
    SelectObject (mem, old); DeleteObject (bmp); DeleteDC (mem); ReleaseDC (nullptr, scr);
}

int main (int argc, char** argv)
{
    HMODULE lib = LoadLibraryA (argc > 1 ? argv[1] : "MilsDenoise_x64.dll");
    auto mainFn = (AEffect* (*)(HostCallback)) GetProcAddress (lib, "VSTPluginMain");
    AEffect* e = mainFn (hostCb); e->user = (void*) 1;
    auto d = [&](int op, int idx = 0, intptr_t v = 0, void* p = nullptr, float o = 0) { return e->dispatcher (e, op, idx, v, p, o); };
    d (effOpen);
    WNDCLASSA wc = {}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA (nullptr); wc.lpszClassName = "hostwnd"; RegisterClassA (&wc);
    HWND host = CreateWindowA ("hostwnd", "host", WS_OVERLAPPEDWINDOW, 0, 0, 500, 400, nullptr, nullptr, wc.hInstance, nullptr);
    CHECK (host != nullptr, "ventana contenedora creada");

    ERect* r = nullptr; d (effEditGetRect, 0, 0, &r);                 // 1) como APO: antes de abrir
    CHECK (r != nullptr, "getRect antes de abrir devuelve puntero valido");
    VstIntPtr ok = d (effEditOpen, 0, 0, host);                        // 2) abrir
    CHECK (ok == 1, "effEditOpen devuelve 1");
    d (effEditGetRect, 0, 0, &r);                                      // 3) leer tamaño final
    int W = r->right - r->left, H = r->bottom - r->top; printf ("  tamaño del editor: %dx%d\n", W, H);
    HWND child = GetWindow (host, GW_CHILD);
    CHECK (child != nullptr && IsWindowVisible (host) == FALSE ? true : child != nullptr, "ventana hija del editor creada dentro del host");
    d (effEditIdle);
    saveBmp (child, W, H, "editor_inicial.bmp");

    // clic en la fila 0 (Reduccion), a mitad de pista (x = 140 + 80)
    SendMessageW (child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (220, 44 + 18));
    SendMessageW (child, WM_LBUTTONUP, 0, MAKELPARAM (220, 44 + 18));
    CHECK (fabsf (e->getParameter (e, 0) - 0.5f) < 0.02f, "clic a mitad de pista fija Reduccion ~ 0.5");
    CHECK (automate >= 1 && lastIdx == 0, "avisa al host con audioMasterAutomate (APO guarda el cambio)");
    // arrastre en la fila 3 (Gate): clic + mover a x=270
    SendMessageW (child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (140, 44 + 3 * 36 + 18));
    SendMessageW (child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM (140 + 115, 44 + 3 * 36 + 18));
    SendMessageW (child, WM_LBUTTONUP, 0, MAKELPARAM (255, 44 + 3 * 36 + 18));
    float g = e->getParameter (e, 3); printf ("  Gate tras arrastrar: %.3f\n", g);
    CHECK (g > 0.65f && g < 0.78f, "arrastrar mueve el umbral del gate");
    // arrastre fuera de la pista no sale de 0..1
    SendMessageW (child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (140, 44 + 36 + 18));
    SendMessageW (child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM (9999 & 0x7fff, 44 + 36 + 18));
    SendMessageW (child, WM_LBUTTONUP, 0, 0);
    CHECK (e->getParameter (e, 1) <= 1.0f && e->getParameter (e, 1) >= 0.99f, "arrastre mas alla del extremo se limita a 1.0");
    // doble clic = valor por defecto
    SendMessageW (child, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM (200, 44 + 36 + 18));
    CHECK (fabsf (e->getParameter (e, 1) - 0.5f) < 1e-4f, "doble clic restaura el valor por defecto");
    // casillas: Respiracion (izquierda) y Teclado (derecha), en la fila bajo los deslizadores
    const int yc = 44 + 5 * 36 + 22;
    SendMessageW (child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (30, yc)); SendMessageW (child, WM_LBUTTONUP, 0, MAKELPARAM (30, yc));
    CHECK (e->getParameter (e, 5) == 1.f, "clic en la casilla Respiracion la activa");
    SendMessageW (child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (230, yc)); SendMessageW (child, WM_LBUTTONUP, 0, MAKELPARAM (230, yc));
    CHECK (e->getParameter (e, 6) == 1.f, "clic en la casilla Teclado la activa");
    SendMessageW (child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (30, yc)); SendMessageW (child, WM_LBUTTONUP, 0, MAKELPARAM (30, yc));
    CHECK (e->getParameter (e, 5) == 0.f, "otro clic la desactiva");
    SendMessageW (child, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM (30, yc));
    CHECK (e->getParameter (e, 5) == 1.f, "un doble clic rapido tambien cambia la casilla (no se pierde)");
    CHECK (lastIdx == 5, "avisa al host (audioMasterAutomate) al cambiar una casilla");
    char dsp[16] = {0}; d (effGetParamDisplay, 6, 0, dsp); CHECK (strcmp (dsp, "On") == 0, "Teclado muestra 'On' en el host");
    CHECK (e->getParameter (e, 0) > 0.4f && e->getParameter (e, 0) < 0.6f, "los deslizadores no se alteran al pulsar casillas");
    // cambio desde el host mientras el editor esta abierto
    e->setParameter (e, 4, 0.75f); d (effEditIdle);
    saveBmp (child, W, H, "editor_modificado.bmp");
    char disp[16] = {0}; d (effGetParamDisplay, 3, 0, disp); printf ("  Gate muestra: '%s'\n", disp);

    d (effEditClose);
    CHECK (GetWindow (host, GW_CHILD) == nullptr, "effEditClose destruye la ventana hija");
    // reabrir (APO abre/cierra el panel varias veces)
    ok = d (effEditOpen, 0, 0, host); CHECK (ok == 1, "se puede reabrir el editor");
    d (effEditClose); d (effEditClose); CHECK (true, "cerrar dos veces no falla");
    d (effClose); DestroyWindow (host);
    printf ("\n%s (%d fallos)\n", fails ? "RESULTADO: HAY FALLOS" : "RESULTADO: TODO OK", fails);
    return fails ? 1 : 0;
}
