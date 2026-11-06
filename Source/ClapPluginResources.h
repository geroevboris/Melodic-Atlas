#pragma once
#include <JuceHeader.h>

#if defined(ATLAS_ENABLE_NATIVE_CLAP) && ATLAS_ENABLE_NATIVE_CLAP
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#elif JUCE_MAC
#include <dlfcn.h>
#endif

namespace Atlas
{
    // Locate the plugin MODULE, not the DAW executable. Assets are stored
    // next to this module's bundle in Contents/Resources.
    inline juce::File nativeClapModelFile()
    {
        juce::File module;
       #if JUCE_WINDOWS
        static int moduleMarker = 0;
        HMODULE handle = nullptr;
        if (GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&moduleMarker), &handle))
        {
            wchar_t buffer[32768] {};
            if (GetModuleFileNameW(handle, buffer, 32768) > 0)
                module = juce::File(juce::String(buffer));
        }
       #elif JUCE_MAC
        static int moduleMarker = 0;
        Dl_info info {};
        if (dladdr(&moduleMarker, &info) != 0 && info.dli_fname != nullptr)
            module = juce::File(juce::String::fromUTF8(info.dli_fname));
       #endif

        if (!module.existsAsFile())
            return {};
        const auto executableDir = module.getParentDirectory();
        const auto resourceCandidate =
            executableDir.getParentDirectory().getChildFile("Resources")
                .getChildFile("clap_audio.onnx");
        if (resourceCandidate.existsAsFile())
            return resourceCandidate;
        // Standalone Windows executable resources live beside the EXE.
        return executableDir.getChildFile("clap_audio.onnx");
    }
}
#endif
