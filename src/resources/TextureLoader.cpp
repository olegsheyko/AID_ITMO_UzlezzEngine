#include "TextureLoader.h"
#include "render/IRenderAdapter.h"
#include "core/Logger.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include <stb_image.h>

namespace {
constexpr std::size_t kDdsHeaderSize = 128;

bool endsWithCaseInsensitive(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }

    return std::equal(
        suffix.rbegin(),
        suffix.rend(),
        value.rbegin(),
        [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
        });
}

uint32_t readUInt32LE(const unsigned char* data) {
    return static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16) |
        (static_cast<uint32_t>(data[3]) << 24);
}

uint16_t readUInt16LE(const unsigned char* data) {
    return static_cast<uint16_t>(data[0]) |
        (static_cast<uint16_t>(data[1]) << 8);
}

unsigned char expand5To8(uint16_t value) {
    return static_cast<unsigned char>((value * 255u + 15u) / 31u);
}

unsigned char expand6To8(uint16_t value) {
    return static_cast<unsigned char>((value * 255u + 31u) / 63u);
}

void decodeRgb565(uint16_t packedColor, unsigned char& r, unsigned char& g, unsigned char& b) {
    r = expand5To8(static_cast<uint16_t>((packedColor >> 11) & 0x1F));
    g = expand6To8(static_cast<uint16_t>((packedColor >> 5) & 0x3F));
    b = expand5To8(static_cast<uint16_t>(packedColor & 0x1F));
}

void writePixel(
    unsigned char* pixels,
    uint32_t width,
    uint32_t x,
    uint32_t y,
    unsigned char r,
    unsigned char g,
    unsigned char b,
    unsigned char a) {
    const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4u;
    pixels[offset + 0] = r;
    pixels[offset + 1] = g;
    pixels[offset + 2] = b;
    pixels[offset + 3] = a;
}

void decodeDxtColorBlock(
    const unsigned char* block,
    unsigned char colors[4][4],
    bool allowTransparentColor) {
    const uint16_t color0 = readUInt16LE(block);
    const uint16_t color1 = readUInt16LE(block + 2);

    decodeRgb565(color0, colors[0][0], colors[0][1], colors[0][2]);
    colors[0][3] = 255;
    decodeRgb565(color1, colors[1][0], colors[1][1], colors[1][2]);
    colors[1][3] = 255;

    if (!allowTransparentColor || color0 > color1) {
        for (int channel = 0; channel < 3; ++channel) {
            colors[2][channel] = static_cast<unsigned char>((2u * colors[0][channel] + colors[1][channel]) / 3u);
            colors[3][channel] = static_cast<unsigned char>((colors[0][channel] + 2u * colors[1][channel]) / 3u);
        }
        colors[2][3] = 255;
        colors[3][3] = 255;
    } else {
        for (int channel = 0; channel < 3; ++channel) {
            colors[2][channel] = static_cast<unsigned char>((colors[0][channel] + colors[1][channel]) / 2u);
        }
        colors[2][3] = 255;
        colors[3][0] = 0;
        colors[3][1] = 0;
        colors[3][2] = 0;
        colors[3][3] = 0;
    }
}

