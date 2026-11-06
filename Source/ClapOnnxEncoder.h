#pragma once
#include "ClapAudioFeatures.h"
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#if defined(ATLAS_ENABLE_NATIVE_CLAP) && ATLAS_ENABLE_NATIVE_CLAP
#include <onnxruntime_cxx_api.h>

namespace Atlas
{
    // Owned only by the scanner thread. Destruction releases weights and the
    // ONNX session; never instantiate this from an audio callback.
    class ClapOnnxEncoder
    {
    public:
        static constexpr int embeddingSize = 512;
        explicit ClapOnnxEncoder(const juce::File& weights)
            : env(ORT_LOGGING_LEVEL_WARNING, "MelodicAtlasCLAP")
        {
            if (!weights.existsAsFile())
                throw std::runtime_error("Bundled CLAP model is missing");

            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(2);
            options.SetInterOpNumThreads(1);
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

           #if JUCE_WINDOWS
            session = std::make_unique<Ort::Session>(
                env, weights.getFullPathName().toWideCharPointer(), options);
           #else
            session = std::make_unique<Ort::Session>(
                env, weights.getFullPathName().toRawUTF8(), options);
           #endif
        }

        bool embed(const juce::File& audio,
                   juce::AudioFormatManager& formats,
                   std::array<float, embeddingSize>& vector)
        {
            std::vector<float> features;
            if (!ClapAudioFeatures::fromFile(audio, formats, features)
                || features.size() != (size_t)ClapAudioFeatures::frames
                                      * ClapAudioFeatures::bands)
                return false;

            const std::array<int64_t, 4> featureShape {
                1, 1, ClapAudioFeatures::frames, ClapAudioFeatures::bands
            };
            const std::array<int64_t, 2> longerShape { 1, 1 };
            bool isLonger = false; // non-fusion 10-second input crop.
            auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            std::array<Ort::Value, 2> inputs {
                Ort::Value::CreateTensor<float>(
                    memory, features.data(), features.size(),
                    featureShape.data(), featureShape.size()),
                Ort::Value::CreateTensor<bool>(
                    memory, &isLonger, 1,
                    longerShape.data(), longerShape.size())
            };
            const char* inNames[] = { "input_features", "is_longer" };
            const char* outNames[] = { "embeddings" };

            try
            {
                auto results = session->Run(
                    Ort::RunOptions{nullptr},
                    inNames, inputs.data(), inputs.size(), outNames, 1);
                if (results.size() != 1 || !results[0].IsTensor() ||
                    results[0].GetTensorTypeAndShapeInfo().GetElementCount()
                        != embeddingSize)
                    return false;
                auto* ptr = results[0].GetTensorData<float>();
                double sumSq = 0.0;
                for (int i = 0; i < embeddingSize; ++i)
                {
                    if (!std::isfinite(ptr[i]))
                        return false;
                    sumSq += (double)ptr[i] * ptr[i];
                }
                if (sumSq < 1e-12)
                    return false;
                const auto invLength = (float)(1.0 / std::sqrt(sumSq));
                for (int i = 0; i < embeddingSize; ++i)
                    vector[(size_t)i] = ptr[i] * invLength;
                return true;
            }
            catch (const Ort::Exception& err)
            {
                juce::Logger::writeToLog("CLAP inference failed for " +
                    audio.getFileName() + ": " + juce::String(err.what()));
                return false;
            }
        }

    private:
        Ort::Env env;
        std::unique_ptr<Ort::Session> session;
    };
}
#endif
