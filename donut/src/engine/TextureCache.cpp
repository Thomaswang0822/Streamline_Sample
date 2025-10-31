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

/*
License for stb

Public Domain

This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this 
software, either in source code form or as a compiled binary, for any purpose, 
commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of this 
software dedicate any and all copyright interest in the software to the public 
domain. We make this dedication for the benefit of the public at large and to 
the detriment of our heirs and successors. We intend this dedication to be an 
overt act of relinquishment in perpetuity of all present and future rights to 
this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN 
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION 
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <donut/engine/TextureCache.h>

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ConsoleObjects.h>
#include <donut/engine/DDSFile.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>

#ifdef DONUT_WITH_TASKFLOW
#include <taskflow/taskflow.hpp>
#endif

#include <stb_image.h>
#include <stb_image_write.h>


#ifdef DONUT_WITH_MINIZ
    #ifdef DONUT_WITH_TINYEXR
        // tinyexr has its own copy of miniz, need to disable it
        // if we have a separate integration to avoid linker errors
        #define TINYEXR_USE_MINIZ 0
    #endif // DONUT_WITH_TINYEXR

    #include <miniz.h>

#endif

#ifdef DONUT_WITH_TINYEXR

    #if defined (_MSC_VER)
        #pragma warning(push)
        #pragma warning(disable:4018) // Silence warning from tinyEXR
    #endif

    #define TINYEXR_IMPLEMENTATION
    #include <tinyexr.h>

    #if defined (_MSC_VER)
        #pragma warning(pop)
    #endif

#endif // DONUT_WITH_TINYEXR

#include <algorithm>
#include <chrono>
#include <regex>

using namespace donut::math;
using namespace donut::vfs;
using namespace donut::engine;

class StbImageBlob : public IBlob
{
private:
    unsigned char* m_data = nullptr;

public:
    StbImageBlob(unsigned char* _data) : m_data(_data) 
    {
    }

    virtual ~StbImageBlob()
    {
        if (m_data)
        {
            stbi_image_free(m_data);
            m_data = nullptr;
        }
    }

    virtual const void* data() const override
    {
        return m_data;
    }

    virtual size_t size() const override
    {
        return 0; // nobody cares
    }
};


TextureCache::TextureCache(
    nvrhi::IDevice* device,
    std::shared_ptr<IFileSystem> fs,
    std::shared_ptr<DescriptorTableManager> descriptorTable)
    : m_Device(device)
    , m_DescriptorTable(std::move(descriptorTable))
    , m_fs(std::move(fs))
{
}

TextureCache::~TextureCache()
{
    Reset();
}

void TextureCache::Reset()
{
	std::lock_guard<std::shared_mutex> guard(m_LoadedTexturesMutex);

	m_LoadedTextures.clear();

    m_TexturesRequested = 0;
    m_TexturesLoaded = 0;
}

void TextureCache::SetGenerateMipmaps(bool generateMipmaps)
{
    m_GenerateMipmaps = generateMipmaps;
}

bool TextureCache::FindTextureInCache(const std::filesystem::path& path, std::shared_ptr<TextureData>& texture)
{
    std::lock_guard<std::shared_mutex> guard(m_LoadedTexturesMutex);

    // First see if this texture is already loaded (or being loaded).

    texture = m_LoadedTextures[path.generic_string()];
    if (texture)
    {
        return true;
    }

    // Allocate a new texture slot for this file name and return it. Load the file later in a thread pool.
    // LoadTextureFromFileAsync function for a given scene is only called from one thread, so there is no 
    // chance of loading the same texture twice.

    texture = CreateTextureData();
    m_LoadedTextures[path.generic_string()] = texture;

    ++m_TexturesRequested;

    return false;
}

std::shared_ptr<IBlob> TextureCache::ReadTextureFile(const std::filesystem::path& path) const
{
    auto fileData = m_fs->readFile(path);

    if (!fileData)
        log::message(m_ErrorLogSeverity, "Couldn't read texture file '%s'", path.generic_string().c_str());

    return fileData;
}

std::shared_ptr<TextureData> TextureCache::CreateTextureData()
{
    return std::make_shared<TextureData>();
}

bool TextureCache::FillTextureData(
    const std::shared_ptr<vfs::IBlob>& fileData,
    const std::shared_ptr<TextureData>& texture,
    const std::string& extension,
    const std::string& mimeType) const
{
    if (extension == ".dds" || extension == ".DDS" || mimeType == "image/vnd-ms.dds")
    {
        texture->data = fileData;
        if (!LoadDDSTextureFromMemory(*texture))
        {
            texture->data = nullptr;
            log::message(m_ErrorLogSeverity, "Couldn't load DDS texture '%s'", texture->path.c_str());
            return false;
        }
    }
#ifdef DONUT_WITH_TINYEXR
    else if (extension == ".exr" || extension == ".EXR" || mimeType == "image/aces")
    {
        float* data = nullptr;
        int width = 0, height = 0;
        char const* err = nullptr;

        // This reads only 1 or 4 channel images and duplicates channels
        // Should rewrite w/ lower level EXR functions
        if (LoadEXRFromMemory(&data, &width, &height, (uint8_t*)fileData->data(), fileData->size(), &err) == TINYEXR_SUCCESS)
        {
            uint32_t channels = 4;
            uint32_t bytesPerPixel = channels * 4;

            texture->data = std::make_shared<Blob>(data, bytesPerPixel * width * height);
            texture->width = static_cast<uint32_t>(width);
            texture->height = static_cast<uint32_t>(height);
            texture->format = nvrhi::Format::RGBA32_FLOAT;

            texture->originalBitsPerPixel = channels * 32;
            texture->isRenderTarget = true;
            texture->mipLevels = 1;
            texture->dimension = nvrhi::TextureDimension::Texture2D;

            texture->dataLayout.resize(1);
            texture->dataLayout[0].resize(1);
            texture->dataLayout[0][0].dataOffset = 0;
            texture->dataLayout[0][0].rowPitch = static_cast<size_t>(width * bytesPerPixel);
            texture->dataLayout[0][0].dataSize = static_cast<size_t>(width * height * bytesPerPixel);

            return true;
        }
        else
        {
            log::warning("Couldn't load EXR texture '%s'", texture->path.c_str());
            return false;
        }
    }
#endif // DONUT_WITH_TINYEXR
    else
    {
        int width = 0, height = 0, originalChannels = 0, channels = 0;

        if (!stbi_info_from_memory(
            static_cast<const stbi_uc*>(fileData->data()), 
            static_cast<int>(fileData->size()), 
            &width, &height, &originalChannels))
        {
            log::message(m_ErrorLogSeverity, "Couldn't process image header for texture '%s'", texture->path.c_str());
            return false;
        }

        bool is_hdr = stbi_is_hdr_from_memory(
            static_cast<const stbi_uc*>(fileData->data()),
            static_cast<int>(fileData->size()));

        if (originalChannels == 3)
        {
            channels = 4;
        }
        else {
            channels = originalChannels;
        }

        unsigned char* bitmap;
        int bytesPerPixel = channels * (is_hdr ? 4 : 1);
        
        if (is_hdr)
        {
            float* floatmap = stbi_loadf_from_memory(
                static_cast<const stbi_uc*>(fileData->data()),
                static_cast<int>(fileData->size()),
                &width, &height, &originalChannels, channels);

            bitmap = reinterpret_cast<unsigned char*>(floatmap);
        }
        else
        {
            bitmap = stbi_load_from_memory(
                static_cast<const stbi_uc*>(fileData->data()),
                static_cast<int>(fileData->size()),
                &width, &height, &originalChannels, channels);
        }

        if (!bitmap)
        {
            log::message(m_ErrorLogSeverity, "Couldn't load generic texture '%s'", texture->path.c_str());
            return false;
        }

        texture->originalBitsPerPixel = static_cast<uint32_t>(originalChannels) * (is_hdr ? 32 : 8);
        texture->width = static_cast<uint32_t>(width);
        texture->height = static_cast<uint32_t>(height);
        texture->isRenderTarget = true;
        texture->mipLevels = 1;
        texture->dimension = nvrhi::TextureDimension::Texture2D;

        texture->dataLayout.resize(1);
        texture->dataLayout[0].resize(1);
        texture->dataLayout[0][0].dataOffset = 0;
        texture->dataLayout[0][0].rowPitch = static_cast<size_t>(width * bytesPerPixel);
        texture->dataLayout[0][0].dataSize = static_cast<size_t>(width * height * bytesPerPixel);

        texture->data = std::make_shared<StbImageBlob>(bitmap);
        bitmap = nullptr; // ownership transferred to the blob

        switch (channels)
        {
        case 1:
            texture->format = is_hdr ? nvrhi::Format::R32_FLOAT : nvrhi::Format::R8_UNORM;
            break;
        case 2:
            texture->format = is_hdr ? nvrhi::Format::RG32_FLOAT : nvrhi::Format::RG8_UNORM;
            break;
        case 4:
            texture->format = is_hdr ? nvrhi::Format::RGBA32_FLOAT :
                (texture->forceSRGB ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM);
            break;
        default:
            texture->data.reset(); // release the bitmap data

            log::message(m_ErrorLogSeverity, "Unsupported number of components (%d) for texture '%s'", channels, texture->path.c_str());
            return false;
        }
    }

    return true;
}

bool TextureCache::hackLoadEXRFromFile(
    char** outputData,
    int* width, int* height,
    std::filesystem::path textureFile) const
{
    std::string fileName = textureFile.string();

    // Modern TinyEXR API
    const char* err = nullptr;
    EXRHeader   header;
    EXRImage    image;
    InitEXRHeader(&header);
    InitEXRImage(&image);

    // 1. Parse version
    EXRVersion version;
    int        ret = ParseEXRVersionFromFile(&version, fileName.c_str());
    if (ret != TINYEXR_SUCCESS)
    {
        log::error("Invalid EXR version: %s", fileName.c_str());
        return false;
    }

    // 2. Parse header
    ret = ParseEXRHeaderFromFile(&header, &version, fileName.c_str(), &err);
    if (ret != TINYEXR_SUCCESS)
    {
        if (err)
        {
            log::error("EXR header error: %s", err);
        }
        return false;
    }

    // 3. Ensure tinyexr read as FP16 according to the spec
    for (int i = 0; i < header.num_channels; i++)
    {
        assert(header.requested_pixel_types[i] == TINYEXR_PIXELTYPE_HALF, 
            "Input spec says each RGB channel is 16 bits.");
    }

    // 4. Load image data
    ret = LoadEXRImageFromFile(&image, &header, fileName.c_str(), &err);
    if (ret != TINYEXR_SUCCESS)
    {
        if (err)
        {
            log::error("EXR load error: %s", err);
        }
        return false;
    }

    // 5. Find RGB channels (assume first 3 channels are RGB)
    int idxR = -1, idxG = -1, idxB = -1, idxA = -1;
    for (int c = 0; c < header.num_channels; c++)
    {
        if (strcmp(header.channels[c].name, "R") == 0)
            idxR = c;
        else if (strcmp(header.channels[c].name, "G") == 0)
            idxG = c;
        else if (strcmp(header.channels[c].name, "B") == 0)
            idxB = c;
        else if (strcmp(header.channels[c].name, "A") == 0)
            idxA = c;
    }

    // Default to first 3 channels if not found
    assert(idxR != -1 && idxG != -1 && idxB != -1,
        "EXR file %ls has missing (idx = -1) RGB channels: idxR = %d, idxG = %d, idxB = %d",
        fileName.c_str(), idxR, idxG, idxB);

    // 6. Convert to target format
    const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);

    // Get channel pointers; tinyexr use uint16_t = unsigned short for FP16
    uint16_t* r = idxR != -1 ? reinterpret_cast<uint16_t*>(image.images[idxR]) : nullptr;
    uint16_t* g = idxG != -1 ? reinterpret_cast<uint16_t*>(image.images[idxG]) : nullptr;
    uint16_t* b = idxB != -1 ? reinterpret_cast<uint16_t*>(image.images[idxB]) : nullptr;
    uint16_t* a = idxA >= 0 ? reinterpret_cast<uint16_t*>(image.images[idxA]) : nullptr;
    assert(r != nullptr && g != nullptr && b != nullptr,
        "EXR file %ls has null channel pointers when converting to uint16_t: r = %p, g = %p, b = %p",
        fileName.c_str(), r, g, b);

    // prepare FP16 1.0f constant
    tinyexr::FP32 fp32_ONE; fp32_ONE.f = 1.0f;
    const uint16_t      fp16_ONE = tinyexr::float_to_half_full(fp32_ONE).u;

    // first malloc byte array: RGBA16_Float is 4 channels x 2 bytes
    const size_t bytesPerPixel = 4 * 2;
    char* finalCharData = static_cast<char*>(malloc(pixelCount * bytesPerPixel));
    if (!finalCharData)
    {
        log::error("Failed to allocate memory for EXR texture data.");
        return false;
    }

    // store texture； Can directly use FP16
    uint16_t* fp16Data = reinterpret_cast<uint16_t*>(finalCharData);
    uint8_t*  u8Data = reinterpret_cast<uint8_t*>(finalCharData);
    assert(finalCharData != nullptr && fp16Data != nullptr,
        L"Failed to reinterpret_cast for EXR texture.");

    size_t idxSrc, idxDst;
    // used for converting to BGRA8_UNORM
    auto convertToU8 = [](uint16_t value) -> uint8_t {
        tinyexr::FP16 half; half.u = value;
        float fHDR = tinyexr::half_to_float(half).f;
        // toneMap to 0.0-1.0
        float fLDR = fHDR / (1.0f + fHDR);

        return static_cast<uint8_t>(fLDR * 256.f);
        };
    
    for (size_t i = 0; i < image.height; ++i)
    {
        for (size_t j = 0; j < image.width; ++j)
        {
            idxSrc = i * image.width + j;
            idxDst = i * image.width + j;

            // store directly to uint16_t*
            fp16Data[4 * idxDst + 0] = r[idxSrc];
            fp16Data[4 * idxDst + 1] = g[idxSrc];
            fp16Data[4 * idxDst + 2] = b[idxSrc];
            fp16Data[4 * idxDst + 3] = a ? a[idxSrc] : fp16_ONE;
        }
    }

    // write output in the end
    *outputData = finalCharData;
    *width = image.width;
    *height = image.height;
    return true;
}

bool TextureCache::hackLoadJitterFromFile(
    char** outputData,
    int* width, int* height,
    std::filesystem::path textureFile,
    bool isMV) const
{
#ifndef DONUT_WITH_TINYEXR
    log::error("hackFillTextureData requires DONUT_WITH_TINYEXR");
    return false;
#endif

    // both RG16_FLOAT motion vectors or D24S8 depth are 4 bytes per pixel
	const size_t bytesPerPixel = 4;

    // Initialize EXR structures
    EXRVersion version;
    EXRHeader  header;
    EXRImage   image;
    InitEXRHeader(&header);
    InitEXRImage(&image);
    const char* err = nullptr;

    // Parse EXR version
    std::string fileName = textureFile.string();
    int         ret = ParseEXRVersionFromFile(&version, fileName.c_str());
    if (ret != TINYEXR_SUCCESS)
    {
        log::error("Invalid EXR version: %s", fileName.c_str());
        return false;
    }

    // Parse EXR header
    ret = ParseEXRHeaderFromFile(&header, &version, fileName.c_str(), &err);
    if (ret != TINYEXR_SUCCESS)
    {
        if (err)
        {
            log::error("EXR header error: %s", err);
        }
        return false;
    }

    // Ensure tinyexr read as FP16 according to the spec
    for (int i = 0; i < header.num_channels; i++)
    {
        assert(header.requested_pixel_types[i] == TINYEXR_PIXELTYPE_HALF, "Input spec says each RGB channel is 16 bits.");
    }

    // Load EXR image
    ret = LoadEXRImageFromFile(&image, &header, fileName.c_str(), &err);
    if (ret != TINYEXR_SUCCESS)
    {
        if (err)
        {
            log::error("EXR load error: %s", err);
        }
        return false;
    }

    // Find channel indices (R=motionX, G=motionY, B=depth)
    int idxR = -1, idxG = -1, idxB = -1;
    for (int c = 0; c < header.num_channels; c++)
    {
        if (strcmp(header.channels[c].name, "R") == 0)
            idxR = c;
        else if (strcmp(header.channels[c].name, "G") == 0)
            idxG = c;
        else if (strcmp(header.channels[c].name, "B") == 0)
            idxB = c;
    }

    // Validate required channels
    if (isMV && (idxR == -1 || idxG == -1))
    {
        log::error("Motion vectors require R and G channels in %ls", textureFile.c_str());
        return false;
    }
    if (!isMV && idxB == -1)
    {
        log::error("Depth requires B channel in %ls", textureFile.c_str());
        return false;
    }

    // Get channel pointers; tinyexr use uint16_t = unsigned short for FP16
    uint16_t* r = idxR != -1 ? reinterpret_cast<uint16_t*>(image.images[idxR]) : nullptr;
    uint16_t* g = idxG != -1 ? reinterpret_cast<uint16_t*>(image.images[idxG]) : nullptr;
    uint16_t* b = idxB != -1 ? reinterpret_cast<uint16_t*>(image.images[idxB]) : nullptr;
    assert(r != nullptr && g != nullptr && b != nullptr,
        L"EXR file %ls has null channel pointers when converting to uint16_t: r = %p, g = %p, b = %p",
        fileName.c_str(), r, g, b);

    /// Set input, output, and data size.
    /// Input is always 1k. 
    /// Data size = input * ratio = render resolution. E.g. when we upscale 2k render to 4k display,
    ///     we need to "expand" 1k jitter to 2k by interpolation.
    /// Output = render resolution * m_UpscaleRatio = display resolution. This is how big to malloc.
    const size_t imgWidth = static_cast<size_t>(image.width);
    const size_t imgHeight = static_cast<size_t>(image.height);
    assert(imgWidth == 1920 && imgHeight == 1080, L"Jitter EXR input must be 1k resolution.");
    
    // Allocate raw bytes array first, then reinterpret_cast to FP16 or FP32
    char* charData = static_cast<char*>(malloc(imgWidth * imgHeight * bytesPerPixel));
    if (!charData)
    {
        log::error("Memory allocation failed for %ls", textureFile.c_str());
        return false;
    }

    /// NOTE: jitter data is 1k fixed
    size_t idxSrc, idxDst;
    if (isMV)
    {
        uint16_t* fp16Data = reinterpret_cast<uint16_t*>(charData);
        // donut has mvec in pixel space
        const float ratioX = static_cast<float>(imgWidth);
        const float ratioY = static_cast<float>(imgHeight);
        auto scaleMV = [](uint16_t value, float ratio) -> uint16_t
            {
                tinyexr::FP16 half; half.u = value;
                tinyexr::FP32 flt = half_to_float(half);
                flt.f *= ratio;
                return float_to_half_full(flt).u;
            };
        for (int y = 0; y < imgHeight; y++)
        {
            for (int x = 0; x < imgWidth; x++)
            {
                idxSrc = y * imgWidth + x;
                idxDst = (y * imgWidth + x) * 2;  // each mv stored as 2 fp16

                // no interpolation needed
                fp16Data[idxDst]     = scaleMV(r[idxSrc], -ratioX);  // mv.X
                fp16Data[idxDst + 1] = scaleMV(g[idxSrc], +ratioY);  // mv.Y
            }
        } // end iterating the image
    }
    else {
        /// VERY IMPORTANT NOTE:
        /// http://gamedev.net/forums/topic/632751-dxgi_format-codes-and-endianness/4989986/
        /// "For any DXGI format, the byte order is the order of the components in the format name. 
        /// So for R8G8B8A8, R should be the first (lowest) byte and A should be the last (highest) byte."
        uint32_t* u32Data = reinterpret_cast<uint32_t*>(charData);
        for (int y = 0; y < imgHeight; y++)
        {
            for (int x = 0; x < imgWidth; x++)
            {

                idxSrc = y * imgWidth + x;
                idxDst = y * imgWidth + x;  // each depth stored as a D24S8-encoded bits

                // Key: convert FP16 to D24S8 format, where LS 8 bits are stencil set to 0
                // Extract 24 depth bits
                tinyexr::FP16 h; h.u = b[idxSrc];
                float depthValue = half_to_float(h).f;
				assert(depthValue >= 0.0f && depthValue <= 1.0f);
                // Convert float to 24-bit integer depth
                const uint32_t u24MAX = (1 << 24) - 1;
                uint32_t depth24 = static_cast<uint32_t>(depthValue * u24MAX);
                u32Data[idxDst] = depth24; // upper 8 bits automatically 0
            }
        } // end iterating the image
    }

    // write output in the end
    *outputData = charData;
    *width = image.width;
    *height = image.height;
    return true;
}

uint GetMipLevelsNum(uint width, uint height)
{
    uint size = std::min(width, height);
    uint levelsNum = (uint)(logf((float)size) / logf(2.0f)) + 1;

    return levelsNum;
}

void TextureCache::FinalizeTexture(
    std::shared_ptr<TextureData> texture,
    CommonRenderPasses* passes,
    nvrhi::ICommandList* commandList)
{
    assert(texture->data);
    assert(commandList);

    uint originalWidth = texture->width;
    uint originalHeight = texture->height;

    bool isBlockCompressed =
        (texture->format == nvrhi::Format::BC1_UNORM) ||
        (texture->format == nvrhi::Format::BC1_UNORM_SRGB) ||
        (texture->format == nvrhi::Format::BC2_UNORM) ||
        (texture->format == nvrhi::Format::BC2_UNORM_SRGB) ||
        (texture->format == nvrhi::Format::BC3_UNORM) ||
        (texture->format == nvrhi::Format::BC3_UNORM_SRGB) ||
        (texture->format == nvrhi::Format::BC4_SNORM) ||
        (texture->format == nvrhi::Format::BC4_UNORM) ||
        (texture->format == nvrhi::Format::BC5_SNORM) ||
        (texture->format == nvrhi::Format::BC5_UNORM) ||
        (texture->format == nvrhi::Format::BC6H_SFLOAT) ||
        (texture->format == nvrhi::Format::BC6H_UFLOAT) ||
        (texture->format == nvrhi::Format::BC7_UNORM) ||
        (texture->format == nvrhi::Format::BC7_UNORM_SRGB);

    if (isBlockCompressed)
    {
        originalWidth = (originalWidth + 3) & ~3;
        originalHeight = (originalHeight + 3) & ~3;
    }

    uint scaledWidth = originalWidth;
    uint scaledHeight = originalHeight;

    if (m_MaxTextureSize > 0 && int(std::max(originalWidth, originalHeight)) > m_MaxTextureSize &&
        texture->isRenderTarget && texture->dimension == nvrhi::TextureDimension::Texture2D)
    {
        if (originalWidth >= originalHeight)
        {
            scaledHeight = originalHeight * m_MaxTextureSize / originalWidth;
            scaledWidth = m_MaxTextureSize;
        }
        else
        {
            scaledWidth = originalWidth * m_MaxTextureSize / originalHeight;
            scaledHeight = m_MaxTextureSize;
        }
    }

    const char* dataPointer = static_cast<const char*>(texture->data->data());

    nvrhi::TextureDesc textureDesc;
    textureDesc.format = texture->format;
    textureDesc.width = scaledWidth;
    textureDesc.height = scaledHeight;
    textureDesc.depth = texture->depth;
    textureDesc.arraySize = texture->arraySize;
    textureDesc.dimension = texture->dimension;
    textureDesc.mipLevels = m_GenerateMipmaps && texture->isRenderTarget && passes
        ? GetMipLevelsNum(textureDesc.width, textureDesc.height)
        : texture->mipLevels;
    textureDesc.debugName = texture->path;
    textureDesc.isRenderTarget = texture->isRenderTarget;
    texture->texture = m_Device->createTexture(textureDesc);

    commandList->beginTrackingTextureState(texture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);

    if (m_DescriptorTable)
        texture->bindlessDescriptor = m_DescriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::Texture_SRV(0, texture->texture));
    
    if (scaledWidth != originalWidth || scaledHeight != originalHeight)
    {
        nvrhi::TextureDesc tempTextureDesc;
        tempTextureDesc.format = texture->format;
        tempTextureDesc.width = originalWidth;
        tempTextureDesc.height = originalHeight;
        tempTextureDesc.depth = textureDesc.depth;
        tempTextureDesc.arraySize = textureDesc.arraySize;
        tempTextureDesc.mipLevels = 1;
        tempTextureDesc.dimension = textureDesc.dimension;

        nvrhi::TextureHandle tempTexture = m_Device->createTexture(tempTextureDesc);
        assert(tempTexture);
        commandList->beginTrackingTextureState(tempTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);

        for (uint32_t arraySlice = 0; arraySlice < texture->arraySize; arraySlice++)
        {
            const TextureSubresourceData& layout = texture->dataLayout[arraySlice][0];

            commandList->writeTexture(tempTexture, arraySlice, 0, dataPointer + layout.dataOffset,
                layout.rowPitch, layout.depthPitch);
        }

        nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
            .addColorAttachment(texture->texture));
        
        passes->BlitTexture(commandList, framebuffer, tempTexture);
    }
    else
    {
        for (uint32_t arraySlice = 0; arraySlice < texture->arraySize; arraySlice++)
        {
            for (uint32_t mipLevel = 0; mipLevel < texture->mipLevels; mipLevel++)
            {
                const TextureSubresourceData& layout = texture->dataLayout[arraySlice][mipLevel];

                commandList->writeTexture(texture->texture, arraySlice, mipLevel, dataPointer + layout.dataOffset,
                    layout.rowPitch, layout.depthPitch);
            }
        }
    }

    texture->data.reset();

    for (uint mipLevel = texture->mipLevels; mipLevel < textureDesc.mipLevels; mipLevel++)
    {
        nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
            .addColorAttachment(nvrhi::FramebufferAttachment()
                .setTexture(texture->texture)
                .setArraySlice(0)
                .setMipLevel(mipLevel)));
        
        BlitParameters blitParams;
        blitParams.sourceTexture = texture->texture;
        blitParams.sourceMip = mipLevel - 1;
        blitParams.targetFramebuffer = framebuffer;
        passes->BlitTexture(commandList, blitParams);
    }

    commandList->setPermanentTextureState(texture->texture, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    ++m_TexturesFinalized;
}

void TextureCache::TextureLoaded(std::shared_ptr<TextureData> texture)
{
    std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

    if (texture->mimeType.empty())
        log::message(m_InfoLogSeverity, "Loaded %d x %d, %d bpp: %s", texture->width, texture->height,
        texture->originalBitsPerPixel, texture->path.c_str());
    else
        log::message(m_InfoLogSeverity, "Loaded %d x %d, %d bpp: %s (%s)", texture->width, texture->height,
        texture->originalBitsPerPixel, texture->path.c_str(), texture->mimeType.c_str());
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromFile(
    const std::filesystem::path& path,
    bool sRGB,
    CommonRenderPasses* passes,
    nvrhi::ICommandList* commandList)
{
    std::shared_ptr<TextureData> texture;

    if (FindTextureInCache(path, texture))
        return texture;

    texture->forceSRGB = sRGB;
    texture->path = path.generic_string();

    auto fileData = ReadTextureFile(path);
    if (fileData)
    {
        if (FillTextureData(fileData, texture, path.extension().generic_string(), ""))
        {
            TextureLoaded(texture);

            FinalizeTexture(texture, passes, commandList);
        }
    }

    ++m_TexturesLoaded;

    return texture;
}

std::shared_ptr<TextureData> TextureCache::hackLoadTextureFromFile(
    const std::filesystem::path& path,
    HackDataType dtype)
{
    std::shared_ptr<TextureData> texture = CreateTextureData();
    std::string pathStr = path.generic_string();
    texture->path = pathStr;

    int width = 0, height = 0;
    char* data = nullptr;
    char const* err = nullptr;
    int channels = 4;
    // LDR, HDR, MV, Depth are BGRA8_UNORM, RGBA16_FLOAT, RG16_FLOAT, D24S8 respectively.
    uint32_t bytesPerPixel = dtype == HackDataType::COLOR_HDR ? channels * 2 : channels;
    switch (dtype)
    {
    case HackDataType::COLOR_HDR:
    {
        if (!hackLoadEXRFromFile(&data, &width, &height, pathStr)) {
            log::error("Couldn't load EXR frame '%s'", texture->path.c_str());
            return nullptr;
        }
        texture->format = nvrhi::Format::RGBA16_FLOAT;
        texture->data = std::make_shared<Blob>(data, bytesPerPixel * width * height);

        break;
    }
    case HackDataType::MOTION_VECTORS:
    {
        if (!hackLoadJitterFromFile(&data, &width, &height, pathStr, true /* isMV */)) {
            log::error("Couldn't load EXR MV '%s'", texture->path.c_str());
            return nullptr;
        }
        texture->format = nvrhi::Format::RG16_FLOAT;
        texture->data = std::make_shared<Blob>(data, bytesPerPixel * width * height);

        break;
    }
    case HackDataType::GBUFFER_DEPTH:
    {
        if (!hackLoadJitterFromFile(&data, &width, &height, pathStr, false /* isMV */)) {
            log::error("Couldn't load EXR Depth '%s'", texture->path.c_str());
            return nullptr;
        }
        texture->format = nvrhi::Format::D24S8;
        texture->data = std::make_shared<Blob>(data, bytesPerPixel * width * height);

        break;
    }
    default:
        log::error("Unsupported HackDataType %d", static_cast<int>(dtype));

        return nullptr;
    }

    // ownership transferred to the blob
    data = nullptr; 

    // write common attributes
    texture->width = static_cast<uint32_t>(width);
    texture->height = static_cast<uint32_t>(height);

    texture->originalBitsPerPixel = static_cast<uint32_t>(channels) * 8;
    texture->isRenderTarget = true;
    texture->mipLevels = 1;
    texture->dimension = nvrhi::TextureDimension::Texture2D;

    texture->dataLayout.resize(1);
    texture->dataLayout[0].resize(1);
    texture->dataLayout[0][0].dataOffset = 0;
    texture->dataLayout[0][0].rowPitch = static_cast<size_t>(width * bytesPerPixel);
    texture->dataLayout[0][0].dataSize = static_cast<size_t>(width * height * bytesPerPixel);


    //hackFinalizeTexture(texture, dtype);
    ++m_TexturesLoaded;

    return texture;
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromFileDeferred(
    const std::filesystem::path& path,
    bool sRGB)
{
    std::shared_ptr<TextureData> texture;

    if (FindTextureInCache(path, texture))
        return texture;

    texture->forceSRGB = sRGB;
    texture->path = path.generic_string();

    auto fileData = ReadTextureFile(path);
    if (fileData)
    {
        if (FillTextureData(fileData, texture, path.extension().generic_string(), ""))
        {
            TextureLoaded(texture);

            std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

            m_TexturesToFinalize.push(texture);
        }
    }

    ++m_TexturesLoaded;

    return texture;
}

