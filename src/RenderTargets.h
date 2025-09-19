//----------------------------------------------------------------------------------
// File:        sl_demo.cpp
// SDK Version: 2.0
// Email:       StreamlineSupport@nvidia.com
// Site:        http://developer.nvidia.com/
//
// Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//----------------------------------------------------------------------------------

#pragma once

#include <donut/core/math/math.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/render/GBuffer.h>
#include <nvrhi/nvrhi.h>
#include <nvrhi/common/misc.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/SceneTypes.h>
#include <donut/core/log.h>


/// <summary>
/// This class stores all the color buffers we will use in our render pipeline.
/// </summary>
class RenderTargets : public donut::render::GBufferRenderTargets
{
public:
    nvrhi::TextureHandle HdrColor;
    nvrhi::TextureHandle LdrColor;
    nvrhi::TextureHandle ColorspaceCorrectionColor;
    nvrhi::TextureHandle AAResolvedColor;
    nvrhi::TextureHandle TemporalFeedback1;
    nvrhi::TextureHandle TemporalFeedback2;
    nvrhi::TextureHandle AmbientOcclusion;
    nvrhi::TextureHandle NisColor;
    nvrhi::TextureHandle PreUIColor;
    nvrhi::TextureHandle SpecHitDistance;
    nvrhi::TextureHandle GBufferDiffuseRR;
    nvrhi::TextureHandle GBufferSpecularRR;
    nvrhi::TextureHandle GBufferNormalsRR;
    nvrhi::TextureHandle GBufferEmissiveRR;

#pragma region HACK
    // general options are in base class GBufferRenderTargets
    // data storage: loaded textures
    std::vector<std::shared_ptr<donut::engine::TextureData>> hackLoadedColorsLDR;
    std::vector<std::shared_ptr<donut::engine::TextureData>> hackLoadedColorsHDR;
    std::vector<std::shared_ptr<donut::engine::TextureData>> hackLoadedMVs;
    std::vector<std::shared_ptr<donut::engine::TextureData>> hackLoadedDepths;
    std::vector<donut::math::float2> hackLoadedJitterOffsets;
    // used for render targets; MV and Depth are in GBufferRenderTargets
    nvrhi::TextureHandle hackPreUIColor;
    nvrhi::TextureHandle hackHdrColor;
    
#pragma endregion

    nvrhi::HeapHandle Heap;

    std::shared_ptr<donut::engine::FramebufferFactory> ForwardFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> HdrFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> LdrFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> AAResolvedFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> PreUIFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> SpecHitDistanceBuffer;

    donut::math::int2 m_RenderSize;// size of render targets pre-DLSS
    donut::math::int2 m_DisplaySize; // size of render targets post-DLSS

