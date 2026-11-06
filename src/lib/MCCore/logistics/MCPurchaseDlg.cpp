#include "stdafx.h"
#include "logistics/MCPurchaseDlg.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCUpdateDisplay.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPilotPurchaseBlock.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "logistics/MCCompPurchaseBlock.h"
#include "logistics/MCUnitLimits.h"
#include "logistics/MCPurProfile.h"
#include "main/MCGameStrings.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The samples the dialog plays: a click, and a refusal (the arrow can't go further).</summary>
    constexpr uint32_t ClickSample = 0xf;
    constexpr uint32_t RefusedSample = 0x33;

    /// <summary>Whether (x, y) of the box lies in the rectangle (inclusive).</summary>
    bool Inside(int32_t x, int32_t y, int32_t left, int32_t top, int32_t right, int32_t bottom)
    {
        return x >= left && x <= right && y >= top && y <= bottom;
    }
}

auto MCPurchaseDlg::Init(int32_t newPurchaseType, int32_t newUnitCost, int32_t newMaxQuantity,
                         std::string_view newTitle, std::string_view newSubtitle,
                         MCOptionalRef<const MCGuiPort> picture) -> void
{
    if (newMaxQuantity < 0)
    {
        newMaxQuantity = MaxSpinnerQuantity;
    }

    Quantity = 1;
    PurchaseType = newPurchaseType;
    UnitCost = newUnitCost;
    Title = newTitle.substr(0, newTitle.find('\0'));
    Subtitle = newSubtitle.substr(0, newSubtitle.find('\0'));
    PicturePort.reset();

    if (picture.has_value())
    {
        const MCGuiPort& source = picture->get();
        PicturePort = std::make_shared<MCLogPort>();
        PicturePort->Init(source.Width(), source.Height());
        VfxPaneCopy(source.Frame(), 0, 0, PicturePort->Frame(), 0, 0, -1);
    }

    MaxQuantity = newMaxQuantity;
    // A single item needs no spinner.
    Spinner = newMaxQuantity != 1;
}

auto MCPurchaseDlg::OnDestroy() -> void
{
    Subtitle.clear();
    Title.clear();
    MCLogDialogBox::OnDestroy();
}

auto MCPurchaseDlg::CanAddOne() const -> bool
{
    return Quantity < MaxQuantity && UnitCost * (Quantity + 1) <= ResourcePoints;
}

auto MCPurchaseDlg::HandleEvent(MCGuiEvent& event) -> void
{
    const int32_t localX = event.X - GlobalX();
    const int32_t localY = event.Y - GlobalY();
    // The spinner's arrows only work for purchases (even types) and type 5.
    const bool arrowsLocked = (PurchaseType & 1) != 0 && PurchaseType != 5;

    // Holds an arrow down: it repeats on the timer.
    auto holdArrow = [this](PressedPart arrow, bool up)
    {
        DrawBackground();
        Pressed = arrow;
        _SpinUp = up;
        GuiSystem().AddTimer(weak_from_this(), RepeatTimer, 200, 0, 0, false);
        GuiSystem().Grab(weak_from_this());
    };

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            if (Inside(localX, localY, 0x40, 0x83, 0x6e, 0x8e))
            {
                // OK.
                PlayLogSound(ClickSample);
                Pressed = PressedPart::Ok;
                UpdateDisplay(false, false, 0, false, 0);
                Deactivate(-1);
            }
            else if (Inside(localX, localY, 0x77, 0x83, 0xa5, 0x8e))
            {
                // Cancel.
                PlayLogSound(ClickSample);
                Pressed = PressedPart::Cancel;
                UpdateDisplay(false, false, 0, false, 0);
                Deactivate(0);
            }
            else if (Spinner)
            {
                if (Inside(localX, localY, 0x92, 0x53, 0x9a, 0x59))
                {
                    if (arrowsLocked)
                    {
                        break;
                    }

                    holdArrow(PressedPart::Up, true);

                    if (CanAddOne())
                    {
                        PlayLogSound(ClickSample);
                        Quantity++;
                        break;
                    }

                    PlayLogSound(RefusedSample);
                }
                else if (Inside(localX, localY, 0x92, 0x5b, 0x9a, 0x61) && !arrowsLocked)
                {
                    holdArrow(PressedPart::Down, false);

                    if (Quantity != 0)
                    {
                        PlayLogSound(ClickSample);
                        Quantity--;
                        break;
                    }

                    PlayLogSound(RefusedSample);
                }
            }
            break;
        }
        case MCGuiEventType::LeftButtonUp:
        {
            DrawBackground();
            GuiSystem().RemoveTimer(*this, RepeatTimer);
            break;
        }
        case MCGuiEventType::KeyDown:
        {
            if (event.Key == 0x0d)
            {
                Deactivate(TwoButton ? -1 : 0);
            }
            else if (event.Key == 0x1b)
            {
                Deactivate(0);
            }
            break;
        }
        case MCGuiEventType::Timer:
        {
            // The held arrow repeats.
            if (!_SpinUp)
            {
                if (Quantity != 0)
                {
                    Quantity--;
                }

                DrawBackground();
                Pressed = PressedPart::Down;
            }
            else
            {
                if (CanAddOne() && Quantity < MaxRepeatQuantity)
                {
                    Quantity++;
                }

                DrawBackground();
                Pressed = PressedPart::Up;
            }
            break;
        }

        default:
            break;
    }
}

