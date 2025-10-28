/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include <donut/engine/SceneTypes.h>
#include <donut/core/log.h>

#include <nvrhi/nvrhi.h>
#include <atomic>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <queue>

#ifdef DONUT_WITH_TASKFLOW
namespace tf
{
    class Executor;
}
#endif

namespace donut::vfs
{
    class IBlob;
    class IFileSystem;
}

namespace donut::engine
{
    class CommonRenderPasses;

    struct TextureSubresourceData
    {
        size_t rowPitch = 0;
        size_t depthPitch = 0;
        ptrdiff_t dataOffset = 0;
        size_t dataSize = 0;
    };

    struct TextureData : public LoadedTexture
    {
        std::shared_ptr<vfs::IBlob> data;

        nvrhi::Format format = nvrhi::Format::UNKNOWN;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t arraySize = 1;
        uint32_t mipLevels = 1;
        nvrhi::TextureDimension dimension = nvrhi::TextureDimension::Unknown;
        bool isRenderTarget = false;
        bool forceSRGB = false;

        // ArraySlice -> MipLevel -> TextureSubresourceData
        std::vector<std::vector<TextureSubresourceData>> dataLayout;
    };

    class TextureCache
    {
    public:
        enum class HackDataType {
            COLOR_HDR = 0,
            MOTION_VECTORS = 1,
            GBUFFER_DEPTH = 2,
        };
    protected:
        nvrhi::DeviceHandle m_Device;
        nvrhi::CommandListHandle m_CommandList;
        std::unordered_map<std::string, std::shared_ptr<TextureData>> m_LoadedTextures;
        mutable std::shared_mutex m_LoadedTexturesMutex;

        std::queue<std::shared_ptr<TextureData>> m_TexturesToFinalize;
        std::shared_ptr<DescriptorTableManager> m_DescriptorTable;
        std::mutex m_TexturesToFinalizeMutex;
    
        std::shared_ptr<vfs::IFileSystem> m_fs;

        uint32_t m_MaxTextureSize = 0;

        bool m_GenerateMipmaps = true;

        log::Severity m_InfoLogSeverity = log::Severity::Info;
        log::Severity m_ErrorLogSeverity = log::Severity::Warning;

        std::atomic<uint32_t> m_TexturesRequested = 0;
        std::atomic<uint32_t> m_TexturesLoaded = 0;
        uint32_t m_TexturesFinalized = 0;

        bool FindTextureInCache(const std::filesystem::path& path, std::shared_ptr<TextureData>& texture);
        std::shared_ptr<vfs::IBlob> ReadTextureFile(const std::filesystem::path& path) const;

        bool FillTextureData(
            const std::shared_ptr<vfs::IBlob>& fileData,
            const std::shared_ptr<TextureData>& texture,
            const std::string& extension,
            const std::string& mimeType) const;

        // helpers
        /**
         * Load RGBA16_FLOAT or BGRA8_UNORM frame captures.
         */
        bool hackLoadEXRFromFile(
            char** outputData,
            int* width, int* height,
            std::filesystem::path textureFile) const;

        /**
         * Load RG16_FLOAT motion vectors or D24S8 depth.
         */
        bool hackLoadJitterFromFile(
            char** outputData,
            int* width, int* height,
            std::filesystem::path textureFile,
            bool isMV) const;

        void FinalizeTexture(
            std::shared_ptr<TextureData> texture,
            CommonRenderPasses* passes,
            nvrhi::ICommandList* commandList);

        virtual void TextureLoaded(std::shared_ptr<TextureData> texture);
        virtual std::shared_ptr<TextureData> CreateTextureData();

    public:
        TextureCache(
            nvrhi::IDevice* device,
            std::shared_ptr<vfs::IFileSystem> fs,
            std::shared_ptr<DescriptorTableManager> descriptorTable);
        virtual ~TextureCache();

        // Release all cached textures
        void Reset();

        // Synchronous read and decode, synchronous upload and mip generation on a given command list (must be open).
        // The `passes` argument is optional, and mip generation is disabled if it's NULL.
        virtual std::shared_ptr<LoadedTexture> LoadTextureFromFile(
            const std::filesystem::path& path,
            bool sRGB,
            CommonRenderPasses* passes,
            nvrhi::ICommandList* commandList);

