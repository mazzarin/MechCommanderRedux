#pragma once

#include "platform/MCMemoryLedger.h"
#include "platform/MCSdlHandle.h"

#include <memory>

// Port-only (SPEC-memory.md's "The ledger"): the GPU's textures and buffers counted under their holder from their
// making to their release, so none is missed and none is counted after it's gone. A texture's bytes are its format's
// blocks over its size and levels (SDL_CalculateGPUTextureFormatSize); the driver's own padding and alignment aren't
// known, and are the difference from the system's figure the ledger names. One let go into a frame's retired list (held
// until that frame has run) counts as retired, not freed, until its last holder lets go. SDL frees the object itself once
// the GPU is done with it, a frame or two later, which isn't seen.

/// <summary>Releases a GPU object as MCGpuRelease does, taking its bytes out of the ledger.</summary>
struct MCCountedGpuRelease
{
    MCGpuRelease Release;
    MCMemoryLedger::MCHolder Holder = MCMemoryLedger::MCHolder::GpuOther;
    /// <summary>The object's bytes, and those of them counted as payload.</summary>
    int64_t Bytes = 0;
    int64_t Payload = 0;
    /// <summary>Whether its bytes count as retired (<see cref="MCGpuCounted::Retire"/>).</summary>
    bool Retired = false;

    void operator()(SDL_GPUTexture* texture) const;
    void operator()(SDL_GPUBuffer* buffer) const;
    void operator()(SDL_GPUTransferBuffer* buffer) const;

    /// <summary>Its bytes move from allocated to retired (once).</summary>
    void Retire();

private:
    /// <summary>Its bytes out of the ledger, as it's released.</summary>
    void Uncount() const;
};

/// <summary>A GPU object owned alone and counted in the ledger, released to its device with the handle.</summary>
template <typename T> using MCCountedGpuHandle = std::unique_ptr<T, MCCountedGpuRelease>;

namespace MCGpuCounted
{
    /// <summary>The bytes a texture made as <paramref name="info"/> says takes, every level's.</summary>
    uint64_t TextureBytes(const SDL_GPUTextureCreateInfo& info);

    /// <summary>
    /// A texture <paramref name="device"/> makes as <paramref name="info"/> says, counted under
    /// <paramref name="holder"/>; null where SDL_CreateGPUTexture fails (SDL_GetError says why).
    /// </summary>
    MCCountedGpuHandle<SDL_GPUTexture> MakeTexture(const std::shared_ptr<SDL_GPUDevice>& device,
                                                   const SDL_GPUTextureCreateInfo& info,
                                                   MCMemoryLedger::MCHolder holder);

    /// <summary>A buffer, as <see cref="MakeTexture"/>.</summary>
    MCCountedGpuHandle<SDL_GPUBuffer> MakeBuffer(const std::shared_ptr<SDL_GPUDevice>& device,
                                                 const SDL_GPUBufferCreateInfo& info, MCMemoryLedger::MCHolder holder);

    /// <summary>A transfer buffer, as <see cref="MakeTexture"/>.</summary>
    MCCountedGpuHandle<SDL_GPUTransferBuffer> MakeTransferBuffer(const std::shared_ptr<SDL_GPUDevice>& device,
                                                                 const SDL_GPUTransferBufferCreateInfo& info,
                                                                 MCMemoryLedger::MCHolder holder);

    /// <summary>
    /// Its owner lets go of <paramref name="handle"/> into a frame's retired list: its bytes count as retired until its
    /// last holder lets go. Only for an object the caller owns alone (a shared page's texture is its page's, not its
    /// pictures'); nothing for one not counted.
    /// </summary>
    template <typename T> void Retire(const std::shared_ptr<T>& handle)
    {
        if (MCCountedGpuRelease* release = std::get_deleter<MCCountedGpuRelease>(handle); release != nullptr)
        {
            release->Retire();
        }
    }
}
