#include "stdafx.h"
#include "logistics/MCLogPort.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "main/MCGamePaths.h"

namespace
{
    /// <summary>The art <see cref="LogArt"/> loaded, by file name.</summary>
    std::unordered_map<std::string, std::shared_ptr<MCLogPort>> LoadedArt;
}

auto LogArt(std::string_view fileName) -> std::shared_ptr<MCLogPort>
{
    const std::string name(fileName);

    if (const auto found = LoadedArt.find(name); found != LoadedArt.end())
    {
        return found->second;
    }

    auto art = std::make_shared<MCLogPort>();
    art->Load(name);
    return LoadedArt.emplace(name, std::move(art)).first->second;
}

auto LogScreenArt(std::string_view name) -> std::shared_ptr<MCLogPort>
{
    return LogArt(std::format("{}logart\\{}", ArtPath, name));
}

auto ClearLogArt() -> void
{
    LoadedArt.clear();
}

auto MCLogPort::Init(int32_t width, int32_t height) -> int32_t
{
    return MakeBitmap(width, height);
}

auto MCLogPort::Load(std::string_view fileName) -> void
{
    MCFile file;

    if (file.Open(std::format("{}{}", ArtPath, fileName)) != 0 && file.Open(fileName) != 0)
    {
        GeneralMsg(std::format("Error reading '{}'", fileName));
    }

    const uint32_t size = file.FileSize();

    if (size == 0)
    {
        GeneralMsg(std::format("Error reading '{}'", fileName));
    }

    std::vector<uint8_t> data(size);
    file.Read(data);
    file.Close();

    // A TGA header: the 16-bit width and height at +0x0c/+0x0e; the 8-bit pixels follow the 18-byte header and a
    // 256-entry palette.
    int16_t tgaWidth = 0;
    int16_t tgaHeight = 0;
    std::memcpy(&tgaWidth, data.data() + 0x0c, sizeof(tgaWidth));
    std::memcpy(&tgaHeight, data.data() + 0x0e, sizeof(tgaHeight));
    Init(tgaWidth, tgaHeight);
    const std::shared_ptr<MCTexture> texture = Bitmap().Texture.lock();
    std::memcpy(MCRenderer::LockTexture(*texture).data(), data.data() + 0x312,
                static_cast<size_t>(tgaHeight * tgaWidth));
    MCRenderer::UnlockTexture(*texture);
}