        /**
         * hack version of LoadTextureFromFile, but works very differently:
         * Unlike LoadTextureFromFile, which calls FinalizeTexture to recreate data at texture->texture
         * and clear TextureData* texture->data,
         * this function does not create texture->texture or clear texture->data.
         *
         * `texture` is struct TextureData : public LoadedTexture
         * `texture->data` is IBlob*, i.e. raw bytes
         * `texture->texture` is TextureHandle, a member of LoadedTexture         *
         */
        std::shared_ptr<TextureData> hackLoadTextureFromFile(
            const std::filesystem::path& path,
            HackDataType dtype);

        // Synchronous read and decode, deferred upload and mip generation (in the ProcessRenderingThreadCommands queue).
        virtual std::shared_ptr<LoadedTexture> LoadTextureFromFileDeferred(
            const std::filesystem::path& path,
            bool sRGB);

#ifdef DONUT_WITH_TASKFLOW
        // Asynchronous read and decode, deferred upload and mip generation (in the ProcessRenderingThreadCommands queue).
        virtual std::shared_ptr<LoadedTexture> LoadTextureFromFileAsync(
            const std::filesystem::path& path,
            bool sRGB,
            tf::Executor& executor);

        // Same as LoadTextureFromFileAsync, but using a memory blob and MIME type instead of file name, and uncached.
        virtual std::shared_ptr<LoadedTexture> LoadTextureFromMemoryAsync(
            const std::shared_ptr<vfs::IBlob>& data,
            const std::string& name,
            const std::string& mimeType,
            bool sRGB,
            tf::Executor& executor);
#endif

        // Same as LoadTextureFromFile, but using a memory blob and MIME type instead of file name, and uncached.
        virtual std::shared_ptr<LoadedTexture> LoadTextureFromMemory(
            const std::shared_ptr<vfs::IBlob>& data,
            const std::string& name,
            const std::string& mimeType,
            bool sRGB,
            CommonRenderPasses* passes,
            nvrhi::ICommandList* commandList);

        // Same as LoadTextureFromFileDeferred, but using a memory blob and MIME type instead of file name, and uncached.
        virtual std::shared_ptr<LoadedTexture> LoadTextureFromMemoryDeferred(
            const std::shared_ptr<vfs::IBlob>& data,
            const std::string& name,
            const std::string& mimeType,
            bool sRGB);
        
        int TraverseFolderPath(
            const std::filesystem::path& folderPath,
            std::vector<std::filesystem::path>& outPaths,
            bool extractJitter,
            std::vector<donut::math::float2>& jitterXY,
            std::string extension);

        // Tells if the texture has been loaded from file successfully and its data is available in the texture object.
        // After the texture is finalized and uploaded to the GPU, the data is no longer available on the CPU,
        // and this function returns false.
        bool IsTextureLoaded(const std::shared_ptr<LoadedTexture>& texture);

        // Tells if the texture has been uploaded to the GPU
        bool IsTextureFinalized(const std::shared_ptr<LoadedTexture>& texture);

        // Removes the texture from cache. The texture must *not* be in the deferred finalization queue when it's unloaded.
        // Returns true if the texture has been found and removed from the cache, false otherwise.
        // Note: Any existing handles for the texture remain valid after the texture is unloaded.
        //       Texture lifetimes are tracked by NVRHI and the texture object is only destroyed when no references exist.
        bool UnloadTexture(const std::shared_ptr<LoadedTexture>& texture);

        // Process a portion of the upload queue, taking up to `timeLimitMilliseconds` CPU time.
        // If `timeLimitMilliseconds` is 0, processes the entire queue.
        // Returns true if any textures have been processed.
        bool ProcessRenderingThreadCommands(CommonRenderPasses& passes, float timeLimitMilliseconds);

        // Destroys the internal command list in order to release the upload buffers used in it.
        void LoadingFinished();

        // Set the maximum texture size allowed after load. Larger textures are resized to fit this constraint.
        // Currently does not affect DDS textures.
        void SetMaxTextureSize(uint32_t size);