#ifdef DONUT_WITH_TASKFLOW
std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromFileAsync(
    const std::filesystem::path& path,
    bool sRGB,
    tf::Executor& executor)
{
    std::shared_ptr<TextureData> texture;

    if (FindTextureInCache(path, texture))
        return texture;

    texture->forceSRGB = sRGB;
    texture->path = path.generic_string();

    executor.async([this, texture, path]()
    {
        auto fileData = ReadTextureFile(path);
        if (fileData)
        {
            if (FillTextureData(fileData, texture, path.extension().generic_string(), ""))
            {
                TextureLoaded(texture);

                std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

                m_TexturesToFinalize.push(texture);
            }
        }

        ++m_TexturesLoaded;
    });

    return texture;
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromMemoryAsync(
    const std::shared_ptr<vfs::IBlob>& data,
    const std::string& name,
    const std::string& mimeType,
    bool sRGB,
    tf::Executor& executor)
{
    std::shared_ptr<TextureData> texture = CreateTextureData();
    
    texture->forceSRGB = sRGB;
    texture->path = name;
    texture->mimeType = mimeType;

    executor.async([this, texture, data, mimeType]()
        {
            if (FillTextureData(data, texture, "", mimeType))
            {
                TextureLoaded(texture);

                std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

                m_TexturesToFinalize.push(texture);
            }

            ++m_TexturesLoaded;
        });

    return texture;
}
#endif

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromMemory(
    const std::shared_ptr<vfs::IBlob>& data,
    const std::string& name,
    const std::string& mimeType,
    bool sRGB,
    CommonRenderPasses* passes,
    nvrhi::ICommandList* commandList)
{
    std::shared_ptr<TextureData> texture = CreateTextureData();
    
    texture->forceSRGB = sRGB;
    texture->path = name;
    texture->mimeType = mimeType;

    if (FillTextureData(data, texture, "", mimeType))
    {
        TextureLoaded(texture);

        FinalizeTexture(texture, passes, commandList);
    }

    ++m_TexturesLoaded;

    return texture;
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromMemoryDeferred(
    const std::shared_ptr<vfs::IBlob>& data,
    const std::string& name,
    const std::string& mimeType,
    bool sRGB)
{
    std::shared_ptr<TextureData> texture = CreateTextureData();
    
    texture->forceSRGB = sRGB;
    texture->path = name;
    texture->mimeType = mimeType;

    if (FillTextureData(data, texture, "", mimeType))
    {
        TextureLoaded(texture);

        std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

        m_TexturesToFinalize.push(texture);
    }
    
    ++m_TexturesLoaded;

    return texture;
}

int TextureCache::TraverseFolderPath(
    const std::filesystem::path& folderPath, 
    std::vector<std::filesystem::path>& outPaths)
{
    /// vfs::IFileSystem works relative to project root, i.e. "/media/whatever"
    /// while cwd is at _build/, i.e. "../media/whatever"
    std::filesystem::path ifsPath(folderPath.string().substr(2));
    int count = m_fs->enumerateFiles(ifsPath, { ".exr" },
        [&folderPath, &outPaths](std::string_view name)
        {
            // but still output correct relative path for tinyexr to use
            outPaths.push_back((folderPath / name).generic_string());
        });

    // Sort files to ensure proper frame order (assuming filenames contain frame numbers)
    std::sort(outPaths.begin(), outPaths.end());

    return outPaths.size();
}

void TextureCache::LoadJitterFromFileLists(
    const std::vector<std::filesystem::path>& FilePaths, 
    std::vector<donut::math::float2>& jitterXY,
    const uint32_t FramesToReplayTotal,
    const uint32_t FramesToCapture)
{
    // Then iterate the sorted list to keep the jitter order consistent
    // this func is also called when reading MV and Depths, so we clear conditionally.
    jitterXY.clear();
    for (const auto& entry : FilePaths)
    {
        /// Example: NPP_beauty_2472_0000_0_-0.40563965_-0.35599041
        /// NOTE: both XY are .8f with range in [-0.5, 0.5]
        try
        {
            std::string pathStr = entry.stem().generic_string();

            size_t lastDelim = pathStr.find_last_of('_');
            size_t secondLastDelim = pathStr.find_last_of('_', lastDelim - 1);
            if (lastDelim == std::string::npos || secondLastDelim == std::string::npos)
                donut::log::error("EXR jitter filename %ls does not have expected number of underscores.", pathStr);

            // 2nd-last X, last Y
            jitterXY.push_back(float2(
                std::stof(pathStr.substr(secondLastDelim + 1, lastDelim - secondLastDelim - 1)),
                std::stof(pathStr.substr(lastDelim + 1))
            ));
        }
        catch (const std::exception& e)
        {
            donut::log::error("%s", e.what());
        }
    }

    const size_t nFiles = jitterXY.size();
    /// But make sure we at least FramesToReplayTotal = 19 entries.
    /// This will happen ONLY IF we have < 19 input files.
    /// 
    /// First make the middle part (frames to be captured) to
    if (nFiles < FramesToReplayTotal)
    {
        size_t index = 0;
        while (jitterXY.size() < FramesToCapture) {
            jitterXY.push_back(jitterXY[index]);
			index = (index + 1) % nFiles;
        }
        // then make 15 to 18
        auto safetyJitter = jitterXY.back();
        // preprend last 3 entries as warmup frames
        jitterXY.insert(jitterXY.begin(), jitterXY.end() - 3, jitterXY.end());
        // append first entries as safety frame
        jitterXY.push_back(safetyJitter);
	}
	assert(jitterXY.size() >= FramesToReplayTotal);
    return;
}

std::shared_ptr<TextureData> TextureCache::GetLoadedTexture(std::filesystem::path const& path)
{
	std::lock_guard<std::shared_mutex> guard(m_LoadedTexturesMutex);
	return m_LoadedTextures[path.generic_string()];
}

bool TextureCache::ProcessRenderingThreadCommands(CommonRenderPasses& passes, float timeLimitMilliseconds)
{
    using namespace std::chrono;

    time_point<high_resolution_clock> startTime = high_resolution_clock::now();

    uint commandsExecuted = 0;
    while (true)
    {
        std::shared_ptr<TextureData> pTexture;

        if (timeLimitMilliseconds > 0 && commandsExecuted > 0)
        {
            time_point<high_resolution_clock> now = high_resolution_clock::now();

            if (float(duration_cast<microseconds>(now - startTime).count()) > timeLimitMilliseconds * 1e3f)
                break;
        }

        {
            std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

            if (m_TexturesToFinalize.empty())
                break;

            pTexture = m_TexturesToFinalize.front();
            m_TexturesToFinalize.pop();
        }

        if (pTexture->data)
        {
            commandsExecuted += 1;

            if (!m_CommandList)
            {
                m_CommandList = m_Device->createCommandList();
            }

            m_CommandList->open();

            FinalizeTexture(pTexture, &passes, m_CommandList);

            m_CommandList->close();
            m_Device->executeCommandList(m_CommandList);
            m_Device->runGarbageCollection();
        }
    }

    return (commandsExecuted > 0);
}

void TextureCache::LoadingFinished()
{
    m_CommandList = nullptr;
}

void TextureCache::SetMaxTextureSize(uint32_t size)
{
	m_MaxTextureSize = size;
}

#ifdef _MSC_VER 
#define strcasecmp _stricmp
#endif

namespace donut::engine
{
    bool SaveTextureToFile(
        nvrhi::IDevice* device,
        CommonRenderPasses* pPasses,
        nvrhi::ITexture* texture,
        nvrhi::ResourceStates textureState,
        const char* fileName,
        bool saveAlphaChannel)
    {
        if (!fileName)
            return false;

        // Find the file's extension
        char const* ext = strrchr(fileName, '.');

        if (!ext)
            return false; // No extension fond in the file name

        // Determine the image format from the extension
        enum { BMP, PNG, JPG, TGA } destFormat;
        if (strcasecmp(ext, ".bmp") == 0)
            destFormat = BMP;
        else if (strcasecmp(ext, ".png") == 0)
            destFormat = PNG;
        else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
            destFormat = JPG;
        else if (strcasecmp(ext, ".tga") == 0)
            destFormat = TGA;
        else
            return false; // Unknown file type
        
        if (destFormat == JPG)
            saveAlphaChannel = false;

        nvrhi::TextureDesc desc = texture->getDesc();
        nvrhi::TextureHandle tempTexture;
        nvrhi::FramebufferHandle tempFramebuffer;

        nvrhi::CommandListHandle commandList = device->createCommandList();
        commandList->open();

        if (textureState != nvrhi::ResourceStates::Unknown)
        {
            commandList->beginTrackingTextureState(texture, nvrhi::TextureSubresourceSet(0, 1, 0, 1), textureState);
        }

        // If the source texture format is not RGBA8, create a temporary texture and blit into it to convert
        switch (desc.format)
        {
        case nvrhi::Format::RGBA8_UNORM:
        case nvrhi::Format::SRGBA8_UNORM:
            tempTexture = texture;
            break;
        default:
            desc.format = nvrhi::Format::SRGBA8_UNORM;
            desc.isRenderTarget = true;
            desc.initialState = nvrhi::ResourceStates::RenderTarget;
            desc.keepInitialState = true;

            tempTexture = device->createTexture(desc);
            tempFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(tempTexture));
            
            pPasses->BlitTexture(commandList, tempFramebuffer, texture);
        }

        // Create a staging texture to access the data from the CPU, copy the data into it
        nvrhi::StagingTextureHandle stagingTexture = device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
        commandList->copyTexture(stagingTexture, nvrhi::TextureSlice(), tempTexture, nvrhi::TextureSlice());

        if (textureState != nvrhi::ResourceStates::Unknown)
        {
            commandList->setTextureState(texture, nvrhi::TextureSubresourceSet(0, 1, 0, 1), textureState);
            commandList->commitBarriers();
        }

        commandList->close();
        device->executeCommandList(commandList);

        // Map the staging texture
        size_t rowPitch = 0;
        uint8_t const* pData = static_cast<uint8_t const*>(device->mapStagingTexture(
            stagingTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));

        if (!pData)
            return false;

        uint8_t* newData = nullptr;
        int channels = saveAlphaChannel ? 4 : 3;

        // If the mapped data is not laid out in a densely packed format with the right number of channels,
        // create a temporary buffer and move the data into the right layout for stb_image.
        if (rowPitch != desc.width * channels)
        {
            newData = new uint8_t[desc.width * desc.height * channels];

            for (uint32_t row = 0; row < desc.height; ++row)
            {
                uint8_t* dstRow = newData + row * desc.width * channels;
                uint8_t const* srcRow = pData + row * rowPitch;

                if (channels == 4)
                {
                    // Simple row copy
                    memcpy(dstRow, srcRow, desc.width * channels);
                }
                else
                {
                    // Convert 4 channels to 3
                    for (uint32_t col = 0; col < desc.width; ++col)
                    {
                        dstRow[0] = srcRow[0];
                        dstRow[1] = srcRow[1];
                        dstRow[2] = srcRow[2];
                        dstRow += 3;
                        srcRow += 4;
                    }
                }
            }

            pData = newData;
        }

        // Write the output image
        bool writeSuccess = false;
        switch(destFormat)
        {
            case BMP: 
                writeSuccess = stbi_write_bmp(fileName, int(desc.width), int(desc.height), channels, pData) != 0;
                break;
            case PNG: 
                writeSuccess = stbi_write_png(fileName, int(desc.width), int(desc.height), channels, pData, desc.width * channels) != 0;
                break;
            case JPG: 
                writeSuccess = stbi_write_jpg(fileName, int(desc.width), int(desc.height), channels, pData, /* quality = */ 99) != 0;
                break;
            case TGA: 
                writeSuccess = stbi_write_tga(fileName, int(desc.width), int(desc.height), channels, pData) != 0;
                break;
        }
        
        if (newData)
        {
            delete[] newData;
            newData = nullptr;
        }

        device->unmapStagingTexture(stagingTexture);

        return writeSuccess;
    }

    bool SaveRTsToEXR(nvrhi::IDevice* device, nvrhi::ITexture* texture, const char* fileName)
    {
        assert(false, "SaveRTsToEXR() is deprecated because it cannot capture FG frames");

        const auto& desc = texture->getDesc();

        // Create command list and staging texture
        nvrhi::CommandListHandle commandList = device->createCommandList();
        commandList->open();

        nvrhi::StagingTextureHandle stagingTexture = device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
        commandList->copyTexture(stagingTexture, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());

        commandList->close();
        device->executeCommandList(commandList);

        // Map staging texture - get raw data pointer
        size_t rowPitchBytes = 0;
        const void* rawData = device->mapStagingTexture(
            stagingTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitchBytes);

        if (!rawData)
            return false;

        // Proper reinterpretation depending on HDR or LDR
        bool isHDR = false;
        switch (desc.format)
        {
        case nvrhi::Format::RGBA16_FLOAT:
            isHDR = true;
			break;
        case nvrhi::Format::BGRA8_UNORM:
            break;
        default:
			log::error("Unsupported texture format in SaveHackToEXR() only BGRA8_UNORM and RGBA8_UNORM accepted.");
            return false;
        }

		const uint8_t* u8Data = reinterpret_cast<const uint8_t*>(rawData);
        const uint16_t* u16Data = reinterpret_cast<const uint16_t*>(rawData);

        const uint32_t width = desc.width;
        const uint32_t height = desc.height;
        const int channels = 4; // RGBA
        const size_t bytesPerPixel = channels * (isHDR ? 2 : 1); // 4 channels * 2 or 1 bytes each
        const size_t expectedRowPitch = width * bytesPerPixel;
        assert(rowPitchBytes == expectedRowPitch, "Expect rowPitchBytes to be %d, got %d", expectedRowPitch, rowPitchBytes);

        // Prepare EXR structures
        EXRHeader header;
        InitEXRHeader(&header);
        EXRImage exrImage;
        InitEXRImage(&exrImage);

        // Configure EXR header
        header.num_channels = 4;
        header.channels = new EXRChannelInfo[header.num_channels];
        header.pixel_types = new int[header.num_channels];
        header.requested_pixel_types = new int[header.num_channels];

        /// The texture stores data in RGBA order, but TEV open it as ABGR (alphetical order).
        //const char channel_names[] = { 'R', 'G', 'B', 'A' };
        const char channel_names[4] = { 'A', 'B', 'G', 'R' };
        for (int i = 0; i < header.num_channels; i++) {
            //strncpy(header.channels[i].name, channel_names[i], 255);
            header.channels[i].name[0] = channel_names[i];
            header.channels[i].name[1] = '\0';
            header.pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
            header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
        }

        header.compression_type = TINYEXR_COMPRESSIONTYPE_NONE;

        // Configure EXR image
        exrImage.num_channels = header.num_channels;
        exrImage.width = width;
        exrImage.height = height;

        // Allocate planar arrays for RGBA channels
        std::vector<std::vector<uint16_t>> channelData(header.num_channels);
        for (auto& channel : channelData) {
            channel.resize(width * height);
        }

        // Calculate row pitch in terms of uint16_t elements
        const size_t rowPitchElements = rowPitchBytes / (isHDR ? sizeof(uint16_t) : sizeof(uint8_t));

        auto unorm8ToFP16 = [](uint8_t value) -> uint16_t {
            tinyexr::FP32 f32; 
            f32.f = static_cast<float>(value) / 255.0f;
            return  tinyexr::float_to_half_full(f32).u;
			};

        // Deinterleave pixel data into planar format
        for (uint32_t y = 0; y < height; y++) {
            if (isHDR) {
                const uint16_t* srcRow = u16Data + y * rowPitchElements;

                for (uint32_t x = 0; x < width; x++) {
                    const size_t dstIdx = y * width + x;
                    const size_t srcIdx = x * 4; // 4 channels per pixel

                    // Reverse the channel order to match EXR expectations
                    channelData[0][dstIdx] = srcRow[srcIdx + 3]; // A into R
                    channelData[1][dstIdx] = srcRow[srcIdx + 2]; // B into G
                    channelData[2][dstIdx] = srcRow[srcIdx + 1]; // G into B
                    channelData[3][dstIdx] = srcRow[srcIdx + 0]; // R into A
                }
            }
            else {
                const uint8_t* srcRow = u8Data + y * rowPitchElements;

                for (uint32_t x = 0; x < width; x++) {
                    const size_t dstIdx = y * width + x;
                    const size_t srcIdx = x * 4; // 4 channels per pixel

                    // BGRA -> ABGR
                    channelData[0][dstIdx] = unorm8ToFP16(srcRow[srcIdx + 3]);
                    channelData[1][dstIdx] = unorm8ToFP16(srcRow[srcIdx + 0]);
                    channelData[2][dstIdx] = unorm8ToFP16(srcRow[srcIdx + 1]);
                    channelData[3][dstIdx] = unorm8ToFP16(srcRow[srcIdx + 2]);
				}
            }
        }

        // Prepare channel pointers for EXR
        std::vector<unsigned char*> imagePtrs(header.num_channels);
        for (int i = 0; i < header.num_channels; i++) {
            imagePtrs[i] = reinterpret_cast<unsigned char*>(channelData[i].data());
        }
        exrImage.images = imagePtrs.data();

        // Save EXR file
        const char* err = nullptr;
        int ret = SaveEXRImageToFile(&exrImage, &header, fileName, &err);
        bool success = (ret == TINYEXR_SUCCESS);

        // Cleanup
        delete[] header.channels;
        delete[] header.pixel_types;
        delete[] header.requested_pixel_types;

        device->unmapStagingTexture(stagingTexture);

        return success;
    }

    bool SaveMVDepthsToEXR(bool isMV, nvrhi::IDevice* device, nvrhi::ITexture* texture, const char* fileName)
    {
        const auto& desc = texture->getDesc();

        // Create command list and staging texture
        nvrhi::CommandListHandle commandList = device->createCommandList();
        commandList->open();

        nvrhi::StagingTextureHandle stagingTexture = device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
        commandList->copyTexture(stagingTexture, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());

        commandList->close();
        device->executeCommandList(commandList);

        // Map staging texture - get raw data pointer
        size_t rowPitchBytes = 0;
        const void* rawData = device->mapStagingTexture(
            stagingTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitchBytes);

        if (!rawData)
            return false;

        const uint16_t* u16Data = reinterpret_cast<const uint16_t*>(rawData);
		const uint32_t* u32Data = reinterpret_cast<const uint32_t*>(rawData);

        const uint32_t width = desc.width;
        const uint32_t height = desc.height;
        const int channels = isMV ? 2 : 1; // MV x and y
		const size_t bytesPerPixel = channels * isMV ? 2 : 4; // RG16_FLOAT : D24S8
        const size_t expectedRowPitch = width * bytesPerPixel;
        //assert(rowPitchBytes == expectedRowPitch, "Expect rowPitchBytes to be %d, got %d", expectedRowPitch, rowPitchBytes);

        // Prepare EXR structures
        EXRHeader header;
        InitEXRHeader(&header);
        EXRImage exrImage;
        InitEXRImage(&exrImage);

        // Configure EXR header
        header.num_channels = channels;
        header.channels = new EXRChannelInfo[header.num_channels];
        header.pixel_types = new int[header.num_channels];
        header.requested_pixel_types = new int[header.num_channels];

        /// The texture stores data in RGBA order, but TEV open it as ABGR (alphetical order).
        const char channel_names[2] = {'X', 'Y'};
        for (int i = 0; i < header.num_channels; i++) {
            //strncpy(header.channels[i].name, channel_names[i], 255);
            header.channels[i].name[0] = channel_names[i];
            header.channels[i].name[1] = '\0';
            header.pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
            header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
        }

        header.compression_type = TINYEXR_COMPRESSIONTYPE_NONE;

        // Configure EXR image
        exrImage.num_channels = header.num_channels;
        exrImage.width = width;
        exrImage.height = height;

        // Allocate planar arrays for RGBA channels
        std::vector<std::vector<uint16_t>> channelData(header.num_channels);
        for (auto& channel : channelData) {
            channel.resize(width * height);
        }

        // Calculate row pitch in terms of uint16_t elements
        const size_t rowPitchElements = rowPitchBytes / (isMV ? sizeof(uint16_t) : sizeof(uint32_t));;

        constexpr uint32_t FP24MAX = 0x00FFFFFF;
        auto float_to_half = [](float value) -> uint16_t {
            tinyexr::FP32 f32;
			f32.f = value;
			return tinyexr::float_to_half_full(f32).u;
        };

        // Deinterleave pixel data into planar format
        for (uint32_t y = 0; y < height; y++) {
            if (isMV) {
                const uint16_t* srcRow = u16Data + y * rowPitchElements;

                for (uint32_t x = 0; x < width; x++) {
                    const size_t dstIdx = y * width + x;
                    const size_t srcIdx = x * channels; // 2 channels per pixel

                    channelData[0][dstIdx] = srcRow[srcIdx + 0]; // X
                    channelData[1][dstIdx] = srcRow[srcIdx + 1]; // Y
                }
            }
            else {
                // Depth texture is D24S8 format
                const uint32_t* srcRow = u32Data + y * rowPitchElements;

                for (uint32_t x = 0; x < width; x++) {
                    uint32_t pixel = srcRow[x];
                    // Extract depth from D24S8 (depth in lower bits)
                    uint32_t depth24 = pixel & FP24MAX;
                    float depthValue = static_cast<float>(depth24) / static_cast<float>(FP24MAX);
                    // Convert to FP16
                    uint16_t halfDepth = float_to_half(depthValue);

                    size_t dstIdx = y * width + x;
                    channelData[0][dstIdx] = halfDepth;
                }
            }
        }

        // Prepare channel pointers for EXR
        std::vector<unsigned char*> imagePtrs(header.num_channels);
        for (int i = 0; i < header.num_channels; i++) {
            imagePtrs[i] = reinterpret_cast<unsigned char*>(channelData[i].data());
        }
        exrImage.images = imagePtrs.data();

        // Save EXR file
        const char* err = nullptr;
        int ret = SaveEXRImageToFile(&exrImage, &header, fileName, &err);
        bool success = (ret == TINYEXR_SUCCESS);

        // Cleanup
        delete[] header.channels;
        delete[] header.pixel_types;
        delete[] header.requested_pixel_types;

        device->unmapStagingTexture(stagingTexture);

        return success;
    }

    bool SaveTMedLDRToEXR(const uint32_t* bgraData, const char* fileName, const uint32_t width, const uint32_t height)
    {
        assert(false, "SaveTMedLDRToEXR() generates wrong-looking output and thus shouldn't be used.");

        constexpr int channels = 3; // RGB
        EXRHeader header;
        InitEXRHeader(&header);
        EXRImage exrImage;
        InitEXRImage(&exrImage);

        // Configure EXR header
        header.num_channels = channels;
        header.channels = new EXRChannelInfo[header.num_channels];
        header.pixel_types = new int[header.num_channels];
        header.requested_pixel_types = new int[header.num_channels];

        /// The texture stores data in RGBA order, but TEV open it as ABGR (alphetical order).
        //const char channel_names[] = { 'R', 'G', 'B', 'A' };
        const char channel_names[channels] = { 'B', 'G', 'R' };
        for (int i = 0; i < header.num_channels; i++) {
            //strncpy(header.channels[i].name, channel_names[i], 255);
            header.channels[i].name[0] = channel_names[i];
            header.channels[i].name[1] = '\0';
            header.pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
            header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
        }

        header.compression_type = TINYEXR_COMPRESSIONTYPE_NONE;

        // Configure EXR image
        exrImage.num_channels = channels;
        exrImage.width = width;
        exrImage.height = height;

        // Allocate planar arrays for RGBA channels
        std::vector<std::vector<uint16_t>> channelData(header.num_channels);
        for (auto& channel : channelData) {
            channel.resize(width * height);
        }

        // helper lambda: UNORM8 to LDR FP16 represented by uint16_t
        auto unorm8ToFP16 = [](uint8_t value) -> uint16_t {
            tinyexr::FP32 f32;
            /// Reinhard tonemapping: ldr = hdr / (hdr + 1.0) = 1.0 - 1.0 / (hdr + 1.0)
			/// => hdr = 1.0 / (1.0 - ldr) - 1.0 = ldr / (1.0 - ldr)
            float ldr = static_cast<float>(value) / 255.0f;
			//f32.f = ldr / (1.0f - ldr + 1e-6f); // avoid div-by-zero
            f32.f = ldr;
            return  tinyexr::float_to_half_full(f32).u;
            };

        // Deinterleave pixel data into planar format
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                size_t index = static_cast<size_t>(y) * width + x;
                uint32_t pixel = bgraData[index];

                // Extract components: BGRA on little-endian (Windows) is 0xAARRGGBB
                channelData[0][index] = unorm8ToFP16((pixel >> 0) & 0xFF);
                channelData[1][index] = unorm8ToFP16((pixel >> 8) & 0xFF);
                channelData[2][index] = unorm8ToFP16((pixel >> 16) & 0xFF);
            }
        }

        // Prepare channel pointers for EXR
        std::vector<unsigned char*> imagePtrs(channels);
        for (int i = 0; i < header.num_channels; i++) {
            imagePtrs[i] = reinterpret_cast<unsigned char*>(channelData[i].data());
        }
        exrImage.images = imagePtrs.data();

        // Save EXR file
        const char* err = nullptr;
        int ret = SaveEXRImageToFile(&exrImage, &header, fileName, &err);
        bool success = (ret == TINYEXR_SUCCESS);

        // Cleanup
        delete[] header.channels;
        delete[] header.pixel_types;
        delete[] header.requested_pixel_types;

        return success;

    }

    bool TestTinyExrWrite()
    {
        EXRHeader header;
        EXRImage image;
        InitEXRHeader(&header);
        InitEXRImage(&image);

        int width = 1920;
        int height = 1080;
        int num_channels = 4;

        // Set up header
        header.num_channels = num_channels;
        header.channels = new EXRChannelInfo[num_channels];
        header.pixel_types = new int[num_channels];
        header.requested_pixel_types = new int[num_channels];

        //const char names[4] = { 'R', 'G', 'B', 'A' };
        /// TEV Viewer works like this:
        /// It "blindly" reorders the channel in alphabetical order,
        /// which means even if you pack data in RGBA order and set header.channels also,
        /// it display data wrongly as ABGR.
        /// In the channel names below, where NONE is a valid common channel name and H comes first,
        /// no matter how we order them, 
        /// TEV will show a single H-channel image with 0.9f as value, because it comes first.
        const char names[4] = { 'O', 'H', 'P', 'Q' };
        for (int i = 0; i < num_channels; i++) {
            header.channels[i].name[0] = names[i];
            header.channels[i].name[1] = '\0';
            header.pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
            header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
        }
        header.compression_type = TINYEXR_COMPRESSIONTYPE_NONE; // No compression for simplicity

        // Allocate data for each channel
        image.num_channels = num_channels;
        image.width = width;
        image.height = height;
        image.images = new unsigned char* [num_channels];

        // Create distinct test values for each channel:
        // Channel A: all 1.0f
        // Channel R: all 0.9f
        // Channel G: all 0.6f
        // Channel B: all 0.1f
        auto fToU16 = [](float value) {
            tinyexr::FP32 f32; 
            f32.f = value;
            return  tinyexr::float_to_half_full(f32).u;
            };
        std::vector<uint16_t> dataR(width * height, fToU16(0.9f));
        std::vector<uint16_t> dataG(width * height, fToU16(0.6f));
        std::vector<uint16_t> dataB(width * height, fToU16(0.1f));
        std::vector<uint16_t> dataA(width * height, fToU16(1.0f));

        image.images[0] = reinterpret_cast<unsigned char*>(dataR.data());
        image.images[1] = reinterpret_cast<unsigned char*>(dataG.data());
        image.images[2] = reinterpret_cast<unsigned char*>(dataB.data());
        image.images[3] = reinterpret_cast<unsigned char*>(dataA.data());

        // Save the EXR file
        const char* err = nullptr;
        int ret = SaveEXRImageToFile(&image, &header, "D:/Code/Streamline_Sample/media/TEST_SCENE/outputs/testWrite.exr", &err);
        if (ret != TINYEXR_SUCCESS) {
            printf("Error: %s\n", err);
            return 1;
        }

        // Cleanup
        delete[] header.channels;
        delete[] header.requested_pixel_types;
        delete[] image.images;

        printf("EXR file 'test.exr' created successfully.\n");
        return true;
    }

    bool SaveStagingTextureDataToEXR(
        const void* pData,
        const uint32_t rowPitch,
        const int width,
        const int height,
        const std::string filename)
    {
        const uint16_t* u16Data = reinterpret_cast<const uint16_t*>(pData);

        EXRHeader header;
        InitEXRHeader(&header);
        EXRImage exrImage;
        InitEXRImage(&exrImage);

        // Configure EXR header
        header.num_channels = 4;
        header.channels = new EXRChannelInfo[header.num_channels];
        header.pixel_types = new int[header.num_channels];
        header.requested_pixel_types = new int[header.num_channels];

        /// The texture stores data in RGBA order, but TEV open it as ABGR (alphetical order).
        //const char channel_names[4] = { 'R', 'G', 'B', 'A' };
        const char channel_names[4] = { 'A', 'B', 'G', 'R' };
        for (int i = 0; i < header.num_channels; i++) {
            //strncpy(header.channels[i].name, channel_names[i], 255);
            header.channels[i].name[0] = channel_names[i];
            header.channels[i].name[1] = '\0';
            header.pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
            header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
        }

        header.compression_type = TINYEXR_COMPRESSIONTYPE_NONE;

        // Configure EXR image
        exrImage.num_channels = header.num_channels;
        exrImage.width = width;
        exrImage.height = height;

        // Allocate planar arrays for RGBA channels
        std::vector<std::vector<uint16_t>> channelData(header.num_channels);
        for (auto& channel : channelData) {
            channel.resize(width * height);
        }

        // Calculate row pitch in terms of uint16_t elements
        const size_t rowPitchElements = rowPitch / sizeof(uint16_t);

        // Deinterleave pixel data into planar format
        for (uint32_t y = 0; y < height; y++) {
            const uint16_t* srcRow = reinterpret_cast<const uint16_t*>(
                static_cast<const uint8_t*>(pData) + y * rowPitch);

            for (uint32_t x = 0; x < width; x++) {
                const size_t dstIdx = y * width + x;
                const size_t srcIdx = x * 4; // 4 channels per pixel

                // Reverse the channel order to match EXR expectations
                channelData[0][dstIdx] = srcRow[srcIdx + 3]; // A into R
                channelData[1][dstIdx] = srcRow[srcIdx + 2]; // B into G
                channelData[2][dstIdx] = srcRow[srcIdx + 1]; // G into B
                channelData[3][dstIdx] = srcRow[srcIdx + 0]; // R into A
            }

        }

        // Prepare channel pointers for EXR
        std::vector<unsigned char*> imagePtrs(header.num_channels);
        for (int i = 0; i < header.num_channels; i++) {
            imagePtrs[i] = reinterpret_cast<unsigned char*>(channelData[i].data());
        }
        exrImage.images = imagePtrs.data();

        // Save EXR file
        const char* err = nullptr;
        int ret = SaveEXRImageToFile(&exrImage, &header, filename.c_str(), &err);
        bool success = (ret == TINYEXR_SUCCESS);

        // Cleanup
        delete[] header.channels;
        delete[] header.pixel_types;
        delete[] header.requested_pixel_types;
        
        return true;
    }

    bool TextureCache::IsTextureLoaded(const std::shared_ptr<LoadedTexture>& _texture)
    {
        TextureData* texture = static_cast<TextureData*>(_texture.get());

        return texture && texture->data;
    }

    bool TextureCache::IsTextureFinalized(const std::shared_ptr<LoadedTexture>& texture)
    {
        return texture->texture != nullptr;
    }

    bool TextureCache::UnloadTexture(const std::shared_ptr<LoadedTexture>& texture)
    {
        const auto& it = m_LoadedTextures.find(texture->path);

        if (it == m_LoadedTextures.end())
            return false;

        m_LoadedTextures.erase(it);

        return true;
    }

}