    void Init(
        nvrhi::IDevice* device,
        donut::math::int2 renderSize,
        donut::math::int2 displaySize,
        nvrhi::Format backbufferFormat,
        dm::uint sampleCount = 1,
        bool enableMotionVectors = true,
        bool useReverseProjection = true)
    {
        GBufferRenderTargets::Init(device, (donut::math::uint2) renderSize, sampleCount, enableMotionVectors, useReverseProjection);


        m_RenderSize = renderSize;
        m_DisplaySize = displaySize;

        nvrhi::TextureDesc desc;
        desc.width = renderSize.x;
        desc.height = renderSize.y;
        desc.isRenderTarget = true;
        desc.useClearValue = true;
        desc.clearValue = nvrhi::Color(1.f);
        desc.sampleCount = sampleCount;
        desc.dimension = sampleCount > 1 ? nvrhi::TextureDimension::Texture2DMS : nvrhi::TextureDimension::Texture2D;
        desc.keepInitialState = true;
        desc.isVirtual = device->queryFeatureSupport(nvrhi::Feature::VirtualResources);

        desc.clearValue = nvrhi::Color(0.f);
        desc.isTypeless = false;
        desc.isUAV = sampleCount == 1;
        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        desc.debugName = "HdrColor";
        HdrColor = device->createTexture(desc);

        desc.format = nvrhi::Format::R16_FLOAT;
        desc.debugName = "SpecHitDistance";
        desc.isUAV = true;
        desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        SpecHitDistance = device->createTexture(desc);

        // The render targets below this point are non-MSAA
        desc.sampleCount = 1;
        desc.dimension = nvrhi::TextureDimension::Texture2D;
        desc.format = nvrhi::Format::R8_UNORM;
        desc.isUAV = true;
        desc.initialState = nvrhi::ResourceStates::RenderTarget;
        desc.debugName = "AmbientOcclusion";
        AmbientOcclusion = device->createTexture(desc);

        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.width = displaySize.x;
        desc.height = displaySize.y;
        desc.isUAV = true;
        desc.debugName = "AAResolvedColor";
        AAResolvedColor = device->createTexture(desc);

        desc.format = nvrhi::Format::RGBA16_SNORM;
        desc.isUAV = true;
        desc.debugName = "TemporalFeedback1";
        TemporalFeedback1 = device->createTexture(desc);
        desc.debugName = "TemporalFeedback2";
        TemporalFeedback2 = device->createTexture(desc);

        desc.format = nvrhi::Format::SRGBA8_UNORM;
        desc.isUAV = false;
        desc.debugName = "LdrColor";
        LdrColor = device->createTexture(desc);

        // Redefined these RR G-Buffer textures locally to use custom formats, 
        // as the ones provided by Donut are not compatible with our requirements.
        // Keeping them separate gives us more control over the format and usage.
        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.debugName = "GBufferDiffuseRR";
        GBufferDiffuseRR = device->createTexture(desc);

        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.debugName = "GBufferSpecularRR";
        GBufferSpecularRR = device->createTexture(desc);

        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.debugName = "GBufferNormalsRR";
        GBufferNormalsRR = device->createTexture(desc);

        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.debugName = "GBufferEmissiveRR";
        GBufferEmissiveRR = device->createTexture(desc);

        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.isUAV = true;
        desc.debugName = "ColorspaceCorrectionColor";
        ColorspaceCorrectionColor = device->createTexture(desc);

        desc.format = backbufferFormat;
        desc.isUAV = true;
        desc.debugName = "NisColor";
        NisColor = device->createTexture(desc);

        desc.format = backbufferFormat;
        desc.isUAV = true;
        desc.debugName = "PreUIColor";
        PreUIColor = device->createTexture(desc);

#pragma region HACK
        desc = PreUIColor->getDesc();
        desc.debugName = "hackPreUIColor";
        hackPreUIColor = device->createTexture(desc);

        desc = HdrColor->getDesc();
        desc.debugName = "hackHdrColor";
        hackHdrColor = device->createTexture(desc);
#pragma endregion

        if (desc.isVirtual)
        {
            uint64_t heapSize = 0;
            nvrhi::ITexture* const textures[] = {
                HdrColor,
                AAResolvedColor,
                SpecHitDistance,
                TemporalFeedback1,
                TemporalFeedback2,
                LdrColor,
                GBufferDiffuseRR,
                GBufferSpecularRR,
                GBufferNormalsRR,
                GBufferEmissiveRR,
                ColorspaceCorrectionColor,
                PreUIColor,
                NisColor,
                AmbientOcclusion,
                GBufferSpecularRR,
                GBufferDiffuseRR,
                // hack render targets
                hackPreUIColor,
                hackHdrColor,
            };

            for (auto texture : textures)
            {
                nvrhi::MemoryRequirements memReq = device->getTextureMemoryRequirements(texture);
                heapSize = nvrhi::align(heapSize, memReq.alignment);
                heapSize += memReq.size;
            }

            nvrhi::HeapDesc heapDesc;
            heapDesc.type = nvrhi::HeapType::DeviceLocal;
            heapDesc.capacity = heapSize;
            heapDesc.debugName = "RenderTargetHeap";

            Heap = device->createHeap(heapDesc);

            uint64_t offset = 0;
            for (auto texture : textures)
            {
                nvrhi::MemoryRequirements memReq = device->getTextureMemoryRequirements(texture);
                offset = nvrhi::align(offset, memReq.alignment);

                device->bindTextureMemory(texture, Heap, offset);

                offset += memReq.size;
            }
        }

        ForwardFramebuffer = std::make_shared<donut::engine::FramebufferFactory>(device);
        ForwardFramebuffer->RenderTargets = { HdrColor };
        ForwardFramebuffer->DepthTarget = Depth;

        HdrFramebuffer = std::make_shared<donut::engine::FramebufferFactory>(device);
        HdrFramebuffer->RenderTargets = { HdrColor };

        SpecHitDistanceBuffer = std::make_shared<donut::engine::FramebufferFactory>(device);
        SpecHitDistanceBuffer->RenderTargets = { SpecHitDistance };

        LdrFramebuffer = std::make_shared<donut::engine::FramebufferFactory>(device);
        LdrFramebuffer->RenderTargets = { LdrColor };

        AAResolvedFramebuffer = std::make_shared<donut::engine::FramebufferFactory>(device);
        AAResolvedFramebuffer->RenderTargets = { AAResolvedColor };

        PreUIFramebuffer = std::make_shared<donut::engine::FramebufferFactory>(device);
        PreUIFramebuffer->RenderTargets = { PreUIColor };
    }

