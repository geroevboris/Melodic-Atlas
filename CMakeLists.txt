cmake_minimum_required(VERSION 3.22)

# Собираем Universal Binary (Apple Silicon arm64 + Intel x86_64 для Rosetta)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal architectures" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version" FORCE)
endif()

project(MelodicAtlas VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# 1. Подключаем JUCE 8 (совместим с macOS 15 SDK и Xcode 16)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# 2. Подключаем Signalsmith Stretch
FetchContent_Declare(
    signalsmith_stretch
    GIT_REPOSITORY https://github.com/juce-framework/signalsmith-stretch.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
# Fallback на официальный репозиторий Signalsmith
FetchContent_Declare(
    signalsmith_stretch_official
    GIT_REPOSITORY https://github.com/signalsmith-audio/signalsmith-stretch.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(signalsmith_stretch_official)

juce_add_plugin(MelodicAtlas
    COMPANY_NAME "BorisAudio"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE BrAu
    PLUGIN_CODE MlAt
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Melodic-Atlas"
)

juce_generate_juce_header(MelodicAtlas)

target_sources(MelodicAtlas
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(MelodicAtlas
    PRIVATE
        Source
        ${signalsmith_stretch_official_SOURCE_DIR}
)

target_compile_definitions(MelodicAtlas
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(MelodicAtlas
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_opengl
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)


# Stage 8B: native offline CLAP. Release builds enable this explicitly and
# provide a pinned ONNX model + ONNX Runtime as BUILD INPUTS. Users never
# download dependencies at scan time.
option(ATLAS_ENABLE_NATIVE_CLAP "Embed local CLAP inference into plugin bundles" OFF)
set(ATLAS_ONNXRUNTIME_ROOT "" CACHE PATH "Extracted ONNX Runtime C++ distribution")
set(ATLAS_CLAP_MODEL_PATH "" CACHE FILEPATH "Pinned, SHA-256-verified audio_model.onnx")

if(ATLAS_ENABLE_NATIVE_CLAP)
    if(NOT EXISTS "${ATLAS_CLAP_MODEL_PATH}")
        message(FATAL_ERROR "CLAP model missing. Build-time fetch must provide ATLAS_CLAP_MODEL_PATH.")
    endif()
    if(NOT EXISTS "${ATLAS_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
        message(FATAL_ERROR "ONNX Runtime distribution missing. Set ATLAS_ONNXRUNTIME_ROOT.")
    endif()

    FetchContent_Declare(
        umappp
        GIT_REPOSITORY https://github.com/libscran/umappp.git
        GIT_TAG v3.3.2
        GIT_SHALLOW TRUE
    )
    set(UMAPPP_TESTS OFF CACHE BOOL "No dependency tests in plugin build" FORCE)
    FetchContent_MakeAvailable(umappp)

    target_compile_definitions(MelodicAtlas PRIVATE ATLAS_ENABLE_NATIVE_CLAP=1)
    target_include_directories(MelodicAtlas PRIVATE "${ATLAS_ONNXRUNTIME_ROOT}/include")
    target_link_libraries(MelodicAtlas PRIVATE libscran::umappp)

    if(APPLE)
        set(ATLAS_ORT_LINK "${ATLAS_ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib")
        set(ATLAS_ORT_VERSIONED "${ATLAS_ONNXRUNTIME_ROOT}/lib/libonnxruntime.1.19.2.dylib")
        if(NOT EXISTS "${ATLAS_ORT_LINK}")
            message(FATAL_ERROR "macOS ONNX Runtime dylib not found")
        endif()
    elseif(WIN32)
        set(ATLAS_ORT_LINK "${ATLAS_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
        set(ATLAS_ORT_DLL "${ATLAS_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
        if(NOT EXISTS "${ATLAS_ORT_LINK}" OR NOT EXISTS "${ATLAS_ORT_DLL}")
            message(FATAL_ERROR "Windows ONNX Runtime import library or DLL not found")
        endif()
    else()
        message(FATAL_ERROR "Native CLAP currently supports macOS and Windows only.")
    endif()
    target_link_libraries(MelodicAtlas PRIVATE "${ATLAS_ORT_LINK}")

    foreach(atlas_format IN ITEMS VST3 AU Standalone)
        set(atlas_wrapper "MelodicAtlas_${atlas_format}")
        if(TARGET "${atlas_wrapper}")
            if(APPLE)
                set(atlas_bundle "$<TARGET_BUNDLE_DIR:${atlas_wrapper}>")
                set(atlas_resources "${atlas_bundle}/Contents/Resources")
                set(atlas_frameworks "${atlas_bundle}/Contents/Frameworks")
                set_target_properties("${atlas_wrapper}" PROPERTIES
                    BUILD_RPATH "@loader_path/../Frameworks"
                    INSTALL_RPATH "@loader_path/../Frameworks")
                add_custom_command(TARGET "${atlas_wrapper}" POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${atlas_resources}"
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${atlas_frameworks}"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${ATLAS_CLAP_MODEL_PATH}" "${atlas_resources}/clap_audio.onnx"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${ATLAS_ORT_LINK}" "${atlas_frameworks}/libonnxruntime.dylib"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${ATLAS_ORT_LINK}" "${atlas_frameworks}/libonnxruntime.1.19.2.dylib"
                    # The bundle's code signature must be applied AFTER
                    # model weights and ONNX Runtime have been installed.
                    # Otherwise macOS AU hosts can reject the modified bundle.
                    COMMAND /usr/bin/codesign --force --deep --sign -
                        --timestamp=none "${atlas_bundle}"
                    VERBATIM)
            else()
                if(atlas_format STREQUAL "VST3")
                    set(atlas_resources
                        "$<TARGET_FILE_DIR:${atlas_wrapper}>/../Resources")
                else()
                    set(atlas_resources "$<TARGET_FILE_DIR:${atlas_wrapper}>")
                endif()
                add_custom_command(TARGET "${atlas_wrapper}" POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${atlas_resources}"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${ATLAS_CLAP_MODEL_PATH}" "${atlas_resources}/clap_audio.onnx"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${ATLAS_ORT_DLL}" "$<TARGET_FILE_DIR:${atlas_wrapper}>/onnxruntime.dll"
                    VERBATIM)
            endif()
        endif()
    endforeach()
endif()
