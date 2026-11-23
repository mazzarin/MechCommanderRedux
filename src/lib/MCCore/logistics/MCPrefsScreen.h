#pragma once

#include "iface/MCKeyBindings.h"
#include "logistics/MCSettingsScreen.h"

/// <summary>
/// Port-only (SPEC-menus.md, "Preferences"; stage 6): Preferences remade (<c>ui/prefs.rml</c>), one document in two
/// places: from the main menu on the night ground (the menus' layer, a screen of the flow), and in a mission from the
/// pause's ALL PREFERENCES over the battlefield (the mission's layer, at its interface size). Its sections are tabs down
/// the left, Q and E between them; their rows are the settings model's (logistics/MCSettingsModel), each change shown
/// at once. ACCEPT keeps the changes (PREFS.CFG written), CANCEL and Escape put back what the screen opened with,
/// DEFAULTS puts back the section's defaults, ORIGINAL (on HD) turns every HD look off and LAPTOP (on CONTROLS) sets
/// 1998's laptop keys. CONTROLS: a click on a key's box waits for a key (PRESS A KEY), a right-click clears it. In a
/// mission a row the mission can't take is held, its line saying why.
/// </summary>
class MCPrefsScreen final : public MCSettingsScreen
{
public:
    /// <summary>Where it opens.</summary>
    enum class MCPlace : uint8_t
    {
        /// <summary>From the main menu: the menus' layer, the night ground.</summary>
        Menus,
        /// <summary>From the pause: the mission's layer, over the battlefield.</summary>
        Mission,
    };

    explicit MCPrefsScreen(MCPlace place = MCPlace::Menus);

    /// <summary>What ACCEPT does once the settings are kept (the menus: the screen below; a mission: the pause).</summary>
    std::function<void()> Accepted;

    /// <summary>CANCEL (and Escape, the flow's Leave): the settings put back as the screen opened with them, once.</summary>
    void Leave() override;

    MCPlace Place() const { return _Place; }
    MCSettingsModel::MCSection Section() const { return _Section; }
    /// <summary>Shows <paramref name="section"/>'s rows, its tab chosen (Q and E, a click on a tab).</summary>
    void ShowSection(MCSettingsModel::MCSection section);

    /// <summary>The keys CONTROLS edits (the player's when the screen opened, until ACCEPT).</summary>
    const MCKeyBindings::MCKeyTable& EditedKeys() const { return _Keys; }
    /// <summary>Whether a key's box waits for a key (PRESS A KEY).</summary>
    bool Binding() const { return _Binding.has_value(); }

    bool TakesKeys() const override { return _Binding.has_value(); }
    bool OnRawKey(const SDL_KeyboardEvent& key) override;
    void OnFrame() override;

protected:
    void OnLoaded(Rml::ElementDocument& document) override;
    void OnDestroy() override;
    void BeforeRestart() override;

private:
    /// <summary>A key's box waiting for a key: its command (the row's or its alternate's).</summary>
    struct MCWaiting
    {
        MCKeyCommand Command = MCKeyCommand::Unused;
    };

    /// <summary>The section's tabs and rows written into the document, and listened to.</summary>
    void MakeRows(Rml::ElementDocument& document);
    /// <summary>CONTROLS' boxes set from <see cref="_Keys"/>.</summary>
    void RefreshKeys();
    /// <summary>The foot's buttons the section shows (ORIGINAL on HD, LAPTOP on CONTROLS).</summary>
    void ShowFoot();
    /// <summary>ACCEPT: the changes kept.</summary>
    void Accept();
    /// <summary>DEFAULTS: the section's defaults (CONTROLS: the original's keys).</summary>
    void UseDefaults();
    /// <summary>A click or right-click on CONTROLS' boxes.</summary>
    void OnKeyBox(Rml::Event& event);
    /// <summary>Binds <paramref name="key"/> to the waiting box's command, then waits no more.</summary>
    void Bind(uint32_t key);
    void StopWaiting();

    MCPlace _Place;
    MCSettingsModel::MCSection _Section = MCSettingsModel::MCSection::Game;
    MCSettingsModel::MCSaved _Saved;
    MCKeyBindings::MCKeyTable _Keys{};
    std::optional<MCWaiting> _Binding;
    /// <summary>A modifier pressed while waiting: bound alone when it's let go with no other key.</summary>
    int16_t _PendingModifier = 0;
    /// <summary>ACCEPT or CANCEL ran: the other doesn't.</summary>
    bool _Done = false;
};
