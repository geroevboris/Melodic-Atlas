#pragma once
#include "ClapAudioFeatures.h"
#include <array>
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>
#include <utility>

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
            // Conservative on Apple Silicon: avoid oversubscribing the DAW.
            options.SetIntraOpNumThreads(4);
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

        // Prepare in groups of at most 16. True tensor batching is only
        // attempted when the exported ONNX model declares a dynamic batch
        // dimension. Fixed-batch models use the exact single-item path.
        // An unsupported batch is never mistaken for a classification error.
        void embedBatch(const std::vector<juce::File>& files,
                        juce::AudioFormatManager& formats,
                        std::vector<std::array<float, embeddingSize>>& vectors,
                        std::vector<bool>& valid)
        {
            const size_t count = files.size();
            vectors.resize(count);
            valid.assign(count, false);
            if (count == 0)
                return;

            std::vector<std::vector<float>> prepared(count);
            for (size_t i = 0; i < count; ++i)
                ClapAudioFeatures::fromFile(files[i], formats, prepared[i]);

            constexpr size_t featureCount =
                (size_t)ClapAudioFeatures::frames * ClapAudioFeatures::bands;
            bool allPrepared = std::all_of(prepared.begin(), prepared.end(),
                [](const auto& f) { return f.size() == featureCount; });

            if (count > 1 && allPrepared && modelSupportsDynamicBatch())
            {
                try
                {
                    std::vector<float> stacked;
                    stacked.reserve(count * featureCount);
                    for (const auto& f : prepared)
                        stacked.insert(stacked.end(), f.begin(), f.end());

                    const std::array<int64_t, 4> shape {
                        (int64_t)count, 1,
                        ClapAudioFeatures::frames, ClapAudioFeatures::bands
                    };
                    const std::array<int64_t, 2> longerShape {
                        (int64_t)count, 1
                    };
                    std::unique_ptr<bool[]> longer(new bool[count] {});
                    auto memory = Ort::MemoryInfo::CreateCpu(
                        OrtArenaAllocator, OrtMemTypeDefault);
                    std::array<Ort::Value, 2> inputs {
                        Ort::Value::CreateTensor<float>(
                            memory, stacked.data(), stacked.size(),
                            shape.data(), shape.size()),
                        Ort::Value::CreateTensor<bool>(
                            memory, longer.get(), count,
                            longerShape.data(), longerShape.size())
                    };
                    const char* names[] = { "input_features", "is_longer" };
                    const char* outputs[] = { "embeddings" };
                    auto results = session->Run(Ort::RunOptions{nullptr},
                        names, inputs.data(), inputs.size(), outputs, 1);
                    if (results.size() == 1 && results[0].IsTensor()
                        && results[0].GetTensorTypeAndShapeInfo().GetElementCount()
                           == count * embeddingSize)
                    {
                        const float* data = results[0].GetTensorData<float>();
                        for (size_t i = 0; i < count; ++i)
                            valid[i] = normalizeEmbedding(
                                data + i * embeddingSize, vectors[i]);
                        return;
                    }
                }
                catch (const Ort::Exception& error)
                {
                    juce::Logger::writeToLog(
                        "CLAP batch unavailable, using singles: "
                        + juce::String(error.what()));
                }
            }

            for (size_t i = 0; i < count; ++i)
                if (prepared[i].size() == featureCount)
                    valid[i] = embedPrepared(prepared[i], vectors[i]);
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

            return embedPrepared(features, vector);
        }

    private:
        bool modelSupportsDynamicBatch() const
        {
            try
            {
                auto shape = session->GetInputTypeInfo(0)
                    .GetTensorTypeAndShapeInfo().GetShape();
                return shape.size() == 4 && shape[0] < 0;
            }
            catch (const Ort::Exception&)
            {
                return false;
            }
        }

        static bool normalizeEmbedding(
            const float* ptr, std::array<float, embeddingSize>& vector)
        {
            double sumSq = 0.0;
            for (int i = 0; i < embeddingSize; ++i)
            {
                if (!std::isfinite(ptr[i]))
                    return false;
                sumSq += (double)ptr[i] * ptr[i];
            }
            if (sumSq < 1e-12)
                return false;
            const float inv = (float)(1.0 / std::sqrt(sumSq));
            for (int i = 0; i < embeddingSize; ++i)
                vector[(size_t)i] = ptr[i] * inv;
            return true;
        }

        bool embedPrepared(const std::vector<float>& features,
                           std::array<float, embeddingSize>& vector)
        {
            const std::array<int64_t, 4> featureShape {
                1, 1, ClapAudioFeatures::frames, ClapAudioFeatures::bands
            };
            const std::array<int64_t, 2> longerShape { 1, 1 };
            bool isLonger = false;
            auto memory = Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator, OrtMemTypeDefault);
            std::array<Ort::Value, 2> inputs {
                Ort::Value::CreateTensor<float>(
                    memory, const_cast<float*>(features.data()), features.size(),
                    featureShape.data(), featureShape.size()),
                Ort::Value::CreateTensor<bool>(
                    memory, &isLonger, 1,
                    longerShape.data(), longerShape.size())
            };
            const char* inNames[] = { "input_features", "is_longer" };
            const char* outNames[] = { "embeddings" };
            try
            {
                auto results = session->Run(Ort::RunOptions{nullptr},
                    inNames, inputs.data(), inputs.size(), outNames, 1);
                if (results.size() != 1 || !results[0].IsTensor()
                    || results[0].GetTensorTypeAndShapeInfo().GetElementCount()
                       != embeddingSize)
                    return false;
                return normalizeEmbedding(
                    results[0].GetTensorData<float>(), vector);
            }
            catch (const Ort::Exception& error)
            {
                juce::Logger::writeToLog(
                    "CLAP inference failed: " + juce::String(error.what()));
                return false;
            }
        }

        Ort::Env env;
        std::unique_ptr<Ort::Session> session;
    };
}
#endif
