#include "stdafx.h"
#include "MCTest.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "GuiTree.h"
#include "TestGame.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFitIniFile.h"
#include "main/MCLogistics.h"
#include "main/MCPrefs.h"
#include "platform/MCFileSystem.h"
#include "platform/MCPresenter.h"
#include "sound/MCSoundSystem.h"
#include "vfx/MCVfxFunctions.h"

/// <summary>
/// Preferences' RENDERER: SOFTWARE chosen, its line says it takes a restart (nothing asked); VULKAN back and again
/// changes it back and then nothing; CANCEL puts the old choice back (and the switch shows it when Preferences opens
/// again); ACCEPT writes it to PREFS "Renderer"; the running game keeps its renderer.
/// </summary>
TEST_CASE_ISOLATED("game: the preferences screen chooses the renderer and saves it in PREFS")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::FinishChanges());
    // The tests' user folder overlays the install, so a PREFS written here would be every later test's.
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::filesystem::remove(prefsPath);
    const int32_t running = GRenderer;
    const int32_t chosen = GRendererPreference;
    REQUIRE_EQ(chosen, static_cast<int32_t>(MCRendererKind::Vulkan));
    constexpr std::string_view Restart = "Takes a restart.";

    // Another renderer than the running one: its line says it takes a restart, and nothing is asked.
    std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Display);
    REQUIRE(prefs != nullptr);
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Vulkan");
    CHECK_EQ(MCRemadeTest::Words(*prefs, "line-renderer"), Restart);
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "renderer", SDL_SCANCODE_RIGHT));
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Software));
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Software");
    CHECK_EQ(MCRemadeTest::Words(*prefs, "line-renderer"), Restart);
    CHECK(!MCRmlDialogs::Shown());

    // VULKAN back (the running renderer); VULKAN again: no change, nothing asked.
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "renderer", SDL_SCANCODE_LEFT));
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Vulkan");
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "renderer", SDL_SCANCODE_LEFT));
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Vulkan");
    CHECK(!MCRmlDialogs::Shown());
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "renderer", SDL_SCANCODE_RIGHT));
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Software");

    // CANCEL puts the choice back, and the switch shows it when Preferences opens again.
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "cancel"));
    CHECK_EQ(GRendererPreference, chosen);
    prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Display);
    REQUIRE(prefs != nullptr);
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Vulkan");

    REQUIRE(MCRemadeTest::KeyOn(*prefs, "renderer", SDL_SCANCODE_RIGHT));
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "accept"));
    CHECK_EQ(GRenderer, running);

    char renderer[32] = {};
    {
        MCFitIniFile prefsFile;
        REQUIRE_EQ(prefsFile.Open(MCFileSystem::ToUtf8(prefsPath).c_str()), 0);
        REQUIRE_EQ(prefsFile.SeekBlock("MechCommander"), 0);
        CHECK_EQ(prefsFile.ReadIdString("Renderer", std::span(renderer).first(sizeof(renderer) - 1)), 0);
    }

    std::filesystem::remove(prefsPath);
    CHECK(std::string_view(renderer) == "software");
}

/// <summary>
/// Preferences' INTERFACE volume shows the interface level when Preferences opens; its slider sets the level (and only
/// it: the effects volume stays) from silence to full, and CANCEL puts the level back.
/// </summary>
TEST_CASE_ISOLATED("game: INTERFACE VOLUME sets the interface level, and CANCEL puts it back")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::FinishChanges());
    MCSoundSystem& sound = *SoundSystem();
    sound.InterfaceLevel = 90;
    sound.DigitalMasterVolume = 40;
    const MCOptionalRef<const MCSettingsModel::MCSetting> found = MCSettingsModel::Find("interface");
    REQUIRE(found.has_value());
    const MCSettingsModel::MCSetting& volume = found->get();
    const std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Sound);
    REQUIRE(prefs != nullptr);
    Rml::Element* slider = MCRemadeTest::Find(*prefs, "interface");
    REQUIRE(slider != nullptr);
    const auto shown = [slider] { return slider->GetAttribute<int>("value", -1); };
    CHECK_EQ(shown(), volume.Get());
    CHECK_EQ(MCRemadeTest::Words(*prefs, "level-interface"),
             std::to_string(MCSettingsModel::LevelPercent(volume, volume.Get())));

    // The slider's ends, by its steps: silence, then full.
    constexpr int32_t Steps = 25;
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "interface", SDL_SCANCODE_LEFT, Steps));
    CHECK_EQ(shown(), 0);
    CHECK_EQ(sound.InterfaceLevel, 0);
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "interface", SDL_SCANCODE_RIGHT, Steps));
    CHECK_EQ(sound.InterfaceLevel, 127);
    CHECK_EQ(sound.DigitalMasterVolume, 40);

    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "cancel"));
    CHECK_EQ(sound.InterfaceLevel, 90);
    CHECK_EQ(sound.DigitalMasterVolume, 40);
}

