#include "stdafx.h"
#include "lib/MCFilePatch.h"
#include "platform/MCFileSystem.h"

// HDiffPatch's patcher is built single-threaded (MCCore.vcxproj, src/mac/CMakeLists.txt); its headers are read the same.
#define _IS_USED_MULTITHREAD 0
#include "libHDiffPatch/HPatch/patch.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

namespace
{
    /// <summary>The checksum a patch must carry, as hdiffz names it, and its size in bytes.</summary>
    constexpr const char* ChecksumType = "xxh128";
    constexpr size_t ChecksumBytes = 16;
    constexpr size_t HalfChecksumBytes = ChecksumBytes / 2;
    constexpr int BitsPerByte = 8;
    /// <summary>The read and write cache beside a patch's window and step (hpatchz's default is 8 MB).</summary>
    constexpr size_t IoCacheBytes = size_t{1} << 20;
    /// <summary>The most memory a patch's header may ask for, its window and its step each: more is refused.</summary>
    constexpr uint64_t MaxPatchMemory = uint64_t{256} << 20;
    /// <summary>The patch is applied on this thread alone (patch_window_diff's thread count).</summary>
    constexpr size_t SingleThread = 1;

    std::string Shown(const std::filesystem::path& path)
    {
        return MCFileSystem::ToUtf8(path);
    }

    /// <summary>A file HDiffPatch reads at any position (the old file, the patch).</summary>
    struct InputFile
    {
        InputFile() = default;
        InputFile(const InputFile&) = delete;
        InputFile& operator=(const InputFile&) = delete;

        hpatch_TStreamInput Stream{};
        std::ifstream File;
        /// <summary>Where the next read starts unless it seeks (none: unknown). A seek drops the stream's buffer.</summary>
        std::optional<hpatch_StreamPos_t> At;
    };

    /// <summary>The file HDiffPatch writes (the new file), which it may read back.</summary>
    struct OutputFile
    {
        OutputFile() = default;
        OutputFile(const OutputFile&) = delete;
        OutputFile& operator=(const OutputFile&) = delete;

        hpatch_TStreamOutput Stream{};
        std::fstream File;
        bool WriteFailed = false;
        /// <summary>Asked before each write (none: always on); once it says to stop, every write fails.</summary>
        const std::function<bool()>* GoOn = nullptr;
        bool Stopped = false;
        /// <summary>
        /// Where the file stands after the last read or write, and whether it was a write (none: unknown): a read after
        /// a write, or a write after a read, seeks first, as a file stream needs.
        /// </summary>
        std::optional<hpatch_StreamPos_t> At;
        bool LastWrote = false;
    };

    hpatch_BOOL ReadInput(const hpatch_TStreamInput* stream, hpatch_StreamPos_t from, unsigned char* data,
                          unsigned char* end)
    {
        auto* const input = static_cast<InputFile*>(stream->streamImport);
        const auto count = static_cast<std::streamsize>(end - data);
        input->File.clear();

        if (input->At != from)
        {
            input->File.seekg(static_cast<std::streamoff>(from));
        }

        input->File.read(reinterpret_cast<char*>(data), count);

        if (input->File.gcount() != count)
        {
            input->At.reset();
            return hpatch_FALSE;
        }

        input->At = from + static_cast<hpatch_StreamPos_t>(count);
        return hpatch_TRUE;
    }

    hpatch_BOOL ReadWritten(const hpatch_TStreamOutput* stream, hpatch_StreamPos_t from, unsigned char* data,
                            unsigned char* end)
    {
        auto* const output = static_cast<OutputFile*>(stream->streamImport);
        const auto count = static_cast<std::streamsize>(end - data);
        output->File.clear();

        if (output->LastWrote || output->At != from)
        {
            output->File.seekg(static_cast<std::streamoff>(from));
        }

        output->File.read(reinterpret_cast<char*>(data), count);
        output->LastWrote = false;

        if (output->File.gcount() != count)
        {
            output->At.reset();
            return hpatch_FALSE;
        }

        output->At = from + static_cast<hpatch_StreamPos_t>(count);
        return hpatch_TRUE;
    }

    hpatch_BOOL WriteOutput(const hpatch_TStreamOutput* stream, hpatch_StreamPos_t to, const unsigned char* data,
                            const unsigned char* end)
    {
        auto* const output = static_cast<OutputFile*>(stream->streamImport);
        const auto count = static_cast<std::streamsize>(end - data);

        if (output->Stopped || (output->GoOn != nullptr && *output->GoOn && !(*output->GoOn)()))
        {
            output->Stopped = true;
            return hpatch_FALSE;
        }

        output->File.clear();

        if (!output->LastWrote || output->At != to)
        {
            output->File.seekp(static_cast<std::streamoff>(to));
        }

        output->File.write(reinterpret_cast<const char*>(data), count);
        output->LastWrote = true;

        if (!output->File.good())
        {
            output->At.reset();
            output->WriteFailed = true;
            return hpatch_FALSE;
        }

        output->At = to + static_cast<hpatch_StreamPos_t>(count);
        return hpatch_TRUE;
    }

