#include "stdafx.h"
#include "main/MCLogisticsShared.h"
#include "main/MCGameStrings.h"

auto LogWeightClass(float tonnage) -> int32_t
{
    if (tonnage < 40.0f)
    {
        return 0;
    }

    if (tonnage < 60.0f)
    {
        return 1;
    }

    if (tonnage < 80.0f)
    {
        return 2;
    }

    return 3;
}

auto LogSlotForm(uint8_t masterID) -> MCComponentForm
{
    return masterID < NumMasterComponents() ? MasterComponentList[masterID].Form : MCComponentForm::Simple;
}

auto LoadLogString(uint32_t id) -> std::string
{
    return LoadGameString(id, 0xfe);
}

auto LogFileBaseName(std::string_view path, size_t maxLength) -> std::string
{
    const size_t separator = path.find_last_of("\\/:");
    std::string_view name = separator == std::string_view::npos ? path : path.substr(separator + 1);
    const size_t dot = name.rfind('.');

    if (dot != std::string_view::npos)
    {
        name = name.substr(0, dot);
    }

    return std::string(name.substr(0, maxLength));
}

auto ReadText(MCFitIniFile& file, std::string_view name, size_t maxLength, std::string& text) -> bool
{
    MCFitResult<std::string> value = file.Read<std::string>(name);

    if (!value.has_value())
    {
        return false;
    }

    if (value->size() >= maxLength)
    {
        text = value->substr(0, maxLength);
        return false;
    }

    text = std::move(*value);
    return true;
}

auto ReadRequiredText(MCFitIniFile& file, std::string_view name, size_t maxLength, std::string_view message)
    -> std::string
{
    std::string text;
    const bool read = ReadText(file, name, maxLength, text);
    Assert(read, 0, message);
    return text;
}

auto MPPurchaseFileName(MCFitIniFile& file) -> std::string
{
    // The original read the name into a 9-byte buffer.
    constexpr size_t MaxPurchaseName = 9;
    std::string purchaseName = "purchase";

    if (file.SeekBlock("PurchaseInfo") != 0)
    {
        ReadText(file, "MainPurchaseFile", MaxPurchaseName, purchaseName);
    }

    return purchaseName;
}
