#include "stdafx.h"
#include "platform/MCGpuCounted.h"

namespace
{
    /// <summary>The release for an object of <paramref name="bytes"/> made for <paramref name="holder"/>, counted now.</summary>
    MCCountedGpuRelease Counted(const std::shared_ptr<SDL_GPUDevice>& device, MCMemoryLedger::MCHolder holder,
                                uint64_t bytes)
    {
        const auto counted = static_cast<int64_t>(bytes);
        const int64_t payload = MCMemoryLedger::PayloadApart(holder) ? 0 : counted;
        MCMemoryLedger::Add(holder, {.Payload = payload, .Allocated = counted});
        return MCCountedGpuRelease{
            .Release = MCGpuRelease{device}, .Holder = holder, .Bytes = counted, .Payload = payload};
    }
}

void MCCountedGpuRelease::operator()(SDL_GPUTexture* texture) const
{
    Release(texture);
    Uncount();
}

void MCCountedGpuRelease::operator()(SDL_GPUBuffer* buffer) const
{
    Release(buffer);
    Uncount();
}

void MCCountedGpuRelease::operator()(SDL_GPUTransferBuffer* buffer) const
{
    Release(buffer);
    Uncount();
}

void MCCountedGpuRelease::Retire()
{
    if (!Retired)
    {
        Retired = true;
        MCMemoryLedger::Add(Holder, {.Payload = -Payload, .Allocated = -Bytes, .Retired = Bytes});
    }
}

void MCCountedGpuRelease::Uncount() const
{
    MCMemoryLedger::Add(Holder, Retired ? MCMemoryLedger::MCFigures{.Retired = -Bytes}
                                        : MCMemoryLedger::MCFigures{.Payload = -Payload, .Allocated = -Bytes});
}

uint64_t MCGpuCounted::TextureBytes(const SDL_GPUTextureCreateInfo& info)
{
    uint64_t bytes = 0;

    for (uint32_t level = 0; level < std::max(info.num_levels, 1u); level++)
    {
        bytes += SDL_CalculateGPUTextureFormatSize(info.format, std::max(info.width >> level, 1u),
                                                   std::max(info.height >> level, 1u),
                                                   std::max(info.layer_count_or_depth, 1u));
    }

    return bytes;
}

MCCountedGpuHandle<SDL_GPUTexture> MCGpuCounted::MakeTexture(const std::shared_ptr<SDL_GPUDevice>& device,
                                                             const SDL_GPUTextureCreateInfo& info,
                                                             MCMemoryLedger::MCHolder holder)
{
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device.get(), &info);
    return texture != nullptr ? MCCountedGpuHandle<SDL_GPUTexture>(texture, Counted(device, holder, TextureBytes(info)))
                              : nullptr;
}

MCCountedGpuHandle<SDL_GPUBuffer> MCGpuCounted::MakeBuffer(const std::shared_ptr<SDL_GPUDevice>& device,
                                                           const SDL_GPUBufferCreateInfo& info,
                                                           MCMemoryLedger::MCHolder holder)
{
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device.get(), &info);
    return buffer != nullptr ? MCCountedGpuHandle<SDL_GPUBuffer>(buffer, Counted(device, holder, info.size)) : nullptr;
}

MCCountedGpuHandle<SDL_GPUTransferBuffer> MCGpuCounted::MakeTransferBuffer(const std::shared_ptr<SDL_GPUDevice>& device,
                                                                           const SDL_GPUTransferBufferCreateInfo& info,
                                                                           MCMemoryLedger::MCHolder holder)
{
    SDL_GPUTransferBuffer* buffer = SDL_CreateGPUTransferBuffer(device.get(), &info);
    return buffer != nullptr ? MCCountedGpuHandle<SDL_GPUTransferBuffer>(buffer, Counted(device, holder, info.size))
                             : nullptr;
}
