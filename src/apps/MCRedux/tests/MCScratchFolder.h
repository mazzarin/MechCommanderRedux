#pragma once

/// <summary>A scratch folder of a test's own under the temp folder (its group's, then its name), deleted with it.</summary>
class MCScratchFolder
{
public:
    MCScratchFolder(std::string_view group, std::string_view name)
        : _Path(std::filesystem::temp_directory_path() / group / name)
    {
        std::error_code error;
        std::filesystem::remove_all(_Path, error);
        std::filesystem::create_directories(_Path);
    }

    MCScratchFolder(const MCScratchFolder&) = delete;
    MCScratchFolder& operator=(const MCScratchFolder&) = delete;

    ~MCScratchFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(_Path, error);
    }

    const std::filesystem::path& Path() const { return _Path; }

private:
    std::filesystem::path _Path;
};
