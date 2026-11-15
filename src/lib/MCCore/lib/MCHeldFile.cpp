#include "stdafx.h"
#include "lib/MCHeldFile.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <stop_token>
#include <thread>

namespace
{
    /// <summary>
    /// The readers every held file shares: the pieces waiting, in the order held, and up to MCHeldFile::ReadsAtOnce
    /// threads taking them, started as pieces wait and kept until the game ends.
    /// </summary>
    class MCHeldReaders
    {
    public:
        static MCHeldReaders& Get()
        {
            static MCHeldReaders readers;
            return readers;
        }

        /// <summary>Queues <paramref name="pieces"/> pieces of <paramref name="file"/>, behind those waiting.</summary>
        void Queue(const std::shared_ptr<MCHeldFile>& file, size_t pieces)
        {
            {
                const std::scoped_lock held(_Lock);

                for (size_t piece = 0; piece < pieces; ++piece)
                {
                    _Waiting.emplace_back(file, piece);
                }

                _Files.push_back(file);
                std::erase_if(_Files, [](const std::weak_ptr<MCHeldFile>& live) { return live.expired(); });

                while (_Threads.size() < MCHeldFile::ReadsAtOnce && _Threads.size() < _Waiting.size())
                {
                    _Threads.emplace_back([this](std::stop_token stop) { Take(stop); });
                }
            }

            _Queued.notify_all();
        }

        /// <summary>The files held now, in the order held.</summary>
        std::vector<std::shared_ptr<MCHeldFile>> Files()
        {
            const std::scoped_lock held(_Lock);
            std::vector<std::shared_ptr<MCHeldFile>> files;

            for (const std::weak_ptr<MCHeldFile>& file : _Files)
            {
                if (std::shared_ptr<MCHeldFile> live = file.lock(); live != nullptr)
                {
                    files.push_back(std::move(live));
                }
            }

            return files;
        }

    private:
        MCHeldReaders() = default;

        /// <summary>A reader: each waiting piece in turn, a let-go file's skipped, until the game ends.</summary>
        void Take(const std::stop_token& stop)
        {
            for (;;)
            {
                std::shared_ptr<MCHeldFile> file;
                size_t piece = 0;
                {
                    std::unique_lock held(_Lock);

                    // Woken by a stop too (the readers' threads stop as the game ends).
                    if (!_Queued.wait(held, stop, [&] { return !_Waiting.empty(); }))
                    {
                        return;
                    }

                    file = _Waiting.front().first.lock();
                    piece = _Waiting.front().second;
                    _Waiting.pop_front();
                }

                if (file != nullptr)
                {
                    file->ReadPiece(piece, false);
                }
            }
        }

        std::mutex _Lock;
        std::condition_variable_any _Queued;
        std::deque<std::pair<std::weak_ptr<MCHeldFile>, size_t>> _Waiting;
        std::vector<std::weak_ptr<MCHeldFile>> _Files;
        /// <summary>Last, so they stop (asked to, and joined) before the rest goes.</summary>
        std::vector<std::jthread> _Threads;
    };

    /// <summary>The pieces of a file of <paramref name="length"/> bytes.</summary>
    size_t PiecesOf(uint32_t length)
    {
        return (size_t{length} + MCHeldFile::PieceBytes - 1) / MCHeldFile::PieceBytes;
    }
}

std::shared_ptr<MCHeldFile> MCHeldFile::Read(std::filesystem::path path, uint32_t length, std::string name)
{
    auto file = std::make_shared<MCHeldFile>(Key(), std::move(path), length, std::move(name));
    MCHeldReaders::Get().Queue(file, file->_Pieces.size());
    return file;
}

MCHeldFile::MCHeldFile(Key, std::filesystem::path path, uint32_t length, std::string name)
    : _Path(std::move(path))
    , _Length(length)
    , _Name(std::move(name))
    , _Held(std::chrono::steady_clock::now())
    , _Bytes(length)
    , _Pieces(PiecesOf(length), MCPieceState::Waiting)
{
    if (_Pieces.empty())
    {
        _AllRead = _Held;
    }
}

void MCHeldFile::ReadPiece(size_t piece, bool asked)
{
    {
        const std::scoped_lock held(_Lock);

        if (piece >= _Pieces.size() || _Pieces[piece] != MCPieceState::Waiting)
        {
            return;
        }

        // A failed hold's other pieces left unread: its holders read the disk.
        if (_Failed)
        {
            PieceDone(piece);
            return;
        }

        _Pieces[piece] = MCPieceState::Reading;
        _PiecesAsked += asked ? 1 : 0;
    }

    const size_t start = piece * PieceBytes;
    const size_t size = std::min(PieceBytes, size_t{_Length} - start);
    // The file opened again for each piece, as long as it was held: another file by that name reads as a failure.
    std::optional<MCDiskFile> disk = MCDiskFile::Open(_Path, MCDiskFileMode::Read, MCDiskFileReads::Direct);
    const bool read = disk.has_value() && disk->Length() == _Length &&
                      disk->ReadAt(static_cast<uint32_t>(start), std::span(_Bytes).subspan(start, size)) == size;
    {
        const std::scoped_lock held(_Lock);
        _Failed = _Failed || !read;
        PieceDone(piece);
    }

    _PieceDone.notify_all();
}

void MCHeldFile::PieceDone(size_t piece)
{
    _Pieces[piece] = MCPieceState::Done;

    if (++_PiecesDone == _Pieces.size())
    {
        _AllRead = std::chrono::steady_clock::now();
    }
}

std::optional<size_t> MCHeldFile::ReadAt(uint32_t offset, std::span<uint8_t> buffer)
{
    if (offset >= _Length || buffer.empty())
    {
        return 0;
    }

    const size_t count = std::min(buffer.size(), size_t{_Length} - offset);
    const size_t first = offset / PieceBytes;
    const size_t last = (offset + count - 1) / PieceBytes;

    for (size_t piece = first; piece <= last; ++piece)
    {
        ReadPiece(piece, true);
    }

    std::unique_lock held(_Lock);
    _PieceDone.wait(held,
                    [&]
                    {
                        return _Failed || std::all_of(_Pieces.begin() + static_cast<ptrdiff_t>(first),
                                                      _Pieces.begin() + static_cast<ptrdiff_t>(last + 1),
                                                      [](MCPieceState state) { return state == MCPieceState::Done; });
                    });

    if (_Failed)
    {
        return std::nullopt;
    }

    ++_Served;
    held.unlock();
    std::memcpy(buffer.data(), _Bytes.data() + offset, count);
    return count;
}

auto MCHeldFile::Stats() const -> MCHeldStats
{
    const std::scoped_lock held(_Lock);
    const bool done = _PiecesDone == _Pieces.size();
    return {.Name = _Name,
            .Bytes = _Length,
            .Served = _Served,
            .Pieces = _Pieces.size(),
            .PiecesAsked = _PiecesAsked,
            .Done = done,
            .ReadMs = done ? std::chrono::duration<double, std::milli>(_AllRead - _Held).count() : 0.0,
            .Failed = _Failed};
}

auto MCHeldFile::Live() -> std::vector<MCHeldStats>
{
    std::vector<MCHeldStats> stats;

    for (const std::shared_ptr<MCHeldFile>& file : MCHeldReaders::Get().Files())
    {
        stats.push_back(file->Stats());
    }

    return stats;
}