auto MCPurchaseDlg::Draw() -> void
{
    DrawBox();
    MCPane& port = _Port->Frame();
    auto label = [](uint32_t id) { return LoadGameString(id, 0xfe); };
    // Labels: price, resource points, quantity, remaining.
    MedWhiteFont->WriteString(port, 0x15, 0x47, label(0x48));
    MedWhiteFont->WriteString(port, 0x91, 0x47, label(0x4b));
    MedWhiteFont->WriteString(port, 0x15, 0x57, label(0x49));
    MedWhiteFont->WriteString(port, 0x16, 0x6c, label(0x4a));
    MedWhiteFont->WriteString(port, 0x91, 0x6c, label(0x4b));
    MedWhiteFont->WriteString(port, 0x2a, 0x20, Title);
    std::shared_ptr<MCGuiFont> subtitleFont = PurchaseType == 3 ? MedRedFont : MedWhiteFont;
    subtitleFont->WriteString(port, 0x2a, 0x2e, Subtitle);

    // The numbers are right-aligned at 0x8e, measured in the black font.
    const std::string cost = std::format("{}", UnitCost < 0 ? -UnitCost : UnitCost);
    MedWhiteFont->WriteString(port, 0x8e - MedBlackFont->Width(cost), 0x47, cost);
    const std::string left = std::format("{}", ResourcePoints - Quantity * UnitCost);
    MedWhiteFont->WriteString(port, 0x8e - MedBlackFont->Width(left), 0x6c, left);
    const std::string quantity = std::format("{}", Quantity);
    MedWhiteFont->WriteString(port, (0x11 - MedBlackFont->Width(quantity)) / 2 + 0x7e, 0x55, quantity);

    // The item kind's icon (mech, pilot, component, vehicle; buy/sell). Another type loads the quantity text as a file
    // name, as the original did.
    static constexpr std::array<std::string_view, 8> icons = {"lspcbm00.tga", "lspcbm01.tga", "lspcbp00.tga",
                                                              "lspcbp01.tga", "lspcbc00.tga", "lspcbc01.tga",
                                                              "lspcbv00.tga", "lspcbv01.tga"};
    std::shared_ptr<MCLogPort> icon = PurchaseType >= 0 && PurchaseType < 8
                                          ? LogScreenArt(icons[static_cast<size_t>(PurchaseType)])
                                          : LogArt(quantity);
    icon->CopyTo(port, 3, 3, true);
    DrawPressed();
}

auto MCPurchaseDlg::Activate() -> void
{
    NeedBackground = true;
    GuiSystem().Grab(weak_from_this());
    BringToFront(false);
    DrawBackground();
    ShowGuiWindow(true);
}

auto MCPurchaseDlg::Deactivate(int32_t dialogResult) -> void
{
    GuiSystem().RemoveTimer(*this, RepeatTimer);
    GuiSystem().Release();
    ShowGuiWindow(false);

    if (PurchaseCallback)
    {
        PurchaseCallback(dialogResult, Quantity);
    }
}
