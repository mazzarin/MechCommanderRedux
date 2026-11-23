#pragma once

#include "ui/MCRmlScreen.h"

class MCRmlModel;

/// <summary>
/// Port-only (SPEC-menus.md, stage 2): the building blocks' test screen (<c>ui/probe.rml</c>), which no player sees:
/// every control of the kit, a list bound to a model, a 1998 picture and the hint line. Its window
/// (<see cref="MCRmlProbeWindow"/>) opens over it. The tests and the menus tour show it.
/// </summary>
class MCRmlProbe final : public MCRmlScreen
{
public:
    /// <summary>A row of its list: a game, as INTERNET's will be.</summary>
    struct MCGame
    {
        std::string Name;
        std::string Map;
        int Players = 0;
    };

    MCRmlProbe();
    ~MCRmlProbe() override;

    /// <summary>The list's games (the model's), shown from the next frame.</summary>
    void SetGames(std::vector<MCGame> games);
    /// <summary>A probe's usual list: <paramref name="count"/> games, numbered.</summary>
    static std::vector<MCGame> SampleGames(int count);

    /// <summary>The model's values, as bound (the tests read them).</summary>
    int Volume() const { return _Volume; }
    const std::string& Name() const { return _Name; }
    int Picked() const { return _Picked; }
    const std::string& Status() const { return _Status; }
    /// <summary>How many times JOIN GAME was pressed.</summary>
    int Joins() const { return _Joins; }

    /// <summary>The window, when open.</summary>
    MCGuiHandle<MCRmlScreen> Window() const { return _Window; }

    void OnDestroy() override;

protected:
    void MakeModels(Rml::Context& context) override;
    void OnLoaded(Rml::ElementDocument& document) override;

private:
    void OpenWindow();

    std::unique_ptr<MCRmlModel> _Model;
    std::vector<MCGame> _Games;
    int _Volume = 50;
    std::string _Name = "Commander";
    int _Picked = 0;
    std::string _Status;
    std::string _Count;
    int _Joins = 0;
    MCGuiHandle<MCRmlScreen> _Window;
};

/// <summary>The probe's window (<c>ui/probe_window.rml</c>): a question over the probe, opened on CANCEL.</summary>
class MCRmlProbeWindow final : public MCRmlScreen
{
public:
    /// <summary>What LEAVE does (CANCEL and Escape close the window).</summary>
    std::function<void()> Leaving;

protected:
    void OnLoaded(Rml::ElementDocument& document) override;
};
