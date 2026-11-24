#include "stdafx.h"
#include "platform/MCRegistry.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFrameLog.h"

namespace
{
    /// <summary>The store's file in the user folder.</summary>
    constexpr std::string_view StoreFileName = "registry.cfg";

    /// <summary>A key and value name as one lower-case store key (<c>key|value</c>).</summary>
    std::string StoreKey(std::string_view keyName, std::string_view valueName)
    {
        std::string key;
        key.reserve(keyName.size() + valueName.size() + 1);

        for (char c : keyName)
        {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }

        key.push_back('|');

        for (char c : valueName)
        {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }

        return key;
    }

    /// <summary>Every stored value, keyed by <see cref="StoreKey"/>; each line of the file is <c>key=data</c>.</summary>
    std::map<std::string, std::string> LoadStore()
    {
        std::map<std::string, std::string> values;
        std::filesystem::path path;
        {
            MCFrameLog::Scope probe("probe.reg resolve");
            path = MCFileSystem::Resolve(StoreFileName);
        }
        MCFrameLog::Scope probe("probe.reg load (open, read, close)");
        std::ifstream file(path);
        std::string line;

        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            const size_t equals = line.find('=');

            if (equals != std::string::npos)
            {
                values[line.substr(0, equals)] = line.substr(equals + 1);
            }
        }

        return values;
    }
}

namespace MCRegistry
{
    std::optional<std::string> Read(std::string_view keyName, std::string_view valueName)
    {
        const std::map<std::string, std::string> values = LoadStore();
        const auto found = values.find(StoreKey(keyName, valueName));

        if (found == values.end())
        {
            return std::nullopt;
        }

        return found->second;
    }

    bool Write(std::string_view keyName, std::string_view valueName, std::string_view data)
    {
        std::map<std::string, std::string> values = LoadStore();
        values[StoreKey(keyName, valueName)] = std::string(data);
        std::filesystem::path path;
        {
            MCFrameLog::Scope probe("probe.reg resolve write");
            path = MCFileSystem::ResolveWrite(StoreFileName);
        }
        std::optional<std::ofstream> file;
        {
            MCFrameLog::Scope probe("probe.reg open to write");
            file.emplace(path, std::ios::trunc);
        }

        if (!*file)
        {
            return false;
        }

        {
            MCFrameLog::Scope probe("probe.reg put");
            for (const auto& [key, value] : values)
            {
                *file << key << '=' << value << '\n';
            }
        }

        const bool good = static_cast<bool>(*file);
        MCFrameLog::Scope probe("probe.reg close");
        file.reset();
        return good;
    }
}
