#include "stdafx.h"
#include "lib/MCFile.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFilesAhead.h"
#include "lib/MCPacketFile.h"
#include "main/MCGameContext.h"
#include "platform/MCFileSystem.h"

bool FileExists(std::string_view fileName)
{
    return MCFileSystem::Exists(fileName);
}

std::string GamePath(std::string_view directory, std::string_view name, std::string_view extension)
{
    std::string path;
    path.reserve(directory.size() + name.size() + extension.size());
    path += directory;
    path += name;
    path += extension;
    return path;
}

MCFile::MCFile() = default;

MCFile::~MCFile()
{
    Close();
}

bool MCFile::Eof()
{
    return GetLength() <= _LogicalPosition;
}

std::shared_ptr<MCFile::OpenState> MCFile::DiskState() const
{
    if (!_IsChild)
    {
        return _State != nullptr && _State->Disk.has_value() ? _State : nullptr;
    }

    for (std::shared_ptr<OpenState> state = _ParentState.lock(); state != nullptr; state = state->Parent.lock())
    {
        if (state->Disk.has_value())
        {
            return state;
        }
    }

    return nullptr;
}

bool MCFile::Orphaned() const
{
    return _IsChild && DiskState() == nullptr;
}

void MCFile::CloseIfOrphaned()
{
    if (Orphaned())
    {
        Close();
    }
}

int32_t MCFile::Open(std::string_view fileName, MCFileMode mode)
{
    _FileName = fileName;
    _FileMode = mode;

    // Read whole into memory: a file like this takes no children.
    const auto inRam = [this](std::vector<uint8_t>&& image)
    {
        _FileImage = std::move(image);
        _InRam = true;
        _Length = static_cast<uint32_t>(_FileImage.size());
        _LogicalPosition = 0;
        return NO_ERR;
    };

    // Port-only: a file read ahead (lib/MCFilesAhead.h) was looked for already, as below: an archive's entry, or none.
    if (mode == MCFileMode::Read)
    {
        if (std::optional<MCFilesAhead::MCAheadFile> ahead = MCFilesAhead::Take(_FileName); ahead.has_value())
        {
            MCFilesAhead::Looked(_FileName);
            _State.reset();

            if (!ahead->Found)
            {
                _FileName.clear();
                return FILE_NOT_FOUND;
            }

            return inRam(std::move(ahead->Bytes));
        }
    }

    // Port-only: a file the game made for itself (MCFileSystem::PutImage) reads as a FastFile entry does, whatever
    // the folders hold.
    std::optional<std::vector<uint8_t>> put =
        mode == MCFileMode::Read ? MCFileSystem::PutImageAt(_FileName) : std::nullopt;

    // Port: files opened to write live in the user folder.
    std::optional<MCDiskFile> disk =
        put.has_value() ? std::nullopt
                        : MCDiskFile::Open(mode == MCFileMode::Read ? MCFileSystem::Resolve(_FileName)
                                                                    : MCFileSystem::ResolveWrite(_FileName),
                                           mode == MCFileMode::Create ? MCDiskFileMode::Create : MCDiskFileMode::Read);
    _State.reset();

    if (!disk.has_value())
    {
        if (mode == MCFileMode::Create)
        {
            return FILE_NOT_FOUND;
        }

        // Port: a file the context's source holds in memory (a test's) reads as a FastFile entry does.
        if (put.has_value())
        {
            return inRam(std::move(*put));
        }

        if (const auto image = MCFileSystem::FindImage(_FileName); image.has_value())
        {
            return inRam(std::vector<uint8_t>(image->begin(), image->end()));
        }

        // Port-only: a file looked for in the archives, found or not, may be read ahead next time (lib/MCFilesAhead.h).
        if (mode == MCFileMode::Read)
        {
            MCFilesAhead::Looked(_FileName);
        }

        if (auto entry = MCGameContext::Current().FastFiles().Read(_FileName); entry.has_value())
        {
            return inRam(std::move(*entry));
        }

        _FileName.clear();
        return FILE_NOT_FOUND;
    }

    _LogicalPosition = 0;
    _Length = 0;
    _IsChild = false;
    _ParentState.reset();
    _State = std::make_shared<OpenState>(OpenState{.Disk = std::move(disk)});
    _Length = GetLength();
    _ParentOffset = 0;
    _PhysicalLength = _Length;
    return NO_ERR;
}

int32_t MCFile::Open(MCFile& parent, uint32_t length)
{
    return OpenChild(parent, length, false);
}

