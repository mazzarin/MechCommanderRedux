#pragma once

class MCBigGameObject;
class MCFitIniFile;
class MCMover;

/// <summary>The object type marines are made from (0x28d): the ones a destroyed building lets out, and a dead vehicle's crew.</summary>
extern int32_t DefaultPilotId;
/// <summary>The pilot profile file (in the profile path) of those marines.</summary>
inline constexpr std::string_view MarineProfileName = "PEM00001";
/// <summary>How many marines have been made this mission; each takes part id 0xfff minus this count.</summary>
extern int32_t NumMarines;

/// <summary>
/// A new marine (object type <see cref="DefaultPilotId"/>, awake) with the marine profile loaded; Fatal with
/// <paramref name="createFailure"/> when it can't be made, and with the profile's errors when that can't be read.
/// </summary>
std::shared_ptr<MCMover> MakeMarine(std::string_view createFailure);

/// <summary>
/// Port-only (the first-use rule): the marine profile, read for the mission under the loading bar (MCScenario's
/// MarineProfile) so the marines made in play read nothing; null when it can't be opened, which the first marine made
/// then finds as the original did.
/// </summary>
std::unique_ptr<MCFitIniFile> ReadMarineProfile();

/// <summary>
/// Loads the marine profile into <paramref name="marine"/> (the mission's, read at the load); Fatal with the profile's
/// errors when it can't be opened or read.
/// </summary>
void LoadMarineProfile(MCMover& marine);

/// <summary>
/// Lets <paramref name="marinesWanted"/> marines out of the destroyed <paramref name="building"/>, one for each enemy
/// warrior (from the second on, as the original counted) without a working vehicle: each is piloted by its warrior,
/// placed by <paramref name="place"/> (which also puts it on the object map), sent to the building's side's mech list,
/// and ordered to stand.
/// </summary>
void LetOutBuildingMarines(MCBigGameObject& building, int32_t marinesWanted,
                           const std::function<void(MCMover&)>& place);