bool decodeDxt1(const std::vector<unsigned char>& fileBytes, TextureData& textureData) {
    const uint32_t height = readUInt32LE(fileBytes.data() + 12);
    const uint32_t width = readUInt32LE(fileBytes.data() + 16);
    const uint32_t blockCountX = (width + 3u) / 4u;
    const uint32_t blockCountY = (height + 3u) / 4u;
    const std::size_t requiredSize = kDdsHeaderSize + static_cast<std::size_t>(blockCountX) * blockCountY * 8u;

    if (fileBytes.size() < requiredSize) {
        return false;
    }

    auto* pixels = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(width) * height * 4u));
    if (pixels == nullptr) {
        return false;
    }

    const unsigned char* blocks = fileBytes.data() + kDdsHeaderSize;
    for (uint32_t blockY = 0; blockY < blockCountY; ++blockY) {
        for (uint32_t blockX = 0; blockX < blockCountX; ++blockX) {
            const unsigned char* block = blocks + (static_cast<std::size_t>(blockY) * blockCountX + blockX) * 8u;
            unsigned char colors[4][4] = {};
            decodeDxtColorBlock(block, colors, true);
            const uint32_t colorIndices = readUInt32LE(block + 4);

            for (uint32_t row = 0; row < 4; ++row) {
                for (uint32_t col = 0; col < 4; ++col) {
                    const uint32_t pixelIndex = row * 4u + col;
                    const uint32_t paletteIndex = (colorIndices >> (pixelIndex * 2u)) & 0x3u;
                    const uint32_t x = blockX * 4u + col;
                    const uint32_t y = blockY * 4u + row;
                    if (x < width && y < height) {
                        writePixel(
                            pixels,
                            width,
                            x,
                            y,
                            colors[paletteIndex][0],
                            colors[paletteIndex][1],
                            colors[paletteIndex][2],
                            colors[paletteIndex][3]);
                    }
                }
            }
        }
    }

    textureData.width = static_cast<int>(width);
    textureData.height = static_cast<int>(height);
    textureData.channels = 4;
    textureData.pixels = pixels;
    return true;
}

bool decodeDxt5(const std::vector<unsigned char>& fileBytes, TextureData& textureData) {
    const uint32_t height = readUInt32LE(fileBytes.data() + 12);
    const uint32_t width = readUInt32LE(fileBytes.data() + 16);
    const uint32_t blockCountX = (width + 3u) / 4u;
    const uint32_t blockCountY = (height + 3u) / 4u;
    const std::size_t requiredSize = kDdsHeaderSize + static_cast<std::size_t>(blockCountX) * blockCountY * 16u;

    if (fileBytes.size() < requiredSize) {
        return false;
    }

    auto* pixels = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(width) * height * 4u));
    if (pixels == nullptr) {
        return false;
    }

    const unsigned char* blocks = fileBytes.data() + kDdsHeaderSize;
    for (uint32_t blockY = 0; blockY < blockCountY; ++blockY) {
        for (uint32_t blockX = 0; blockX < blockCountX; ++blockX) {
            const unsigned char* block = blocks + (static_cast<std::size_t>(blockY) * blockCountX + blockX) * 16u;

            unsigned char alphaValues[8] = {
                block[0], block[1], 0, 0, 0, 0, 0, 0
            };
            if (alphaValues[0] > alphaValues[1]) {
                alphaValues[2] = static_cast<unsigned char>((6u * alphaValues[0] + alphaValues[1]) / 7u);
                alphaValues[3] = static_cast<unsigned char>((5u * alphaValues[0] + 2u * alphaValues[1]) / 7u);
                alphaValues[4] = static_cast<unsigned char>((4u * alphaValues[0] + 3u * alphaValues[1]) / 7u);
                alphaValues[5] = static_cast<unsigned char>((3u * alphaValues[0] + 4u * alphaValues[1]) / 7u);
                alphaValues[6] = static_cast<unsigned char>((2u * alphaValues[0] + 5u * alphaValues[1]) / 7u);
                alphaValues[7] = static_cast<unsigned char>((alphaValues[0] + 6u * alphaValues[1]) / 7u);
            } else {
                alphaValues[2] = static_cast<unsigned char>((4u * alphaValues[0] + alphaValues[1]) / 5u);
                alphaValues[3] = static_cast<unsigned char>((3u * alphaValues[0] + 2u * alphaValues[1]) / 5u);
                alphaValues[4] = static_cast<unsigned char>((2u * alphaValues[0] + 3u * alphaValues[1]) / 5u);
                alphaValues[5] = static_cast<unsigned char>((alphaValues[0] + 4u * alphaValues[1]) / 5u);
                alphaValues[6] = 0;
                alphaValues[7] = 255;
            }

            uint64_t alphaIndices = 0;
            for (int byteIndex = 0; byteIndex < 6; ++byteIndex) {
                alphaIndices |= static_cast<uint64_t>(block[2 + byteIndex]) << (8 * byteIndex);
            }

            unsigned char colors[4][4] = {};
            decodeDxtColorBlock(block + 8, colors, false);
            const uint32_t colorIndices = readUInt32LE(block + 12);

            for (uint32_t row = 0; row < 4; ++row) {
                for (uint32_t col = 0; col < 4; ++col) {
                    const uint32_t pixelIndex = row * 4u + col;
                    const uint32_t paletteIndex = (colorIndices >> (pixelIndex * 2u)) & 0x3u;
                    const uint32_t alphaIndex = static_cast<uint32_t>((alphaIndices >> (pixelIndex * 3u)) & 0x7u);
                    const uint32_t x = blockX * 4u + col;
                    const uint32_t y = blockY * 4u + row;
                    if (x < width && y < height) {
                        writePixel(
                            pixels,
                            width,
                            x,
                            y,
                            colors[paletteIndex][0],
                            colors[paletteIndex][1],
                            colors[paletteIndex][2],
                            alphaValues[alphaIndex]);
                    }
                }
            }
        }
    }

    textureData.width = static_cast<int>(width);
    textureData.height = static_cast<int>(height);
    textureData.channels = 4;
    textureData.pixels = pixels;
    return true;
}

