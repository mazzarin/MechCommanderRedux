#include "stdafx.h"
#include "ui/MCRmlProbe.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlModel.h"

#include <RmlUi/Core/ElementDocument.h>

namespace
{
    constexpr const char* ProbeDocument = "probe.rml";
    constexpr const char* WindowDocument = "probe_window.rml";
    constexpr const char* ModelName = "probe";

    /// <summary>The sample games' maps, in turn.</summary>
    constexpr std::array<std::string_view, 4> SampleMaps = {"Steel Rain", "Capture Base", "Lines", "Canyon"};
    constexpr int MostPlayers = 8;
}

MCRmlProbe::MCRmlProbe()
{
    SetDocument(ProbeDocument);
    Keys = {{"Q E", "Change tab"}, {"Esc", "Back"}};
}

MCRmlProbe::~MCRmlProbe() = default;

std::vector<MCRmlProbe::MCGame> MCRmlProbe::SampleGames(int count)
{
    std::vector<MCGame> games;

    for (int i = 0; i < count; ++i)
    {
        games.push_back(MCGame{.Name = std::format("Game {}", i + 1),
                               .Map = std::string(SampleMaps[static_cast<size_t>(i) % SampleMaps.size()]),
                               .Players = 1 + i % MostPlayers});
    }

    return games;
}

void MCRmlProbe::SetGames(std::vector<MCGame> games)
{
    _Games = std::move(games);
    _Picked = std::clamp(_Picked, 0, std::max(static_cast<int>(_Games.size()) - 1, 0));
    _Count = std::format("{} games", _Games.size());

    if (_Model != nullptr)
    {
        _Model->Changed("games");
        _Model->Changed("count");
        _Model->Changed("picked");
    }
}

void MCRmlProbe::MakeModels(Rml::Context& context)
{
    _Model = std::make_unique<MCRmlModel>(context, ModelName);
    MCRmlModel& model = *_Model;
    model.Value("volume", _Volume);
    model.Value("name", _Name);
    model.Value("picked", _Picked);
    model.Value("status", _Status);
    model.Value("count", _Count);
    model.List("games", _Games, std::pair{"name", &MCGame::Name}, std::pair{"map", &MCGame::Map},
               std::pair{"players", &MCGame::Players});
    model.Event("pick",
                [this](const Rml::VariantList& arguments)
                {
                    if (!arguments.empty())
                    {
                        _Picked = arguments.front().Get<int>();
                        _Model->Changed("picked");
                    }
                });

    if (_Games.empty())
    {
        SetGames(SampleGames(MostPlayers * 2));
    }
}

void MCRmlProbe::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick("back",
            [this]
            {
                if (Back)
                {
                    Back();
                }
                else
                {
                    Hide();
                }
            });
    OnClick("window", [this] { OpenWindow(); });
    OnClick("join",
            [this]
            {
                ++_Joins;
                _Status = _Games.empty() ? std::string()
                                         : std::format("Joining {}", _Games[static_cast<size_t>(_Picked)].Name);
                _Model->Changed("status");
            });
}

void MCRmlProbe::OpenWindow()
{
    if (_Window)
    {
        _Window->Show();
        return;
    }

    const std::weak_ptr<MCRmlScreen> self = weak_from_this();
    MCGuiHandle<MCRmlProbeWindow> window = Layer().CreateChild<MCRmlProbeWindow>(
        [self](MCRmlProbeWindow& made)
        {
            made.SetDocument(WindowDocument);
            made.Modal = true;
            made.Keys = {{"Enter", "Leave"}, {"Esc", "Cancel"}};
            made.Back = [&made] { made.Hide(); };
            made.Leaving = [self, &made]
            {
                made.Hide();

                if (const std::shared_ptr<MCRmlScreen> probe = self.lock(); probe != nullptr && probe->Back)
                {
                    probe->Back();
                }
            };
        });

    if (const auto opened = window->Open(); !opened)
    {
        // Let go of, so the next press tries again.
        SDL_Log("Menus: the probe's window can't open: %s", opened.error().c_str());
        window->Close();
        return;
    }

    _Window = window;
}

void MCRmlProbe::OnDestroy()
{
    if (const std::shared_ptr<MCRmlScreen> window = _Window.Lock(); window != nullptr)
    {
        window->Close();
    }

    _Model.reset();
    MCRmlScreen::OnDestroy();
}

void MCRmlProbeWindow::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick("cancel", [this] { Back(); });
    OnClick("leave",
            [this]
            {
                if (Leaving)
                {
                    Leaving();
                }
            });
}
