#include "stdafx.h"
#include "gui/MCGuiStartupWindow.h"
#include "engine/MCFont.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCDice.h"
#include "lib/MCFile.h"
#include "lib/MCPacketFile.h"
#include "platform/MCRenderer.h"
#include "sound/MCSoundSystem.h"
#include "vfx/MCVfxFunctions.h"

auto MCGuiStartupWindow::FreeImages() -> void
{
    for (std::vector<uint8_t>& image : StaticImages)
    {
        if (!image.empty())
        {
            MCRenderer::UnregisterData(std::span(image.data(), 1));
        }

        image = {};
    }
}

auto MCGuiStartupWindow::OnDestroy() -> void
{
    FreeImages();
    StaticPort.reset();
    MCGuiObject::OnDestroy();
}

auto MCGuiStartupWindow::DoStatic() -> void
{
    const std::shared_ptr<MCWindow> window = StaticPane().Window.lock();
    const std::span<uint8_t> pixels = window->Bytes();

    for (int32_t row = 0; row < Height(); row++)
    {
        if (!RollDice(0x1e))
        {
            // Now and then a whole row is copied from a random one.
            if (RollDice(0x32) != 0)
            {
                const int32_t sourceRow = RandomNumber(Height());
                std::memmove(pixels.data() + Width() * row, pixels.data() + sourceRow * Width(),
                             static_cast<size_t>(Width()));
            }
        }
        else
        {
            for (int32_t column = 0; column < Width(); column++)
            {
                AGPixelWrite(StaticPane(), column, row, static_cast<uint32_t>(MCPort::Rand()) & 0x1f);
            }
        }
    }
}

auto MCGuiStartupWindow::EndStatic() -> void
{
    VfxPaneWipe(StaticPane(), 0);
}

auto MCGuiStartupWindow::Display() -> void
{
    if (!ShowWindow || (IsHidden() && HideOffset == 0))
    {
        return;
    }

    // (The original drew each step straight onto the screen.)
    if (StaticPort != nullptr)
    {
        Step();
        DrawInFramePass(*DisplayPort);
    }
}

