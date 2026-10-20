# MilsDenoise 1.2 — supresor de ruido + noise gate + respiración + teclado
**VST2 `.dll` de 64 bits (Equalizer APO) y VST3.** Motor propio: sin RNNoise, sin redes neuronales.

| | |
|---|---|
| CPU medida (48 kHz, mono o estéreo) | ~0.5 % de un núcleo con las casillas apagadas · ~0.5 % con «Teclado» · ~0.9 % con «Respiración» (el detector de tono duplica el coste) |
| RAM del motor | < 100 KB |
| Latencia | 512 muestras a 44.1/48 kHz (10.7 ms a 48k) · 1024 a 88.2/96k · 2048 a >100k |
| DLL VST2 | 64 bits, ~190 KB, sin dependencias externas (CRT estático) |

## Contenido
- `MilsDenoise_x64.dll` → **VST2 de 64 bits para Equalizer APO** (sirve también en cualquier host VST2 de 64 bits). Nombre interno: *MilsDenoise*.
- `MilsDenoise.vst3` → para DAWs (y forks de APO con soporte VST3). Se compila con CMake/GitHub Actions.

## Parámetros
| Nombre en APO | Mando | Rango | Valor real = |
|---|---|---|---|
| `Reduce` | Reducción | 0–40 dB | `40 × v` |
| `Sens` | Sensibilidad | 0–100 % | `100 × v` |
| `Smooth` | Suavizado | 0–100 % | `100 × v` |
| `Gate` | Gate (umbral) | −80…0 dB (**0 = Off**) | `−80 + 80 × v` |
| `Output` | Salida | −12…+12 dB | `−12 + 24 × v` |
| `Breath` | ☐ Respiración | **0 = Off (por defecto)**, 1 = On | `v ≥ 0.5` |
| `Keys` | ☐ Teclado | **0 = Off (por defecto)**, 1 = On | `v ≥ 0.5` |

`v` es el valor normalizado 0–1 que escribe Equalizer APO. **Las dos casillas están apagadas por defecto**: con ellas apagadas el plugin se comporta exactamente igual que la versión 1.1 (verificado: salida idéntica muestra a muestra).

### Casilla «Respiración»
Atenúa unos 20 dB los soplos y la respiración cerca del micrófono. Los reconoce por ser un soplo **sin tono** (sin armónicos de voz), dominado por graves, claramente por encima del ruido de fondo y que dura más de ~45 ms. Mientras haya voz sonora no actúa.

### Casilla «Teclado»
Atenúa hasta 24 dB (desde ~300 Hz, protegiendo los graves de la voz) el clic de las teclas durante ~25 ms. Los reconoce por una **subida brusca y selectiva en agudos** (>12 dB en ~10 ms) que no aparece igual en los graves; la voz sonora sube en ambos a la vez, y las fricativas (/s/, /f/) suben más despacio, así que se ignoran.

### Límites (importante)
- Es un método por reglas, **no una red neuronal**. Funciona muy bien con clics y soplos *aislados* o en pausas; si respiras **a la vez que hablas**, no se puede separar.
- El arranque de un soplo (≈ primeros 50–100 ms) sí pasa: el detector necesita ese tiempo para estar seguro de que es un soplo y no voz.
- Las consonantes explosivas muy secas (p, t, k) pueden suavizarse ligeramente (−1 dB en la prueba). Si lo notas, desactiva «Teclado».
- Probado con señales sintéticas; los micrófonos y teclados reales varían. Las constantes de detección están al inicio de `DenoiserCore.h` por si hay que ajustarlas.

## Gate
El detector mira la *entrada*; se abre al superar el umbral, se mantiene 80 ms, se cierra con histéresis de 3 dB y deja caer la señal 60 dB. Se aplica con 4 ms de anticipación. Empieza con el mando en Off, sube hasta que se corten las pausas y baja un poco.

## Uso en Equalizer APO (Windows, 64 bits)
1. Copia `MilsDenoise_x64.dll` a `C:\Program Files\EqualizerAPO\VSTPlugins\`.
2. Para un **micrófono**: *Configurator* de Equalizer APO → pestaña **Capture devices** → marca tu micrófono → reinicia Windows.
3. *Configuration Editor* → **Plugins → VST plugin** → elige la DLL → doble clic para abrir el panel. O edita `config.txt`:
```
Device: Microphone
VSTPlugin: Library MilsDenoise_x64.dll Reduce 0.45 Sens 0.5 Smooth 0.6 Gate 0.72 Output 0.5 Breath 1 Keys 1
```
(`Gate 0.72` ≈ −22 dB. Quita `Breath 1 Keys 1` si no las quieres.)

Notas:
- Solo el Equalizer APO **oficial** carga VST2. Los forks con VST3 pueden usar el `.vst3`.
- APO ejecuta su propia instancia dentro del motor de audio; el panel solo cambia los valores guardados (no se oye en el panel).
- La latencia (≈11 ms) se suma al micrófono.
- Audio en modo *exclusivo*/ASIO no pasa por APO (limitación de APO).

## Compilar
**Sin instalar nada:** sube la carpeta a GitHub → Actions → *Build* → descarga `MilsDenoise-Windows-x64` (DLL + VST3).

**En tu PC (Windows):** CMake 3.22+, Visual Studio 2022 "Desktop C++".
```
cmake -B build -A x64
cmake --build build --config Release
```
- DLL: `build\Release\MilsDenoise_x64.dll`
- VST3: `build\MilsDenoise_artefacts\Release\VST3\MilsDenoise.vst3` → `C:\Program Files\Common Files\VST3`
- Solo la DLL (rápido, sin descargar JUCE): `cmake -B build -A x64 -DBUILD_VST3=OFF`

**Cross-compilar la DLL desde Linux (mingw-w64):**
```
x86_64-w64-mingw32-g++ -O2 -std=c++17 -shared -static -static-libgcc -static-libstdc++ -s \
  -o MilsDenoise_x64.dll Source/Vst2Plugin.cpp Source/Vst2Plugin.def -luser32 -lgdi32
```

## Pruebas (`tests/`)
- `test_core.cpp`: motor + gate. `test_events.cpp`: respiración y teclado (señales sintéticas).
- `vst2_host_test.cpp`: emula la secuencia de llamadas de Equalizer APO contra la DLL/SO.
- `gui_test_win.cpp`: abre el editor, simula ratón y captura imagen (Windows/Wine).
```
g++ -O2 -std=c++17 tests/test_events.cpp -o t && ./t
```

## Aviso sobre el formato VST2 y licencias
- La interfaz VST2 está implementada desde cero en `Vst2Abi.h` (sin código ni cabeceras de Steinberg), solo para interoperar con hosts como Equalizer APO. Steinberg ya no concede licencias VST2 a desarrolladores nuevos; para **uso personal** es habitual, pero si piensas **distribuir** el `.dll`, consulta antes las condiciones (no soy abogado). El VST3 es la vía oficial.
- El VST3 usa JUCE (AGPLv3 o licencia comercial). La DLL VST2 no usa JUCE.
