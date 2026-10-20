cmake_minimum_required(VERSION 3.22)
project(MilsDenoise VERSION 1.2.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_VST3    "Compilar el VST3 (JUCE)"                          ON)
option(BUILD_APO_DLL "Compilar la DLL VST2 de 64 bits (Equalizer APO)" ${WIN32})

# ------------------------------------------------------------------ DLL VST2 (Equalizer APO)
# Sin JUCE, sin dependencias. CRT estatico: no requiere instalar el Visual C++ Redistributable
# (importante porque el plugin se carga dentro de audiodg.exe).
if(BUILD_APO_DLL)
    add_library(MilsDenoiseAPO MODULE Source/Vst2Plugin.cpp)
    set_target_properties(MilsDenoiseAPO PROPERTIES PREFIX "" OUTPUT_NAME "MilsDenoise_x64")
    target_link_libraries(MilsDenoiseAPO PRIVATE user32 gdi32)
    if(MSVC)
        set_property(TARGET MilsDenoiseAPO PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        target_link_options(MilsDenoiseAPO PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/Source/Vst2Plugin.def")
        target_compile_options(MilsDenoiseAPO PRIVATE /O2 /fp:fast /W3)
    else()
        target_link_options(MilsDenoiseAPO PRIVATE -static -static-libgcc -static-libstdc++ -s
                            "${CMAKE_CURRENT_SOURCE_DIR}/Source/Vst2Plugin.def")
        target_compile_options(MilsDenoiseAPO PRIVATE -O2)
    endif()
endif()

# ------------------------------------------------------------------ VST3 (JUCE)
if(BUILD_VST3)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(MilsDenoise
        COMPANY_NAME "OpenDSP"
        PLUGIN_MANUFACTURER_CODE Opdp
        PLUGIN_CODE Mdn1
        FORMATS VST3
        PRODUCT_NAME "MilsDenoise"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES Fx
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(MilsDenoise PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)

    target_compile_definitions(MilsDenoise PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    if(NOT MSVC)
        target_compile_options(MilsDenoise PRIVATE -Wno-sign-conversion)
    endif()

    target_link_libraries(MilsDenoise
        PRIVATE juce::juce_audio_utils
        PUBLIC  juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)
endif()
