#include "stdafx.h"
#include "logistics/MCSplashScreen.h"
#include "lib/MCFatal.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLogSlider.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCLogChatInput.h"
#include "logistics/MCPlayerNameObject.h"
#include "logistics/MCSessionScreen.h"
#include "main/MCLogistics.h"

namespace
{
    /// <summary>The connection screens' polling timer (its id, and every 2 s).</summary>
    constexpr int32_t PollTimer = 0;
    constexpr int32_t PollInterval = 2000;

    /// <summary>The function of a splash screen button's callback number (12..41), or none for another number.</summary>
    std::function<void()> SplashCallback(int32_t callback)
    {
        switch (callback)
        {
            case 12:
                return ShowModemScreen;
            case 13:
                return ShowSerialScreen;
            case 14:
                return ShowLanScreen;
            case 15:
                return ShowInternet;
            case 16:
                return CancelToConnect;
            case 17:
                return CancelToLan;
            case 18:
                return HostGame;
            case 19:
                return JoinGame;
            case 20:
                return CreateSession;
            case 21:
                return Go;
            case 22:
                return ::Leave;
            case 23:
                return WaitForCall;
            case 24:
                return GetNumber;
            case 25:
                return Dial;
            case 35:
                return CancelPrefs;
            case 36:
                return WritePrefs;
            case 37:
                return CreateSerialSession;
            case 38:
                return SerialJoinButtonPressed;
            case 40:
                return NewCampaign;
            case 41:
                return SoloLoadScreen;
            default:
                return {};
        }
    }

    /// <summary>The event routine of a slider's callback number (0x1a..0x1d), or none for another number.</summary>
    std::function<void(MCGuiObject&, MCGuiEvent&)> SliderRoutine(int32_t callback)
    {
        switch (callback)
        {
            case 0x1a:
                return SlideScreenBrightness;
            case 0x1b:
                return SlideMusicVolume;
            case 0x1c:
                return SlideRadioVolume;
            case 0x1d:
                return SlideFXVolume;
            default:
                return {};
        }
    }

    /// <summary>The function of a difficulty toggle's callback number (0x29..0x2b), or none for another number.</summary>
    std::function<void()> ToggleCallback(int32_t callback)
    {
        switch (callback)
        {
            case 0x29:
                return EasyToggle;
            case 0x2a:
                return RegularToggle;
            case 0x2b:
                return HardToggle;
            default:
                return {};
        }
    }

    /// <summary>Entry <paramref name="name"/> of the current block; a missing one is reported and reads as zero.</summary>
    template <MCFitValue T> T RequireEntry(MCFitIniFile& file, std::string_view name, std::string_view message)
    {
        const MCFitResult<T> value = file.Read<T>(name);
        Assert(value.has_value(), value.has_value() ? 0 : static_cast<uint32_t>(std::to_underlying(value.error())),
               message);
        return value.value_or(T{});
    }
}

MCSplashScreen::MCSplashScreen() : _Art(SharedBackground.lock())
{
    if (_Art == nullptr)
    {
        _Art = std::make_shared<SharedArt>();
        SharedBackground = _Art;
    }
}

auto MCSplashScreen::Init(MCFitIniFile& screenFile) -> int32_t
{
    ReadElementCount(screenFile);

    // Blocks: which elements show together (ShowBlock), one byte per element.
    if (screenFile.SeekBlock("Blocks") == 0)
    {
        const int32_t count =
            RequireEntry<int32_t>(screenFile, "Block Count", " Could not find block count in Generic Screen ");
        Blocks.assign(static_cast<size_t>(std::max(count, 0)), std::vector<uint8_t>(Elements.size()));

        for (size_t i = 0; i < Blocks.size(); i++)
        {
            // A block the file lacks (or has short) shows nothing for the elements left out.
            static_cast<void>(screenFile.ReadArray<uint8_t>(std::format("Block{}", i), std::span(Blocks[i])));
        }
    }

    for (int32_t i = 0; i < NumElements(); i++)
    {
        const ElementHeader header = ReadElement(screenFile, i);

        if (header.Type == 0)
        {
            // The background: the shared picture, loaded again only when the art changes.
            Assert(i == 0, static_cast<uint32_t>(i), " If there's a background it MUST be the first element ");

            if (!MCIEquals(_Art->FileName, header.Art))
            {
                _Art->FileName = header.Art;
                _Art->Port.Load(header.Art);
            }

            // The screen draws the shared art each frame (the original made the shared port its own).
            InitBackground(screenFile, header);
            ArtPort = std::shared_ptr<MCLogPort>(_Art, &_Art->Port);
            continue;
        }

        MakeElementOn(*this, screenFile, i, header);
    }

    ShowGuiWindow(false);
    return 0;
}

