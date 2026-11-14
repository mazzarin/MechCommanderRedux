#include "stdafx.h"
#include "logistics/MCLogPort.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "main/MCGamePaths.h"
#include "sprite/MCHdArt.h"
#include "platform/MCFrameLog.h"

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
    if (const std::shared_ptr<MCTexture> probeTexture = art->Bitmap().Texture.lock(); probeTexture != nullptr)
    {
        MCFrameLog::Note(std::format("probe: art {} hash {:016x}", name, MCHdArt::PictureHash(probeTexture->Pixels, probeTexture->Width, probeTexture->Height)));
    }
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

auto LogArtLoaded() -> size_t
{
    return LoadedArt.size();
}

auto MCLogPort::Init(int32_t width, int32_t height) -> int32_t
{
    return MakeBitmap(width, height);
}

auto MCLogPort::Load(std::string_view fileName) -> void
{
    const MCProbeIo::Timer probeTimer(MCProbeIo::TgaNs, MCProbeIo::Tgas);
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
    const MCLogTga tga = ReadLogTga(data);
    Init(tga.Width, tga.Height);
    const std::shared_ptr<MCTexture> texture = Bitmap().Texture.lock();

    // Port fix: a file too short for its pixels leaves the picture blank (the original copied from past its end).
    if (texture != nullptr && !tga.Pixels.empty())
    {
        std::ranges::copy(tga.Pixels, MCRenderer::LockTexture(*texture).begin());
        MCRenderer::UnlockTexture(*texture);
    }
}

auto ReadLogTga(std::span<const uint8_t> data) -> MCLogTga
{
    constexpr size_t WidthAt = 0x0c;
    constexpr size_t HeightAt = 0x0e;
    constexpr size_t PixelsAt = 0x312;
    MCLogTga tga;

    if (data.size() < HeightAt + sizeof(tga.Height))
    {
        return tga;
    }

    std::memcpy(&tga.Width, data.data() + WidthAt, sizeof(tga.Width));
    std::memcpy(&tga.Height, data.data() + HeightAt, sizeof(tga.Height));
    const size_t pixels = static_cast<size_t>(std::max<int16_t>(tga.Width, 0)) * std::max<int16_t>(tga.Height, 0);

    if (data.size() >= PixelsAt + pixels)
    {
        tga.Pixels = data.subspan(PixelsAt, pixels);
    }

    return tga;
}
