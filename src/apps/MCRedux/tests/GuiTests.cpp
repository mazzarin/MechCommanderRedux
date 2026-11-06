#include "stdafx.h"
#include "MCTest.h"
#include "fixtures/MCRegisteredData.h"
#include "fakes/MCMemoryFileSource.h"
#include "gui/MCGuiAnimation.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "main/MCGamePaths.h"
#include "main/MCGameContext.h"
#include "platform/MCRenderer.h"

// The GUI's memory once the GUI heap is gone: port bitmaps, animation shapes and font data.

namespace
{
    /// <summary>A VFX shape file with no shapes: the "1.10" version and a shape count of 0.</summary>
    std::vector<uint8_t> EmptyShapeFile()
    {
        return {'1', '.', '1', '0', 0, 0, 0, 0};
    }
}

TEST_CASE("gui: a port's bitmap starts zeroed, and pixels it shares outlive it")
{
    auto port = std::make_unique<MCGuiPort>();
    REQUIRE_EQ(port->Init(8, 4), 0);
    const std::span<uint8_t> pixels = port->Pixels();
    REQUIRE_EQ(pixels.size(), 32u);
    CHECK(std::ranges::all_of(pixels, [](uint8_t value) { return value == 0; }));
    CHECK_EQ(port->Frame().X1, 7);
    CHECK_EQ(port->Frame().Y1, 3);

    // Pixels another holder shares (the fog port shares the fog flags, the screen port the screen) stay with that
    // holder when the port goes.
    const auto foreign = std::make_shared<uint8_t[]>(32);
    std::fill_n(foreign.get(), 32, uint8_t{0x5a});
    MCRenderer::DestroyTexture(port->Bitmap());
    port->Bitmap().Pixels = foreign;
    CHECK_EQ(foreign.use_count(), 2);
    const std::weak_ptr<MCWindow> bitmap = port->SharedBitmap();
    port.reset();
    CHECK(bitmap.expired());
    CHECK_EQ(foreign.use_count(), 1);
    CHECK_EQ(foreign[0], 0x5a);
}

TEST_CASE("gui: a resized port gets a fresh zeroed bitmap")
{
    MCGuiPort port;
    REQUIRE_EQ(port.Init(4, 4), 0);
    std::ranges::fill(port.Pixels(), uint8_t{0x11});
    REQUIRE_EQ(port.Resize(6, 5), 0);
    const std::span<uint8_t> pixels = port.Pixels();
    REQUIRE_EQ(pixels.size(), 30u);
    CHECK(std::ranges::all_of(pixels, [](uint8_t value) { return value == 0; }));
    CHECK_EQ(port.Bitmap().XMax, 5);
    CHECK_EQ(port.Bitmap().YMax, 4);
}

TEST_CASE("gui: an animation's shapes are registered while loaded and freed with it")
{
    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    files.AddFile(std::string(ArtPath) + "memanim.shp", EmptyShapeFile());

    MCAddressKey firstShapes;
    {
        MCGuiAnimation animation;
        REQUIRE_EQ(animation.Load("memanim.shp"), 0);
        firstShapes = MCAddressKey::OfData(animation.ShapeTable());
        REQUIRE(firstShapes);
        CHECK(MCIsRegistered(firstShapes));
        CHECK_EQ(animation.NumberOfFrames(), 0);

        animation.Unload();
        CHECK(animation.ShapeTable().empty());
        CHECK(!MCIsRegistered(firstShapes));

        // Deleted while loaded (the original left the shapes to the GUI heap): the destructor lets them go.
        REQUIRE_EQ(animation.Load("memanim.shp"), 0);
        firstShapes = MCAddressKey::OfData(animation.ShapeTable());
        CHECK(MCIsRegistered(firstShapes));
    }

    CHECK(!MCIsRegistered(firstShapes));
}

TEST_CASE("gui: a font's data is registered while loaded and freed with it")
{
    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    // A font of one character one pixel high (count at +4, height at +8, the glyph table at +0x10, the glyph of width 1
    // at +0x14); the rest is filler.
    std::vector<uint8_t> fontBytes(64, 0x22);
    const auto put = [&fontBytes](size_t offset, uint32_t value) { std::memcpy(fontBytes.data() + offset, &value, 4); };
    put(4, 1);
    put(8, 1);
    put(0x10, 0x14);
    put(0x14, 1);
    files.AddFile(std::string(FontPath) + "memfont.fnt", fontBytes);

    MCAddressKey data;
    {
        MCGuiFont font;
        REQUIRE_EQ(font.Load("memfont.fnt"), 0);
        data = MCAddressKey::OfData(font.FontData.Bytes());
        REQUIRE(data);
        CHECK_EQ(font.FontData.Bytes()[63], 0x22);
        CHECK(MCIsRegistered(data));

        font.Unload();
        CHECK(font.FontData.Empty());
        CHECK(!MCIsRegistered(data));

        REQUIRE_EQ(font.Load("memfont.fnt"), 0);
        data = MCAddressKey::OfData(font.FontData.Bytes());
    }

    CHECK(!MCIsRegistered(data));
}