bool loadDdsTexture(const std::string& path, TextureData& textureData) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::vector<unsigned char> fileBytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (fileBytes.size() < kDdsHeaderSize || fileBytes[0] != 'D' || fileBytes[1] != 'D' || fileBytes[2] != 'S' || fileBytes[3] != ' ') {
        LOG_ERROR("Invalid DDS header: " + path);
        return false;
    }

    const std::string fourCC(
        reinterpret_cast<const char*>(fileBytes.data() + 84),
        reinterpret_cast<const char*>(fileBytes.data() + 88));

    if (fourCC == "DXT1") {
        return decodeDxt1(fileBytes, textureData);
    }
    if (fourCC == "DXT5") {
        return decodeDxt5(fileBytes, textureData);
    }

    LOG_ERROR("Unsupported DDS compression format in texture: " + path + " (" + fourCC + ")");
    return false;
}
}

bool TextureLoader::load(const std::string& path, TextureData& textureData, IRenderAdapter* renderer) {
    LOG_INFO("Loading texture from disk: " + path);

    FILE* testFile = std::fopen(path.c_str(), "rb");
    if (testFile == nullptr) {
        LOG_ERROR("Texture file does not exist or cannot be opened: " + path);
        return false;
    }
    std::fclose(testFile);

    if (endsWithCaseInsensitive(path, ".dds")) {
        if (!loadDdsTexture(path, textureData)) {
            LOG_ERROR("DDS texture load failed: " + path);
            return false;
        }
    } else {
        stbi_set_flip_vertically_on_load(false);

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 0);
        if (pixels == nullptr) {
            LOG_ERROR("stb_image failed to load texture: " + path);
            return false;
        }

        textureData.width = width;
        textureData.height = height;
        textureData.channels = channels;
        textureData.pixels = pixels;
    }

    const bool uploaded = uploadToGPU(textureData, renderer);
    std::free(textureData.pixels);
    textureData.pixels = nullptr;

    return uploaded;
}

bool TextureLoader::uploadToGPU(TextureData& textureData, IRenderAdapter* renderer) {
    if (renderer == nullptr) {
        return false;
    }

    const bool uploaded = renderer->createTexture(
        textureData.width,
        textureData.height,
        textureData.channels,
        textureData.pixels,
        textureData.textureId);

    if (uploaded) {
        LOG_INFO("Texture uploaded to GPU: ID=" + std::to_string(textureData.textureId));
    }

    return uploaded;
}