/// <summary>
/// Preferences' SCROLL SPEED shows the scroll speed when Preferences opens (AUTO, the original's pace) and offers 1.5X
/// to 3X in half steps besides; a choice sets the speed, CANCEL puts it back (and the stepper shows it), and ACCEPT
/// writes it to PREFS "ScrollSpeed".
/// </summary>
TEST_CASE_ISOLATED("game: SCROLL SPEED sets the scroll speed, CANCEL puts it back and ACCEPT saves it in PREFS")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::FinishChanges());
    // The tests' user folder overlays the install, so a PREFS written here would be every later test's.
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::filesystem::remove(prefsPath);
    REQUIRE_EQ(GScrollSpeed, AutoScrollSpeed);

    std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Display);
    REQUIRE(prefs != nullptr);
    constexpr std::array<std::string_view, 5> Labels = {"Auto", "1.5x", "2x", "2.5x", "3x"};
    REQUIRE_EQ(Labels.size(), ScrollSpeeds.size());

    // Each choice in turn, its words and its speed; then back to AUTO.
    for (size_t i = 0; i < Labels.size(); i++)
    {
        const MCTest::Scope scope(Labels[i]);
        CHECK_EQ(MCRemadeTest::StepWords(*prefs, "scroll-speed"), Labels[i]);
        CHECK_EQ(GScrollSpeed, ScrollSpeeds[i]);
        REQUIRE(MCRemadeTest::KeyOn(*prefs, "scroll-speed", SDL_SCANCODE_RIGHT));
    }

    REQUIRE(MCRemadeTest::KeyOn(*prefs, "scroll-speed", SDL_SCANCODE_LEFT, static_cast<int32_t>(Labels.size())));
    CHECK_EQ(GScrollSpeed, AutoScrollSpeed);

    // 2X, then CANCEL: AUTO again, shown when Preferences opens.
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "scroll-speed", SDL_SCANCODE_RIGHT, 2));
    CHECK_EQ(GScrollSpeed, 200);
    CHECK_EQ(MCRemadeTest::StepWords(*prefs, "scroll-speed"), "2x");
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "cancel"));
    CHECK_EQ(GScrollSpeed, AutoScrollSpeed);
    prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Display);
    REQUIRE(prefs != nullptr);
    CHECK_EQ(MCRemadeTest::StepWords(*prefs, "scroll-speed"), "Auto");

    // 3X, then ACCEPT: written to PREFS.
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "scroll-speed", SDL_SCANCODE_RIGHT, 4));
    CHECK_EQ(GScrollSpeed, 300);
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "accept"));
    CHECK_EQ(GScrollSpeed, 300);
    int32_t saved = 0;
    {
        MCFitIniFile prefsFile;
        REQUIRE_EQ(prefsFile.Open(prefsPath.string().c_str()), 0);
        REQUIRE_EQ(prefsFile.SeekBlock("MechCommander"), 0);
        saved = prefsFile.Read<int32_t>("ScrollSpeed").value_or(0);
    }

    std::filesystem::remove(prefsPath);
    CHECK_EQ(saved, 300);
}