        // Enables or disables automatic mip generation for loaded textures.
        void SetGenerateMipmaps(bool generateMipmaps);

        // Sets the Severity of log messages about textures being loaded.
        void SetInfoLogSeverity(log::Severity value) { m_InfoLogSeverity = value; }

        // Sets the Severity of log messages about textures that couldn't be loaded.
        void SetErrorLogSeverity(log::Severity value) { m_ErrorLogSeverity = value; }

        uint32_t GetNumberOfLoadedTextures() { return m_TexturesLoaded.load(); }
        uint32_t GetNumberOfRequestedTextures() { return m_TexturesRequested.load(); }
        uint32_t GetNumberOfFinalizedTextures() { return m_TexturesFinalized; }

		std::shared_ptr<TextureData> GetLoadedTexture(std::filesystem::path const& path);

		// Texture cache traversal
		// Note: the iterator locks all cache write-accesses for the duration its lifespan !
		class Iterator
		{
		public:
			typedef std::unordered_map<std::string, std::shared_ptr<TextureData>>::iterator CacheIter;

			Iterator& operator++() { ++m_Iterator; return *this; }
			
			friend bool operator==(Iterator const& a, Iterator const& b) { return a.m_Iterator == b.m_Iterator; }
			friend bool operator!=(Iterator const& a, Iterator const& b) { return !(a == b); }

			CacheIter const& operator->() const { return m_Iterator; }

		private:

			friend class TextureCache;
			
			Iterator(std::shared_mutex& mutex, CacheIter it) : m_Lock(mutex), m_Iterator(it) { }

			std::shared_lock<std::shared_mutex> m_Lock;
			CacheIter m_Iterator;
		};

		Iterator begin() { return Iterator(m_LoadedTexturesMutex, m_LoadedTextures.begin()); }

		Iterator end() { return Iterator(m_LoadedTexturesMutex, m_LoadedTextures.end()); }
    };

    // Saves the contents of texture's slice 0 mip level 0 into an image file.
    // The image format is determined from the file's extension.
    // Supported formats are: BMP, PNG, JPG, TGA.
    // Requires that no immediate command list is open at the time this function is called.
    // Creates and destroys temporary resources internally, so should NOT be called often.
    bool SaveTextureToFile(
        nvrhi::IDevice* device,
        CommonRenderPasses* pPasses,
        nvrhi::ITexture* texture,
        nvrhi::ResourceStates textureState,
        const char* fileName,
        bool saveAlphaChannel = true);

    /**
     * Attempt 1: Save RTs like PreUIColor and AAResolvedColor.
     * They do not store FG frames.
     * 
     * Attempt 2: Save swapchain back buffers (3 per frame).
     * They are in m_SwapChainFramebuffers of DeviceManager.
     * They do not store FG frames either.
     */
    [[deprecated("Cannot export FG frames, deprecated")]]
    bool SaveRTsToEXR(
        nvrhi::IDevice* device,
        nvrhi::ITexture* texture,
        const char* fileName);

    bool SaveMVDepthsToEXR(
        bool isMV,
        nvrhi::IDevice* device,
        nvrhi::ITexture* texture,
        const char* fileName);

    /**
     * Attempt 5: Since BitBlt() we used in Attempt 4 cannot capture HDR data, we try to manually
     * tone map the 8-bit LDR data and save to exr. Unfortunately, the result looks way off.
     */
    [[deprecated("WRONG LOOKING IMAGE - DO NOT USE")]]
    bool SaveTMedLDRToEXR(const uint32_t* bgraData, const char* fileName, const uint32_t width, const uint32_t height);

    bool TestTinyExrWrite();

    /**
     * Attempt 8 Helper: After we map the ID3D11Texture2D to staging texture (D3D11_MAPPED_SUBRESOURCE mapped),
     * save it to exr file using tinyexr.
     * 
     * First 2 args should be mapped.pData and mapped.RowPitch
     */
    bool SaveStagingTextureDataToEXR(const void* pData, const uint32_t rowPitch, const int width, const int height, const std::string filename);

}
