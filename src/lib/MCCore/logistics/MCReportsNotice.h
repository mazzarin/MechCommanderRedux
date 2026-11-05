#pragma once
#include "main/MCTelemetry.h"

// Port-only (the personal build): the reports' notice or question on the main menu (main/MCTelemetry.h), shown once,
// when one is due and the main menu is up with nothing over it; or on the session screen, before the match, for the
// players a lobby starts (who never see the main menu). The notice's OK leaves REPORTS on; the question's OK turns it
// on and its CANCEL leaves it off. Either way PREFS keeps the answer, and HD OPTIONS changes it later.

/// <summary>
/// Shows the notice or the question when one is due and the main menu, or the session screen, is free (else it waits).
/// </summary>
void ShowReportsNotice();

/// <summary>What the main menu says for <paramref name="prompt"/>: a notice's text, or the question's; none, nothing.</summary>
std::string_view ReportsText(MCTelemetry::MCPrompt prompt);