    /// <summary>Opens <paramref name="path"/> for HDiffPatch to read, or says why it can't be.</summary>
    std::expected<void, std::string> Open(InputFile& input, const std::filesystem::path& path)
    {
        std::error_code error;
        const uintmax_t size = std::filesystem::file_size(path, error);

        if (error)
        {
            return std::unexpected(std::format("{}: {}", Shown(path), error.message()));
        }

        input.File.open(path, std::ios::binary);

        if (!input.File.is_open())
        {
            return std::unexpected(std::format("{} can't be read", Shown(path)));
        }

        input.Stream.streamImport = &input;
        input.Stream.streamSize = size;
        input.Stream.read = ReadInput;
        return {};
    }

    // The xxh128 checksum plugin, as hdiffz's own (checksum_plugin_demo.h): XXH3's 128 bits, the low half first, each
    // little-endian.
    const char* ChecksumName()
    {
        return ChecksumType;
    }

    hpatch_size_t ChecksumSize()
    {
        return ChecksumBytes;
    }

    hpatch_checksumHandle OpenChecksum(hpatch_TChecksum*)
    {
        return XXH3_createState();
    }

    void CloseChecksum(hpatch_TChecksum*, hpatch_checksumHandle handle)
    {
        XXH3_freeState(static_cast<XXH3_state_t*>(handle));
    }

    void BeginChecksum(hpatch_checksumHandle handle)
    {
        XXH3_128bits_reset(static_cast<XXH3_state_t*>(handle));
    }

    void AppendChecksum(hpatch_checksumHandle handle, const unsigned char* data, const unsigned char* end)
    {
        XXH3_128bits_update(static_cast<XXH3_state_t*>(handle), data, static_cast<size_t>(end - data));
    }

    void EndChecksum(hpatch_checksumHandle handle, unsigned char* checksum, unsigned char*)
    {
        const XXH128_hash_t hash = XXH3_128bits_digest(static_cast<XXH3_state_t*>(handle));

        for (size_t i = 0; i < HalfChecksumBytes; ++i)
        {
            checksum[i] = static_cast<unsigned char>(hash.low64 >> (i * BitsPerByte));
            checksum[i + HalfChecksumBytes] = static_cast<unsigned char>(hash.high64 >> (i * BitsPerByte));
        }
    }

    hpatch_TChecksum Xxh128Checksum = {ChecksumName,  ChecksumSize,   OpenChecksum, CloseChecksum,
                                       BeginChecksum, AppendChecksum, EndChecksum};

    /// <summary>What a patch being applied keeps: its cache, and why it was refused.</summary>
    struct Patching
    {
        std::vector<unsigned char> Cache;
        std::string Refused;
    };

    hpatch_BOOL OnDiffInfo(winpatch_listener_t* listener, const hpatch_windowDiffInfo* info,
                           hpatch_TDecompress** decompress, hpatch_TChecksum** checksum, hpatch_BOOL* checkNew,
                           hpatch_BOOL* checkOld, hpatch_BOOL* checkDiff, unsigned char** cache,
                           unsigned char** cacheEnd)
    {
        auto* const patching = static_cast<Patching*>(listener->import);
        *decompress = nullptr;
        *checksum = &Xxh128Checksum;
        *checkNew = hpatch_TRUE;
        *checkOld = hpatch_TRUE;
        *checkDiff = hpatch_TRUE;

        // From the patch's header: its window on the old file and its step's memory.
        if (info->maxWindowOldSize > MaxPatchMemory || info->maxStepMemSize > MaxPatchMemory)
        {
            patching->Refused =
                std::format("asks for {} and {} bytes of memory", info->maxWindowOldSize, info->maxStepMemSize);
            return hpatch_FALSE;
        }

        patching->Cache.resize(static_cast<size_t>(info->maxWindowOldSize + info->maxStepMemSize) + IoCacheBytes);
        *cache = patching->Cache.data();
        *cacheEnd = patching->Cache.data() + patching->Cache.size();
        return hpatch_TRUE;
    }

