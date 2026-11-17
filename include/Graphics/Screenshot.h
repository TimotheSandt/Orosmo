#pragma once

#include <cstdint>
#include <string>

class Texture;
class RenderTarget;

// Image capture (TOOL-03). Reads a colour attachment back through the backend-neutral
// ITextureResource::Readback and writes a PNG (RGBA8, origin top-left). Used by the reference-image
// workflow: `ProceduralGeneration --capture <dir>`.
namespace Screenshot
{
// Returns false (and logs why) when the texture is not RGBA8, the readback is empty or the file cannot be written.
bool SavePng(const Texture &texture, const std::string &path);
bool SavePng(const RenderTarget &target, std::uint32_t colorAttachment, const std::string &path);
} // namespace Screenshot
