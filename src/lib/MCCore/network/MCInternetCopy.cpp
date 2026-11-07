#include "stdafx.h"
#include "network/MCInternetCopy.h"

#include "lib/MCFitIniFile.h"
#include "lib/MCRandomId.h"

namespace
{
    constexpr std::string_view CopyFile = "internet.cfg";
    constexpr std::string_view CopyBlock = "Internet";
    constexpr std::string_view IdEntry = "Id";
}

std::string MCInternetCopyId()
{
    if (MCFitIniFile file; file.Open(CopyFile) == 0 && file.SeekBlock(CopyBlock) == 0)
    {
        if (MCFitResult<std::string> id = file.Read<std::string>(IdEntry); id.has_value() && MCIsRandomId(*id))
        {
            return std::move(*id);
        }
    }

    std::string id = MCRandomId();
    MCFitIniFile file;

    if (const int32_t result = file.Create(CopyFile); result != 0)
    {
        SDL_Log("Internet games: cannot create %s (%#x); this copy's ID lasts this run only", CopyFile.data(),
                static_cast<uint32_t>(result));
        return id;
    }

    file.WriteBlock(CopyBlock);
    file.WriteIdString(IdEntry, id);
    return id;
}