    /// <summary>What a failed patch_window_diff means.</summary>
    std::string Failure(TWindowPatchResult result, const Patching& patching, const std::filesystem::path& old,
                        const std::filesystem::path& patch)
    {
        switch (result)
        {
            case kWindowPatch_checksum_old_error:
                return std::format("{} is for another file than {} (its checksum differs)", Shown(patch), Shown(old));
            case kWindowPatch_checksum_diff_error:
                return std::format("{} is damaged (its checksum differs)", Shown(patch));
            case kWindowPatch_checksum_new_error:
                return std::format("{} made a file whose checksum differs from the one it names", Shown(patch));
            case kWindowPatch_old_size_error:
                return std::format("{} is for a file of another size than {}", Shown(patch), Shown(old));
            default:
                return patching.Refused.empty() ? std::format("{} can't be applied (HDiffPatch's error {})",
                                                              Shown(patch), static_cast<int>(result))
                                                : std::format("{} {}", Shown(patch), patching.Refused);
        }
    }

    /// <summary>Applies the patch to <paramref name="out"/>, which the caller removes when it fails.</summary>
    std::expected<void, std::string> Write(InputFile& old, InputFile& patch, const hpatch_windowDiffInfo& info,
                                           const std::filesystem::path& oldPath, const std::filesystem::path& patchPath,
                                           const std::filesystem::path& out, const std::function<bool()>& goOn)
    {
        OutputFile output;
        output.GoOn = &goOn;
        output.File.open(out, std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary);

        if (!output.File.is_open())
        {
            return std::unexpected(std::format("{} can't be written", Shown(out)));
        }

        output.Stream.streamImport = &output;
        output.Stream.streamSize = info.newDataSize;
        output.Stream.read_writed = ReadWritten;
        output.Stream.write = WriteOutput;

        Patching patching;
        winpatch_listener_t listener{};
        listener.import = &patching;
        listener.onDiffInfo = OnDiffInfo;
        const TWindowPatchResult result =
            patch_window_diff(&listener, &output.Stream, &old.Stream, &patch.Stream, 0, SingleThread);

        if (output.Stopped)
        {
            return std::unexpected(std::string(MCFilePatch::StoppedError));
        }

        if (output.WriteFailed)
        {
            return std::unexpected(std::format("{} can't be written", Shown(out)));
        }

        if (result != kWindowPatch_ok)
        {
            return std::unexpected(Failure(result, patching, oldPath, patchPath));
        }

        output.File.flush();

        if (!output.File.good())
        {
            return std::unexpected(std::format("{} can't be written", Shown(out)));
        }

        return {};
    }

    /// <summary>
    /// Checks the patch is one this code applies, for <paramref name="old"/>, then applies it to <paramref name="out"/>,
    /// which the caller removes when it fails.
    /// </summary>
    std::expected<void, std::string> Patched(const std::filesystem::path& old, const std::filesystem::path& patch,
                                             const std::filesystem::path& out, const std::function<bool()>& goOn)
    {
        InputFile oldFile;
        InputFile patchFile;

        if (auto opened = Open(oldFile, old); !opened)
        {
            return opened;
        }

        if (auto opened = Open(patchFile, patch); !opened)
        {
            return opened;
        }

        hpatch_windowDiffInfo info{};

        if (!getWindowDiffInfo(&info, &patchFile.Stream, 0))
        {
            return std::unexpected(
                std::format("{} isn't a patch this game reads (HDiffPatch's window diff)", Shown(patch)));
        }

        if (info.compressedSize != 0 || info.compressType[0] != '\0')
        {
            return std::unexpected(
                std::format("{} is compressed ({}), which this game doesn't read", Shown(patch), info.compressType));
        }

        if (std::string_view(info.checksumType) != ChecksumType || info.checksumByteSize != ChecksumBytes)
        {
            return std::unexpected(std::format("{} has no {} checksums", Shown(patch), ChecksumType));
        }

        if (info.oldDataSize != oldFile.Stream.streamSize)
        {
            return std::unexpected(std::format("{} is for a file of {} bytes, not {}'s {}", Shown(patch),
                                               info.oldDataSize, Shown(old), oldFile.Stream.streamSize));
        }

        return Write(oldFile, patchFile, info, old, patch, out, goOn);
    }
}

std::expected<void, std::string> MCFilePatch::Apply(const std::filesystem::path& old,
                                                    const std::filesystem::path& patch,
                                                    const std::filesystem::path& out, const std::function<bool()>& goOn)
{
    // Refused before out is touched: it's a file this patch reads.
    for (const std::filesystem::path& source : {old, patch})
    {
        std::error_code error;

        if (std::filesystem::equivalent(out, source, error))
        {
            return std::unexpected(std::format("{} would be written over while it's read", Shown(out)));
        }
    }

    auto patched = Patched(old, patch, out, goOn);

    if (!patched)
    {
        // Whatever stopped it, nothing is left at out (not even a file an earlier attempt left there).
        std::error_code error;
        std::filesystem::remove(out, error);
    }

    return patched;
}