int32_t MCFile::OpenChild(MCFile& parent, uint32_t length, bool readWhole)
{
    _IsChild = true;
    _ParentState = parent._State;

    if (parent._FileMode != MCFileMode::Read)
    {
        return CANT_WRITE_TO_CHILD;
    }

    _PhysicalLength = length;
    _ParentOffset = parent._LogicalPosition;
    _LogicalPosition = 0;
    _FileName = parent.GetFilename();
    _FileMode = parent._FileMode;

    if (parent._State == nullptr || parent.Orphaned())
    {
        return TOO_MANY_CHILDREN;
    }

    if (readWhole)
    {
        _State.reset();
        _InRam = true;
        _FileImage.assign(length, 0);

        if (parent.GetFileClass() == MCFileClass::Packet)
        {
            auto& packets = static_cast<MCPacketFile&>(parent);
            packets.ReadPacket(packets.GetCurrentPacket(), std::span(_FileImage));
            return NO_ERR;
        }

        parent.ReadRawAt(_ParentOffset, _FileImage);
        return NO_ERR;
    }

    _State = std::make_shared<OpenState>(OpenState{.Parent = parent._State});
    return NO_ERR;
}

int32_t MCFile::Create(std::string_view fileName)
{
    return Open(fileName, MCFileMode::Create);
}

void MCFile::Close()
{
    _FileName.clear();
    _Length = 0;
    // The children lose this state, so they read as closed from now on.
    _State.reset();
    _IsChild = false;
    _ParentState.reset();
    _InRam = false;
    _FileImage = {};
}

int32_t MCFile::Seek(int32_t pos, int32_t from)
{
    CloseIfOrphaned();
    const int32_t length = static_cast<int32_t>(GetLength());
    int32_t newPosition = static_cast<int32_t>(_LogicalPosition);

    switch (from)
    {
        case SEEK_SET:
        {
            if (length < pos)
            {
                return READ_PAST_EOF;
            }

            newPosition = pos;
            break;
        }
        case SEEK_CUR:
        {
            if (GetLength() < _LogicalPosition + static_cast<uint32_t>(pos))
            {
                return READ_PAST_EOF;
            }

            newPosition = static_cast<int32_t>(_LogicalPosition) + pos;
            break;
        }
        case SEEK_END:
        {
            if (length < std::abs(pos) || pos > 0)
            {
                return READ_PAST_EOF;
            }

            newPosition = length + pos;
            break;
        }
    }

    if (newPosition == -1)
    {
        return INVALID_SEEK;
    }

    _LogicalPosition = static_cast<uint32_t>(newPosition);
    return NO_ERR;
}

int32_t MCFile::ReadRawAt(uint32_t pos, std::span<uint8_t> buffer)
{
    if (buffer.empty() || Orphaned())
    {
        return 0;
    }

    if (_InRam)
    {
        // Port fix: never copy past the image (the original trusts the caller).
        const uint32_t size = GetLength();

        if (pos >= size)
        {
            return 0;
        }

        const size_t count = std::min<size_t>(buffer.size(), size - pos);
        std::copy_n(_FileImage.begin() + pos, count, buffer.begin());
        return static_cast<int32_t>(count);
    }

    // A file in memory takes no children, so a child always reads its parents' disk file.
    const std::shared_ptr<OpenState> disk = DiskState();

    if (disk == nullptr)
    {
        return 0;
    }

    return static_cast<int32_t>(disk->Disk->ReadAt(_ParentOffset + pos, buffer));
}

uint8_t MCFile::ReadByte()
{
    CloseIfOrphaned();
    uint8_t value = 0;

    if (IsOpen())
    {
        ReadRawAt(_LogicalPosition, std::span(&value, 1));
        ++_LogicalPosition;
    }

    return value;
}

int16_t MCFile::ReadWord()
{
    CloseIfOrphaned();
    std::array<uint8_t, 2> bytes{};

    if (IsOpen())
    {
        ReadRawAt(_LogicalPosition, bytes);
        _LogicalPosition += 2;
    }

    return std::bit_cast<int16_t>(bytes);
}

int16_t MCFile::ReadShort()
{
    return ReadWord();
}

int32_t MCFile::ReadLong()
{
    CloseIfOrphaned();
    std::array<uint8_t, 4> bytes{};

    if (IsOpen())
    {
        ReadRawAt(_LogicalPosition, bytes);
        _LogicalPosition += 4;
    }

    return std::bit_cast<int32_t>(bytes);
}

float MCFile::ReadFloat()
{
    return std::bit_cast<float>(ReadLong());
}