auto MCGuiStartupWindow::Step() -> void
{
    const std::shared_ptr<MCSoundSystem> sounds = SoundSystem();
    const int32_t step = StartupState;
    StartupState = step + 1;
    const std::shared_ptr<MCFont> font = LineFont();

    if (sounds == nullptr)
    {
        return;
    }

    // Once the sequence is over, the screen shows noise.
    if (StartupState > 0xaa)
    {
        if (RollDice(10) != 0)
        {
            DoStatic();
            AGShapeDraw(StaticPane(), StaticImages[2], 0, 0x140, 0xf0);
            return;
        }

        if (!RollDice(0x1e))
        {
            return;
        }

        sounds->Update();
        DoStatic();
        AGShapeDraw(StaticPane(), StaticImages[2], 1, 0x140, 0xf0);
        return;
    }

    const int32_t pointX = UplinkPoints[RandomStart][0];
    const int32_t pointY = UplinkPoints[RandomStart][1];
    auto setLarge = [font](bool large)
    {
        font->Scale = large ? 1.6f : 1.0f;
        font->Scaled = large;
    };

    // The text is typed two letters a frame: the first piece restarts the line, the next ones follow the width
    // typed so far (measured by `measured`, which is the piece itself except once), the last one isn't measured.
    auto typeFirst = [&](int32_t lineX, int32_t lineY, std::string_view text)
    {
        setLarge(false);
        font->Print(lineX, lineY, text, 0xfd, StaticPane());
        TextX = font->PrintWidth(text, false);
    };

    auto typeNext = [&](int32_t lineX, int32_t lineY, std::string_view text, std::string_view measured)
    {
        const int32_t typed = TextX;
        setLarge(false);
        font->Print(typed + lineX, lineY, text, 0xfd, StaticPane());
        TextX = font->PrintWidth(measured, false) + typed;
    };

    auto typeLast = [&](int32_t lineX, int32_t lineY, std::string_view text)
    {
        setLarge(false);
        font->Print(TextX + lineX, lineY, text, 0xfd, StaticPane());
    };

    // The finished picture: the map, the bunker and its uplink to the chosen point.
    auto drawUplink = [&]()
    {
        AGShapeDraw(StaticPane(), StaticImages[0], 0, 0x140, 0xf0);
        AGEllipseFill(StaticPane(), 0x10a, 0xe5, 3, 3, 0xfd);
        VfxLineDraw(StaticPane(), 0x10a, 0xe5, 0x1c1, 400, 0xfd);
        setLarge(false);
        font->Print(0x1c2, 0x18b, "Forward Command Bunker", 0xfd, StaticPane());
        setLarge(true);
        font->Print(0x1c2, 0x19f, "Uplinking...", 0xfc, StaticPane());
        VfxLineDraw(StaticPane(), 0x10a, 0xe5, pointX, pointY, 0xfe);
        VfxLineDraw(StaticPane(), pointX, pointY, 10, pointY, 0xfd);
    };

    uint32_t sample = 0x10;

    switch (step)
    {
        case 0:
        {
            AGShapeDraw(StaticPane(), StaticImages[0], 0, 0x140, 0xf0);
            sample = 0x11;
            break;
        }
        case 9:
        {
            AGEllipseFill(StaticPane(), 0x10a, 0xe5, 3, 3, 0xfd);
            sample = 0xf;
            break;
        }
        case 0x13:
            VfxLineDraw(StaticPane(), 0x10a, 0xe5, 0x1c1, 400, 0xfd);
            break;
        // "Forward Command Bunker"
        case 0x1d:
        {
            TextX = 0;
            typeFirst(0x1c7, 0x18b, "Fo");
            break;
        }
        case 0x1e:
            typeNext(0x1c7, 0x18b, "rw", "rw");
            break;
        case 0x1f:
            typeNext(0x1c7, 0x18b, "ar", "ar");
            break;
        case 0x20:
        case 0x24:
            typeNext(0x1c7, 0x18b, "d ", "d ");
            break;
        case 0x21:
            typeNext(0x1c7, 0x18b, "Co", "Co");
            break;
        case 0x22:
            typeNext(0x1c7, 0x18b, "mm", "mm");
            break;
        case 0x23:
            typeNext(0x1c7, 0x18b, "an", "an");
            break;
        case 0x25:
            typeNext(0x1c7, 0x18b, "Bu", "Bu");
            break;
        case 0x26:
            typeLast(0x1c7, 0x18b, "nker");
            break;
        case 0x27:
        {
            setLarge(true);
            font->Print(0x1c2, 0x19f, "Uplinking...", 0xfc, StaticPane());
            break;
        }
        case 0x31:
            VfxLineDraw(StaticPane(), 0x10a, 0xe5, pointX, pointY, 0xfe);
            break;
        case 0x3b:
        {
            sounds->PlayInterfaceSample(0x20);
            sounds->Update();
            DoStatic();
            return;
        }
        case 0x3c:
        case 0x3d:
        case 0x3e:
        case 0x3f:
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
        {
            VfxPaneWipe(StaticPane(), 0);
            drawUplink();
            sounds->Update();
            DoStatic();
            AGShapeDraw(StaticPane(), StaticImages[1], 0, 0x140, 0xf0);
            return;
        }
        case 0x45:
        {
            EndStatic();
            drawUplink();
            sounds->PlayInterfaceSample(0x10);
            sounds->Update();
            AGEllipseFill(StaticPane(), pointX, pointY, 2, 2, 0xfc);
            return;
        }

        // "ComSat CSM-43a"
        case 0x4f:
            typeFirst(10, pointY + 10, "Co");
            break;
        case 0x50:
            // Original bug (OB-065): "mS" is typed but "Ms" measured.
            typeNext(10, pointY + 10, "mS", "Ms");
            break;
        case 0x51:
            typeNext(10, pointY + 10, "at", "at");
            break;
        case 0x52:
            typeNext(10, pointY + 10, " C", " C");
            break;
        case 0x53:
            typeNext(10, pointY + 10, "SM", "SM");
            break;
        case 0x54:
            typeNext(10, pointY + 10, "-4", "-4");
            break;
        case 0x55:
            typeNext(10, pointY + 10, "3a", "3a");
            break;
        // "Establishing Protocols"
        case 0x59:
            typeFirst(10, pointY + 0x19, "Es");
            break;
        case 0x5a:
            typeNext(10, pointY + 0x19, "ta", "ta");
            break;
        case 0x5b:
            typeNext(10, pointY + 0x19, "bl", "bl");
            break;
        case 0x5c:
            typeNext(10, pointY + 0x19, "is", "is");
            break;
        case 0x5d:
            typeNext(10, pointY + 0x19, "hi", "hi");
            break;
        case 0x5e:
            typeNext(10, pointY + 0x19, "ng", "ng");
            break;
        case 0x5f:
            typeNext(10, pointY + 0x19, " P", " P");
            break;
        case 0x60:
            typeNext(10, pointY + 0x19, "ro", "ro");
            break;
        case 0x61:
            typeNext(10, pointY + 0x19, "to", "to");
            break;
        case 0x62:
            typeLast(10, pointY + 0x19, "cols");
            break;
        case 0x63:
        {
            sounds->PlayInterfaceSample(0x10);
            sounds->Update();
            return;
        }

        // "Establishing Downlink"
        case 0x6d:
            typeFirst(10, pointY + 0x2d, "Es");
            break;
        case 0x6e:
            typeNext(10, pointY + 0x2d, "ta", "ta");
            break;
        case 0x6f:
            typeNext(10, pointY + 0x2d, "bl", "bl");
            break;
        case 0x70:
            typeNext(10, pointY + 0x2d, "is", "is");
            break;
        case 0x71:
            typeNext(10, pointY + 0x2d, "hi", "hi");
            break;
        case 0x72:
            typeNext(10, pointY + 0x2d, "ng", "ng");
            break;
        case 0x73:
            typeNext(10, pointY + 0x2d, " D", " D");
            break;
        case 0x74:
            typeNext(10, pointY + 0x2d, "ow", "ow");
            break;
        case 0x75:
            typeNext(10, pointY + 0x2d, "nl", "nl");
            break;
        case 0x76:
            typeLast(10, pointY + 0x2d, "ink");
            break;
        // The field site
        case 0x77:
            VfxLineDraw(StaticPane(), pointX, pointY, 0xf3, 0x101, 0xfd);
            break;
        case 0x81:
        {
            AGEllipseFill(StaticPane(), 0xf3, 0x101, 5, 5, 0xfb);
            sample = 0xf;
            break;
        }
        case 0x8b:
            VfxLineDraw(StaticPane(), 0xf3, 0x101, 0x1b, 0x101, 0xfd);
            break;
        // "Field Site Linking..."
        case 0x95:
            typeFirst(0x1b, 0x104, "Fi");
            break;
        case 0x96:
            typeNext(0x1b, 0x104, "el", "el");
            break;
        case 0x97:
            typeNext(0x1b, 0x104, "d ", "d ");
            break;
        case 0x98:
            typeNext(0x1b, 0x104, "Si", "Si");
            break;
        case 0x99:
            typeNext(0x1b, 0x104, "te", "te");
            break;
        case 0x9a:
            typeNext(0x1b, 0x104, " L", " L");
            break;
        case 0x9b:
            typeNext(0x1b, 0x104, "in", "in");
            break;
        case 0x9c:
            typeNext(0x1b, 0x104, "ki", "ki");
            break;
        case 0x9d:
            typeNext(0x1b, 0x104, "ng", "ng");
            break;
        case 0x9e:
            typeLast(0x1b, 0x104, "...");
            break;
        case 0x9f:
        {
            setLarge(true);
            font->Print(0x1b, 0x113, "GO", 0xfd, StaticPane());
            sample = 0x11;
            break;
        }
        case 0xa9:
        {
            NoiseSample = sounds->PlayInterfaceSample(0x21);
            sounds->Update();
            DoStatic();
            AGShapeDraw(StaticPane(), StaticImages[2], 0, 0x140, 0xf0);
            return;
        }
        default:
            return;
    }

    sounds->PlayInterfaceSample(sample);
    sounds->Update();
}

