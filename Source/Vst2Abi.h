// ============================================================================
//  Vst2Abi.h - Definiciones MINIMAS de la interfaz binaria de plugins "VST 2.x"
//  escritas desde cero (solo lo necesario para interoperar con hosts como
//  Equalizer APO). No contiene codigo ni cabeceras del SDK de Steinberg.
// ============================================================================
#pragma once
#include <stdint.h>

#if defined(_WIN32) && !defined(_WIN64)
 #define VSTCALL __cdecl
#else
 #define VSTCALL
#endif

typedef int32_t  VstInt32;
typedef intptr_t VstIntPtr;

struct AEffect;
typedef VstIntPtr (VSTCALL *HostCallback) (AEffect*, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);

struct AEffect
{
    VstInt32  magic;                                                                    // 'VstP'
    VstIntPtr (VSTCALL *dispatcher)   (AEffect*, VstInt32, VstInt32, VstIntPtr, void*, float);
    void      (VSTCALL *process)      (AEffect*, float** in, float** out, VstInt32 n);  // obsoleto
    void      (VSTCALL *setParameter) (AEffect*, VstInt32 index, float value);
    float     (VSTCALL *getParameter) (AEffect*, VstInt32 index);
    VstInt32  numPrograms, numParams, numInputs, numOutputs, flags;
    VstIntPtr resvd1, resvd2;
    VstInt32  initialDelay, realQualities, offQualities;
    float     ioRatio;
    void*     object;        // libre para el plugin
    void*     user;          // reservado para el HOST (Equalizer APO lo sobrescribe)
    VstInt32  uniqueID, version;
    void      (VSTCALL *processReplacing)       (AEffect*, float** in, float** out, VstInt32 n);
    void      (VSTCALL *processDoubleReplacing) (AEffect*, double** in, double** out, VstInt32 n);
    char      future[56];
};

struct ERect { int16_t top, left, bottom, right; };

enum : VstInt32 { kEffectMagic = 0x56737450 /* 'VstP' */ };

enum : VstInt32   // flags
{
    effFlagsHasEditor   = 1 << 0,
    effFlagsCanReplacing = 1 << 4,
};

enum : VstInt32   // opcodes plugin <- host
{
    effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effSetProgramName = 4, effGetProgramName = 5,
    effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
    effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12,
    effEditGetRect = 13, effEditOpen = 14, effEditClose = 15, effEditIdle = 19,
    effGetChunk = 23, effSetChunk = 24, effProcessEvents = 25, effCanBeAutomated = 26,
    effGetPlugCategory = 35, effGetEffectName = 45, effGetVendorString = 47, effGetProductString = 48,
    effGetVendorVersion = 49, effCanDo = 51, effGetTailSize = 52, effIdle = 53, effGetVstVersion = 58,
    effStartProcess = 71, effStopProcess = 72, effSetProcessPrecision = 77
};

enum : VstInt32   // opcodes host <- plugin
{
    audioMasterAutomate = 0, audioMasterVersion = 1, audioMasterIOChanged = 13, audioMasterSizeWindow = 15,
    audioMasterUpdateDisplay = 42
};

enum : VstInt32 { kPlugCategEffect = 1 };

static_assert (sizeof (void*) != 8 || sizeof (AEffect) == 192, "Layout de AEffect inesperado en 64 bits");