int32_t MCFile::Read(std::span<uint8_t> buffer)
{
    CloseIfOrphaned();

    if (!IsOpen())
    {
        return 0;
    }

    const int32_t got = ReadRawAt(_LogicalPosition, buffer);
    _LogicalPosition += static_cast<uint32_t>(got);
    return got;
}

std::string MCFile::ReadLine(int32_t maxLength)
{
    CloseIfOrphaned();

    if (!IsOpen() || maxLength <= 0)
    {
        return {};
    }

    // Look at up to maxLength bytes, cut at the first CR, and step over CR LF.
    std::vector<uint8_t> window(static_cast<size_t>(maxLength) + 2, 0);
    const int32_t got = ReadRawAt(_LogicalPosition, std::span(window).first(static_cast<size_t>(maxLength) + 1));
    const int32_t limit = std::min(got, maxLength);
    int32_t length = 0;

    while (length < limit && window[length] != '\r')
    {
        ++length;
    }

    // Port fix: stop at LF too, for data files saved with Unix line endings.
    if (const auto lf = std::find(window.begin(), window.begin() + length, '\n'); lf != window.begin() + length)
    {
        length = static_cast<int32_t>(lf - window.begin());
    }

    // Original behaviour (OB-135): a line cut at maxLength skips the byte after the cut.
    _LogicalPosition += static_cast<uint32_t>(length + 1);

    if (window[length] == '\r' && window[length + 1] == '\n')
    {
        ++_LogicalPosition;
    }

    return std::string(window.begin(), window.begin() + length);
}

int32_t MCFile::ReadLine(std::span<char> buffer, int32_t maxLength)
{
    CloseIfOrphaned();

    if (!IsOpen() || maxLength <= 0)
    {
        return 0;
    }

    SDL_assert(buffer.size() > static_cast<size_t>(maxLength));
    const std::string line = ReadLine(maxLength);
    std::ranges::copy(line, buffer.begin());
    buffer[line.size()] = 0;
    return static_cast<int32_t>(line.size()) + 1;
}

int32_t MCFile::ReadLine(std::span<uint8_t> buffer, int32_t maxLength)
{
    // The same bytes, seen as text.
    return ReadLine(std::span(reinterpret_cast<char*>(buffer.data()), buffer.size()), maxLength);
}

int32_t MCFile::WriteRaw(std::span<const uint8_t> data)
{
    if (_IsChild || !IsOpen() || _State == nullptr || !_State->Disk.has_value())
    {
        return 0;
    }

    const size_t put = _State->Disk->WriteAt(_LogicalPosition, data);
    _LogicalPosition += static_cast<uint32_t>(put);
    return static_cast<int32_t>(put);
}

int32_t MCFile::WriteByte(uint8_t value)
{
    if (_IsChild || !IsOpen())
    {
        return 0;
    }

    return WriteRaw(std::span(&value, 1)) == 1 ? NO_ERR : WRITE_ERR;
}

int32_t MCFile::WriteLong(int32_t value)
{
    if (_IsChild || !IsOpen())
    {
        return 0;
    }

    return WriteRaw(std::bit_cast<std::array<uint8_t, 4>>(value)) == 4 ? NO_ERR : WRITE_ERR;
}

int32_t MCFile::WriteString(std::string_view text)
{
    if (_IsChild || !IsOpen())
    {
        return -1;
    }

    WriteRaw(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    return static_cast<int32_t>(text.size());
}

int32_t MCFile::WriteLine(std::string_view text)
{
    const int32_t written = WriteString(text);

    if (written >= 0)
    {
        WriteString("\r\n");
    }

    return written;
}

int32_t MCFile::Write(std::span<const uint8_t> data)
{
    if (_IsChild || !IsOpen())
    {
        return 0;
    }

    return WriteRaw(data);
}

bool MCFile::IsOpen() const
{
    // A child is open while its parents are (its own bytes in memory included).
    return _IsChild ? DiskState() != nullptr : _InRam || DiskState() != nullptr;
}

uint32_t MCFile::GetLength()
{
    CloseIfOrphaned();

    if (_IsChild)
    {
        _Length = _PhysicalLength;
        return _PhysicalLength;
    }

    if (_State != nullptr && _State->Disk.has_value() && (_Length == 0 || _FileMode != MCFileMode::Read))
    {
        _Length = _State->Disk->Length();
    }

    return _Length;
}

void MCFile::Skip(int32_t bytesToSkip)
{
    if (bytesToSkip != 0)
    {
        Seek(static_cast<int32_t>(_LogicalPosition) + bytesToSkip);
    }
}