auto MCGuiStartupWindow::Draw() -> void
{
    MCGuiObject::Draw();

    if (StaticPort != nullptr)
    {
        StaticPort->CopyTo(Port().Frame(), 0, 0, false);
    }
}

auto MCGuiStartupWindow::StaticPane() const -> MCPane&
{
    return StaticPort->Frame();
}

auto MCGuiStartupWindow::Setup() -> int32_t
{
    MCPacketFile& art = *GuiSystem().ArtFile;

    for (int32_t i = 0; i < static_cast<int32_t>(StaticImages.size()); i++)
    {
        if (const int32_t result = art.SeekPacket(0x2d + i); result != 0)
        {
            return result;
        }

        MCFile file;

        if (const int32_t result = file.Open(art, static_cast<uint32_t>(art.GetPacketSize())); result != 0)
        {
            return result;
        }

        std::vector<uint8_t>& image = StaticImages[i];
        image.resize(file.FileSize());
        file.Read(image);
        MCRenderer::RegisterData(std::span(image.data(), image.size()), MCDataKind::Shapes);
        file.Close();
    }

    StaticPort = std::make_unique<MCGuiPort>();

    if (StaticPort->Init(Width(), Height()) != 0)
    {
        return -1;
    }

    FrameCount = 0;
    RandomStart = RandomNumber(static_cast<int32_t>(UplinkPoints.size()));
    return 0;
}
