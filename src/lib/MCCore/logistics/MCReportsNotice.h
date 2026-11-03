#pragma once

// Port-only (the personal build): the reports' notice or question on the main menu (main/MCTelemetry.h), shown once,
// when one is due and the main menu is up with nothing over it. The notice's OK leaves REPORTS on; the question's OK
// turns it on and its CANCEL leaves it off. Either way PREFS keeps the answer, and HD OPTIONS changes it later.

/// <summary>Shows the notice or the question when one is due and the main menu is free (else it waits).</summary>
void ShowReportsNotice();

/// <summary>The notice's text, and the question's.</summary>
std::string_view ReportsNoticeText();
std::string_view ReportsQuestionText();