    bool IsUpdateRequired(donut::math::int2 renderSize, donut::math::int2 displaySize, donut::math::uint sampleCount = 1) const
    {
        if (any(m_RenderSize != renderSize) || any(m_DisplaySize != displaySize) || m_SampleCount != sampleCount) 
            return true;
        
        return false;
    }

    void Clear(nvrhi::ICommandList* commandList) override
    {
        GBufferRenderTargets::Clear(commandList);

        commandList->clearTextureFloat(HdrColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(LdrColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(NisColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(PreUIColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(AAResolvedColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(SpecHitDistance, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(GBufferDiffuseRR, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(GBufferSpecularRR, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(GBufferNormalsRR, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(GBufferEmissiveRR, nvrhi::AllSubresources, nvrhi::Color(0.f));

        commandList->clearTextureFloat(hackPreUIColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(hackHdrColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
    }

    /**
     * Will be called in App scope, create a permanent HackOptionDef object stored in App.
     */
    static HackOptionDef parseHackOptions(int argc, const char* const* argv) {
		HackOptionDef options;

        std::vector<std::string> argList(argv + 1, argv + argc);  // Skip argv[0]

		// counter to sanity check hackPaths, result written to options.frameCount 
        auto count_exr_files = [](const std::filesystem::path& folderPath) {
            return std::count_if(std::filesystem::directory_iterator(folderPath), std::filesystem::directory_iterator{}, [](const auto& entry) {
                return entry.path().extension() == ".exr";
                });
            };

        // parse other options only when global switch "-EnableHack" is set
        bool         hackMode = false;
        for (size_t currentArg = 0; currentArg < argList.size(); currentArg++)
        {
            std::string command = argList[currentArg];
            if (command == "-EnableHack")
            {
                // we reset HackOptions otherwise bool fields can't be overwritten to false
                hackMode = true;
                options.enableHack = true;
                continue;
            }
            if (hackMode && command == "-Identifier")
            {
                // We require at least 1 argument
                assert(currentArg + 1 < argList.size() && argList[currentArg + 1][0] != '-',
                    "-Identifier requires a input to be provided (usage: -Identifier <input>");
                options.identifier = argList[currentArg + 1];
                currentArg++;
                continue;
            }
            if (hackMode && command == "-RenderResolution")
            {
                // We require at least 1 argument
                assert(currentArg + 1 < argList.size() && argList[currentArg + 1][0] != L'-',
                    "-RenderResolution requires a input to be provided (usage: -RenderResolution <1 or 2 or 4>");
                int resOption = std::stoi(argList[currentArg + 1]);
                assert(resOption == 1 || resOption == 2 || resOption == 4,
                    L"usage: -RenderResolution <1 or 2 or 4>, got %d", resOption);
                options.renderResolution = static_cast<HackOptionDef::HackRenderResolution>(resOption);

                currentArg++;
                continue;
            }
            if (hackMode && command == "-ParseJitter")
            {
                options.parseJitter = true;
                continue;
            }
            if (hackMode && command == "-HackPaths")
            {
                // We require at least 1 argument
                assert(currentArg + 1 < argList.size() && argList[currentArg + 1][0] != L'-',
                    "-HackPaths requires a input to be provided (usage: -HackPaths <input>");

                // store 3 paths: default NPP_JI, NPP_GT, MVD_JI
                options.hackPaths.push_back(std::filesystem::path(argList[currentArg + 1]));
                const auto nTargets = count_exr_files(std::filesystem::path(options.hackPaths[0]));

                // path += string works but path + string does not.
                auto gtPath = options.hackPaths.front().parent_path() += "/NPP_GT";
				assert(std::filesystem::exists(gtPath), "4k ground truth exr files must be stored in %s", gtPath.c_str());
                options.hackPaths.push_back(gtPath);
                const auto gtCount = count_exr_files(std::filesystem::path(options.hackPaths[1]));

                auto jitterPath = options.hackPaths.front().parent_path() += "/MVD_JI";
                assert(std::filesystem::exists(jitterPath), "Encoded MVs and Depths exr files must be stored in %s", jitterPath.c_str());
                options.hackPaths.push_back(jitterPath);
                const auto jitterCount = count_exr_files(std::filesystem::path(options.hackPaths[2]));

                assert(nTargets == gtCount && nTargets == jitterCount,
                    "frame capture count and jitter count of (%s) (%s) (%s) should match, but got %d, %d, and %d", 
					options.hackPaths[0].c_str(), options.hackPaths[1].c_str(), options.hackPaths[2].c_str(),
                    nTargets, gtCount, jitterCount);

                options.frameCount = static_cast<size_t>(nTargets);
                /// When "-HackPaths" comes before "-OutputMaxCount", nothing to do here
                /// When "-OutputMaxCount" comes first, we cap it.
                if (options.outputMaxCount > options.frameCount) {
                    options.outputMaxCount = options.frameCount;
                }

                currentArg++;
                continue;
            }
            if (hackMode && command == "-StoreOutput")
            {
                options.storeOutput = true;
                continue;
            }
            if (hackMode && command == "-OutputMaxCount")
            {
                // We require at least 1 argument
                assert(currentArg + 1 < argList.size() && argList[currentArg + 1][0] != L'-',
                    "-OutputMaxCount requires a input to be provided (usage: -OutputMaxCount <input>");

                /// When "-HackPaths" comes before "-OutputMaxCount", we cap it here
                /// When "-OutputMaxCount" comes first, "-HackPaths" will cap it.
                options.outputMaxCount = std::stoull(argList[currentArg + 1]);  // size_t is u long long
                if (options.frameCount > 0 && options.hackPaths.empty() == false) {
                    options.outputMaxCount = std::min(options.outputMaxCount, options.frameCount);
                }

                currentArg++;
                continue;
            }

            if (hackMode && command == "-OutputPath")
            {
                // We require at least 1 argument
                assert(currentArg + 1 < argList.size() && argList[currentArg + 1][0] != L'-',
                    L"-OutputPath requires a input to be provided (usage: -OutputPath <input>");
                options.outPath = std::filesystem::path(argList[currentArg + 1]);
                currentArg++;
                continue;
            }
        }

        // manual change for DEBUG
        //options.enableHack = false;
        options.storeOutput = false;
        return options;
    }

    /**
     * Copy the parmanent option in App to RenderTargets local.
     */
    void setHackOptions(const HackOptionDef& options) {
        hackOptions = options;
	}

    bool LoadHackTextures(std::shared_ptr<donut::engine::TextureCache> textureCache) {
		typedef donut::engine::TextureCache::HackDataType hackDataType;

        auto loadFrameCaptures = [&]
            (const std::filesystem::path& hackPath, hackDataType dtype)
            -> std::vector<std::filesystem::path> 
            {
                std::vector<std::filesystem::path> filePaths;
				bool shouldParseJitter = dtype == hackDataType::COLOR_HDR && hackOptions.parseJitter;
                size_t nFiles = textureCache->TraverseFolderPath(
                    hackPath, filePaths, shouldParseJitter, hackLoadedJitterOffsets, ".exr");
                // double check
                assert(nFiles == hackOptions.frameCount,
                    "#input files counted by TraverseFolder() (%d) and lambda function in parser (%d) don't match.",
                    nTextures,
                    hackOptions.frameCount);


                if (nFiles < hackOptions.outputMaxCount) {
                    donut::log::error("Expect to run %d frames more than %s frame captures: %d",
                        hackOptions.outputMaxCount, hackPath.generic_string(), nFiles);
                }

                // we may want to run only 5 frames even there are 60 in the folder
                for (size_t frameIdx = 0; frameIdx < hackOptions.outputMaxCount; ++frameIdx) {
                    auto& filePath = filePaths[frameIdx];

                    std::shared_ptr<donut::engine::TextureData> loadedTexture = 
                        textureCache->hackLoadTextureFromFile(filePath, dtype);
                        
                    switch (dtype)
                    {
                    case hackDataType::COLOR_LDR:
                        hackLoadedColorsLDR.push_back(loadedTexture);
                        break;
                    case hackDataType::COLOR_HDR:
                        hackLoadedColorsHDR.push_back(loadedTexture);
                        break;
                    case hackDataType::MOTION_VECTORS:
                        hackLoadedMVs.push_back(loadedTexture);
                        break;
                    case hackDataType::GBUFFER_DEPTH:
                        hackLoadedDepths.push_back(loadedTexture);
                        break;
                    default:
                        donut::log::error("Unsupported hackDataType %d", static_cast<int>(dtype));
                        break;
                    }
                }

                return filePaths;
            };

        auto exrFiles = loadFrameCaptures(hackOptions.hackPaths[0], hackDataType::COLOR_HDR);
        auto pngFiles = loadFrameCaptures(hackOptions.hackPaths[1], hackDataType::COLOR_LDR);
        auto mvFiles = loadFrameCaptures(hackOptions.hackPaths[2], hackDataType::MOTION_VECTORS);
		auto depthFiles = loadFrameCaptures(hackOptions.hackPaths[2], hackDataType::GBUFFER_DEPTH);

        return true;
    }
};