auto MCSplashScreen::MakeElementOn(MCGuiObject& parent, MCFitIniFile& file, int32_t index, const ElementHeader& header)
    -> bool
{
    switch (header.Type)
    {
        case 1:
        {
            MakeElement<MCLogButton>(
                parent, index,
                [&](MCLogButton& button)
                {
                    const int32_t result = button.Init(header.Left, header.Top, header.Width, header.Height, {});
                    button.SetTransparent(true);
                    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't init new button ");

                    if (const std::optional<int32_t> callback = ReadButton(file, button, header.Art, true))
                    {
                        if (std::function<void()> exec = SplashCallback(*callback))
                        {
                            button.Callback().SetExec(std::move(exec));
                        }
                        else if (!SetScreenCallback(button, *callback))
                        {
                            Fatal(*callback, " Illegal callback value");
                        }
                    }
                });
            return true;
        }

        case 4:
        {
            MakeTextEntry(parent, index, header, true);
            return true;
        }

        case 5:
        {
            MakeFilePane(parent, file, index, header);
            return true;
        }

        case 6:
        {
            // A picture; elements 6 and the others go just behind the rest.
            const MCGuiHandle<MCLogImage> image =
                MakeElement<MCLogImage>(parent, index,
                                        [&](MCLogImage& made)
                                        {
                                            made.Init(header.Left, header.Top, header.Width, header.Height);
                                            made.Art = std::make_shared<MCLogPort>();
                                            made.Art->Load(header.Art);
                                        });
            image->SetDepth(index == 6 ? -0xb : -0xa);
            image->SetEventRoutine(ImageHandleEvent);
            return true;
        }

        case 7:
        {
            MakeElement<MCLogScrollTextObject>(
                parent, index,
                [&](MCLogScrollTextObject& text)
                {
                    text.Init(header.Left, header.Top, header.Width, header.Height, {});
                    const bool scrolling =
                        RequireEntry<bool>(file, "Scrolling", " Couldn't locate Scrolling in textscrollpane ");
                    text.ScrollTab->ShowGuiWindow(scrolling);
                    text.Scrolling = scrolling;
                    text.ShowGuiWindow(true);
                });
            return true;
        }

        case 8:
        {
            MakeElement<MCGameList>(parent, index,
                                    [&](MCGameList& list)
                                    {
                                        list.Init(header.Left, header.Top, header.Width, header.Height, {});
                                        list.ShowGuiWindow(true);
                                    });
            return true;
        }

        case 9:
        {
            MakeElement<MCLogSlider>(
                parent, index,
                [&](MCLogSlider& slider)
                {
                    slider.Init(header.Left, header.Top, header.Width, header.Height, {});
                    slider.MinValue = RequireEntry<int32_t>(file, "MinValue", " Couldn't locate min slider value");
                    slider.MaxValue = RequireEntry<int32_t>(file, "MaxValue", " Couldn't locate max slider value");
                    const int32_t callback = RequireEntry<int32_t>(file, "Callback", " Couldn't locate callback value");

                    if (auto routine = SliderRoutine(callback))
                    {
                        slider.SetEventRoutine(std::move(routine));
                    }

                    slider.ShowGuiWindow(true);
                });
            return true;
        }

        case 10:
        {
            // A difficulty toggle.
            MakeElement<MCLogToolButton>(parent, index,
                                         [&](MCLogToolButton& toggle)
                                         {
                                             toggle.Init(header.Left, header.Top, header.Width, header.Height, {});
                                             toggle.SetTransparent(true);

                                             if (MCIEquals(header.Art, "NONE"))
                                             {
                                                 toggle.SetBackColor(0xff);
                                             }
                                             else
                                             {
                                                 toggle.SetUpPicture(header.Art);
                                             }

                                             if (const std::optional<int32_t> callback =
                                                     ReadButton(file, toggle, header.Art, false))
                                             {
                                                 std::function<void()> exec = ToggleCallback(*callback);

                                                 if (!exec)
                                                 {
                                                     Fatal(*callback, " Illegal callback value");
                                                 }

                                                 toggle.Callback().SetExec(std::move(exec));
                                             }

                                             toggle.ShowGuiWindow(true);
                                         });
            return true;
        }

        default:
            return false;
    }
}

auto MCSplashScreen::OnDestroy() -> void
{
    Blocks.clear();
    // The shared art stays with the other splash screens.
    ArtPort.reset();
    MCGenericScreen::OnDestroy();
}

auto MCSplashScreen::ShowGuiWindow(bool show) -> void
{
    // The connection screens poll while shown: the connect screen through its element 3.
    if (GlobalLog()->ConnectScreen.Is(*this))
    {
        if (show)
        {
            GuiSystem().AddTimer(Elements[3], PollTimer, PollInterval, 0, 0, false);
            MCGenericScreen::ShowGuiWindow(show);
            return;
        }

        GuiSystem().RemoveTimer(*Element<MCGuiObject>(3), PollTimer);
    }
    else if (GlobalLog()->LanScreen.Is(*this))
    {
        if (show)
        {
            GuiSystem().AddTimer(weak_from_this(), PollTimer, PollInterval, 0, 0, false);
            MCGenericScreen::ShowGuiWindow(show);
            return;
        }

        GuiSystem().RemoveTimer(*this, PollTimer);
    }

    MCGenericScreen::ShowGuiWindow(show);
}

auto MCSplashScreen::ShowBlock(int32_t block) -> void
{
    if (block < 0 || block >= NumBlocks())
    {
        return;
    }

    const std::vector<uint8_t>& shown = Blocks[static_cast<size_t>(block)];

    for (int32_t i = 1; i < static_cast<int32_t>(Elements.size()); i++)
    {
        Element<MCGuiObject>(i)->ShowGuiWindow(false);
    }

    for (size_t i = 1; i < Elements.size(); i++)
    {
        if (shown[i] != 0)
        {
            Element<MCGuiObject>(shown[i])->ShowGuiWindow(true);
        }
    }
}
